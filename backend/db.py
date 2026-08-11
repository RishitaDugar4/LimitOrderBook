"""PostgreSQL persistence for orders and trades.

The matching engine calls the persistence hook inline on its single writer
thread, so the hook must never block on the database. Instead the hook only
does a fast queue put; a separate PersistenceWriter thread drains the queue
and batch-inserts, keeping DB latency off the matching hot path.

Persistence is optional: if DATABASE_URL is unset, init_engine() is a no-op
and make_persistence_hook() returns None, so the app runs fully in-memory.
"""

from __future__ import annotations

import os
import queue
import threading
import logging
from datetime import datetime, timezone
from typing import Callable, Optional

from sqlalchemy import (
    create_engine, BigInteger, Integer, String, DateTime, Numeric, func,
)
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, sessionmaker

log = logging.getLogger("orderbook.db")


class Base(DeclarativeBase):
    pass


# BIGSERIAL on Postgres, but SQLite only auto-increments INTEGER PRIMARY KEY,
# so fall back to INTEGER there (used for local dev / tests).
BigIntPK = BigInteger().with_variant(Integer, "sqlite")


class OrderRow(Base):
    __tablename__ = "orders"

    id: Mapped[int] = mapped_column(BigIntPK, primary_key=True, autoincrement=True)
    order_id: Mapped[int] = mapped_column(BigInteger, index=True)
    action: Mapped[str] = mapped_column(String(16))  # add | cancel | modify
    order_type: Mapped[Optional[str]] = mapped_column(String(32), nullable=True)
    side: Mapped[Optional[str]] = mapped_column(String(8), nullable=True)
    price: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    quantity: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), index=True
    )


class TradeRow(Base):
    __tablename__ = "trades"

    id: Mapped[int] = mapped_column(BigIntPK, primary_key=True, autoincrement=True)
    bid_order_id: Mapped[int] = mapped_column(BigInteger, index=True)
    ask_order_id: Mapped[int] = mapped_column(BigInteger, index=True)
    price: Mapped[int] = mapped_column(Integer)
    quantity: Mapped[int] = mapped_column(Integer)
    executed_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), index=True
    )


# ---- module state -----------------------------------------------------
_engine = None
_SessionLocal = None
_writer: Optional["PersistenceWriter"] = None


def database_url() -> Optional[str]:
    return os.environ.get("DATABASE_URL")


def init_engine() -> bool:
    """Create the engine, tables, and start the writer. No-op without a URL."""
    global _engine, _SessionLocal, _writer
    url = database_url()
    if not url:
        log.info("DATABASE_URL not set; persistence disabled")
        return False

    _engine = create_engine(url, pool_pre_ping=True, pool_size=5, max_overflow=5)
    Base.metadata.create_all(_engine)
    _SessionLocal = sessionmaker(bind=_engine, expire_on_commit=False)
    _writer = PersistenceWriter(_SessionLocal)
    _writer.start()
    log.info("persistence enabled -> %s", url.split("@")[-1])
    return True


def shutdown() -> None:
    if _writer is not None:
        _writer.stop()


class PersistenceWriter:
    """Background thread that batch-inserts orders/trades off the hot path."""

    def __init__(self, session_factory, batch_max: int = 200, flush_ms: int = 250):
        self._session_factory = session_factory
        self._queue: "queue.Queue[Optional[dict]]" = queue.Queue(maxsize=10000)
        self._thread = threading.Thread(
            target=self._run, name="db-writer", daemon=True
        )
        self._batch_max = batch_max
        self._flush_s = flush_ms / 1000.0
        self._running = False

    def start(self) -> None:
        self._running = True
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        self._queue.put(None)
        if self._thread.is_alive():
            self._thread.join(timeout=5)

    def enqueue(self, event: dict) -> None:
        try:
            self._queue.put_nowait(event)
        except queue.Full:
            log.warning("persistence queue full; dropping event")

    def _run(self) -> None:
        pending: list[dict] = []
        while self._running or not self._queue.empty():
            try:
                item = self._queue.get(timeout=self._flush_s)
                if item is None:
                    break
                pending.append(item)
                if len(pending) < self._batch_max:
                    continue
            except queue.Empty:
                pass
            if pending:
                self._flush(pending)
                pending = []
        if pending:
            self._flush(pending)

    def _flush(self, events: list[dict]) -> None:
        orders: list[OrderRow] = []
        trades: list[TradeRow] = []
        for ev in events:
            ts = datetime.fromtimestamp(ev.get("ts", 0), tz=timezone.utc)
            order = ev.get("order")
            if order:
                orders.append(OrderRow(
                    order_id=order["order_id"],
                    action=order.get("action", "add"),
                    order_type=order.get("order_type"),
                    side=order.get("side"),
                    price=order.get("price"),
                    quantity=order.get("quantity"),
                ))
            for t in ev.get("trades", []):
                trades.append(TradeRow(
                    bid_order_id=t["bid"]["order_id"],
                    ask_order_id=t["ask"]["order_id"],
                    price=t["price"],
                    quantity=t["quantity"],
                    executed_at=ts,
                ))
        try:
            with self._session_factory() as session:
                if orders:
                    session.add_all(orders)
                if trades:
                    session.add_all(trades)
                session.commit()
        except Exception:  # never let a DB error kill the writer thread
            log.exception("failed to flush %d events", len(events))


def make_persistence_hook() -> Optional[Callable[[dict], None]]:
    if _writer is None:
        return None
    writer = _writer
    return lambda event: writer.enqueue(event)


# ---- read helpers for the REST history endpoints ----------------------
def recent_trades(limit: int = 100) -> list[dict]:
    if _SessionLocal is None:
        return []
    with _SessionLocal() as session:
        rows = (
            session.query(TradeRow)
            .order_by(TradeRow.executed_at.desc(), TradeRow.id.desc())
            .limit(limit)
            .all()
        )
        return [
            {
                "id": r.id,
                "bid_order_id": r.bid_order_id,
                "ask_order_id": r.ask_order_id,
                "price": r.price,
                "quantity": r.quantity,
                "executed_at": r.executed_at.isoformat(),
            }
            for r in rows
        ]


def recent_orders(limit: int = 100) -> list[dict]:
    if _SessionLocal is None:
        return []
    with _SessionLocal() as session:
        rows = (
            session.query(OrderRow)
            .order_by(OrderRow.created_at.desc(), OrderRow.id.desc())
            .limit(limit)
            .all()
        )
        return [
            {
                "id": r.id,
                "order_id": r.order_id,
                "action": r.action,
                "order_type": r.order_type,
                "side": r.side,
                "price": r.price,
                "quantity": r.quantity,
                "created_at": r.created_at.isoformat(),
            }
            for r in rows
        ]
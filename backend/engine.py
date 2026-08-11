"""Single-writer wrapper around the C++ Orderbook.

The C++ Orderbook is single-producer: AddOrder / CancelOrder / ModifyOrder /
MakeOrder may only be called from ONE thread. FastAPI serves requests from a
pool of threads (and async tasks hop threads), so we must never call the book
directly from a request handler.

This module funnels every mutating call through a single dedicated worker
thread. Handlers enqueue a Command and await its result; the worker is the
sole caller of the C++ API. After each command the worker also publishes the
resulting trades and a fresh L2 snapshot to any subscribed WebSocket clients.
"""

from __future__ import annotations

import os
import sys
import queue
import threading
import asyncio
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Optional

# Locate the compiled pybind11 module. In dev it lives in the CMake build
# dir; in the Docker image it is installed on sys.path. Override with
# LOB_MODULE_PATH if needed.
_default_build = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build-py"
)
_module_path = os.environ.get("LOB_MODULE_PATH", _default_build)
if _module_path and _module_path not in sys.path:
    sys.path.insert(0, _module_path)

import orderbook_py as ob  # noqa: E402

# String <-> enum mapping so the HTTP/JSON layer never touches C++ enums.
ORDER_TYPES = {
    "GoodTillCancel": ob.OrderType.GoodTillCancel,
    "FillAndKill": ob.OrderType.FillAndKill,
    "FillOrKill": ob.OrderType.FillOrKill,
    "GoodForDay": ob.OrderType.GoodForDay,
    "Market": ob.OrderType.Market,
}
SIDES = {"Buy": ob.Side.Buy, "Sell": ob.Side.Sell}


@dataclass
class Command:
    """A unit of work for the writer thread, with a slot for its result."""
    fn: Callable[["MatchingEngine"], Any]
    done: threading.Event = field(default_factory=threading.Event)
    result: Any = None
    error: Optional[BaseException] = None


class MatchingEngine:
    """Owns the C++ book and the single writer thread that mutates it."""

    def __init__(self, order_pool_capacity: Optional[int] = None):
        self._book = (
            ob.Orderbook(order_pool_capacity)
            if order_pool_capacity is not None
            else ob.Orderbook()
        )
        self._ids = ob.OrderIdGenerator()
        self._queue: "queue.Queue[Optional[Command]]" = queue.Queue()
        self._worker = threading.Thread(
            target=self._run, name="orderbook-writer", daemon=True
        )
        self._running = False

        # Async fan-out. The worker thread is a plain thread, so it hands
        # events to the asyncio loop via call_soon_threadsafe.
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._subscribers: set[asyncio.Queue] = set()
        self._sub_lock = threading.Lock()

        # Optional persistence hook (Phase 3). Called on the worker thread
        # with (trades, snapshot) after each command; kept side-effect-light.
        self.on_event: Optional[Callable[[dict], None]] = None

    # ---- lifecycle ----------------------------------------------------
    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop
        self._running = True
        self._worker.start()

    def stop(self) -> None:
        self._running = False
        self._queue.put(None)  # sentinel to unblock the worker
        if self._worker.is_alive():
            self._worker.join(timeout=5)

    # ---- worker loop --------------------------------------------------
    def _run(self) -> None:
        while self._running:
            cmd = self._queue.get()
            if cmd is None:  # shutdown sentinel
                break
            try:
                cmd.result = cmd.fn(self)
            except BaseException as exc:  # surface to the awaiting handler
                cmd.error = exc
            finally:
                cmd.done.set()

    def _submit(self, fn: Callable[["MatchingEngine"], Any]) -> Any:
        """Enqueue work for the writer thread and block until it completes.

        Safe to call from any thread/async task: only the writer thread ever
        touches the C++ book.
        """
        cmd = Command(fn=fn)
        self._queue.put(cmd)
        cmd.done.wait()
        if cmd.error is not None:
            raise cmd.error
        return cmd.result

    # ---- publish/subscribe for WebSocket streaming --------------------
    def subscribe(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=256)
        with self._sub_lock:
            self._subscribers.add(q)
        return q

    def unsubscribe(self, q: asyncio.Queue) -> None:
        with self._sub_lock:
            self._subscribers.discard(q)

    def _publish(self, event: dict) -> None:
        """Fan an event out to all subscriber queues (from the worker thread)."""
        if self._loop is None:
            return
        with self._sub_lock:
            targets = list(self._subscribers)

        def deliver():
            for q in targets:
                try:
                    q.put_nowait(event)
                except asyncio.QueueFull:
                    pass  # slow client: drop rather than stall the engine
        self._loop.call_soon_threadsafe(deliver)

    def _after_command(self, trades: list, order: Optional[dict] = None) -> None:
        """Runs on the worker thread after a mutating command.

        Publishes to WebSocket subscribers and (if configured) hands a richer
        event to the persistence hook. on_event MUST be cheap/non-blocking --
        it runs inline on the writer thread, so it should only enqueue.
        """
        snapshot = self._book.get_order_infos()
        ts = time.time()
        if trades:
            self._publish({"type": "trades", "trades": trades, "ts": ts})
        self._publish({"type": "book", "book": snapshot, "ts": ts})
        if self.on_event is not None:
            self.on_event(
                {"order": order, "trades": trades, "book": snapshot, "ts": ts}
            )

    # ---- public API (all funnel through _submit) ----------------------
    def next_order_id(self) -> int:
        return self._ids.next()

    def add_limit_order(
        self, order_type: str, side: str, price: int, quantity: int,
        order_id: Optional[int] = None,
    ) -> dict:
        oid = order_id if order_id is not None else self.next_order_id()
        ot = ORDER_TYPES[order_type]
        sd = SIDES[side]

        def work(self_: "MatchingEngine"):
            trades = self_._book.add_order(ot, oid, sd, price, quantity)
            record = {
                "order_id": oid, "order_type": order_type, "side": side,
                "price": price, "quantity": quantity, "action": "add",
            }
            self_._after_command(trades, record)
            return trades

        trades = self._submit(work)
        return {"order_id": oid, "trades": trades}

    def add_market_order(
        self, side: str, quantity: int, order_id: Optional[int] = None
    ) -> dict:
        oid = order_id if order_id is not None else self.next_order_id()
        sd = SIDES[side]

        def work(self_: "MatchingEngine"):
            trades = self_._book.add_market_order(oid, sd, quantity)
            record = {
                "order_id": oid, "order_type": "Market", "side": side,
                "price": None, "quantity": quantity, "action": "add",
            }
            self_._after_command(trades, record)
            return trades

        trades = self._submit(work)
        return {"order_id": oid, "trades": trades}

    def cancel_order(self, order_id: int) -> None:
        def work(self_: "MatchingEngine"):
            self_._book.cancel_order(order_id)
            self_._after_command([], {"order_id": order_id, "action": "cancel"})
        self._submit(work)

    def modify_order(
        self, order_id: int, side: str, price: int, quantity: int
    ) -> dict:
        sd = SIDES[side]

        def work(self_: "MatchingEngine"):
            trades = self_._book.modify_order(order_id, sd, price, quantity)
            record = {
                "order_id": order_id, "order_type": None, "side": side,
                "price": price, "quantity": quantity, "action": "modify",
            }
            self_._after_command(trades, record)
            return trades

        trades = self._submit(work)
        return {"order_id": order_id, "trades": trades}

    def snapshot(self) -> dict:
        # Read-only; GetOrderInfos takes the book's internal mutex so this is
        # safe to call off-thread, but we route it through the worker anyway
        # to get a consistent view relative to pending writes.
        return self._submit(lambda self_: self_._book.get_order_infos())

    def size(self) -> int:
        return self._submit(lambda self_: self_._book.size())
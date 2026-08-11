"""FastAPI + WebSocket front end for the C++ matching engine.

REST endpoints submit orders and read the book; the WebSocket streams live
trade prints and L2 book snapshots to all connected clients. All order
mutation funnels through MatchingEngine's single writer thread.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware

from .engine import MatchingEngine
from .models import (
    LimitOrderRequest,
    MarketOrderRequest,
    ModifyOrderRequest,
    OrderResponse,
    BookSnapshot,
)

log = logging.getLogger("orderbook")

engine = MatchingEngine()


_db = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _db
    # Optional DB persistence: enabled only when DATABASE_URL is set.
    try:
        from . import db  # noqa: WPS433
        if db.init_engine():
            engine.on_event = db.make_persistence_hook()
            _db = db
    except Exception:  # persistence must never block the engine from starting
        log.exception("persistence init failed; running in-memory only")
    engine.start(asyncio.get_running_loop())
    log.info("matching engine started")
    yield
    engine.stop()
    if _db is not None:
        _db.shutdown()
    log.info("matching engine stopped")


app = FastAPI(title="Limit Order Book", version="1.0.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # tighten to the frontend origin in production
    allow_methods=["*"],
    allow_headers=["*"],
)


# ---- REST -------------------------------------------------------------
@app.get("/health")
async def health():
    return {"status": "ok", "size": await asyncio.to_thread(engine.size)}


@app.post("/orders", response_model=OrderResponse)
async def submit_limit_order(req: LimitOrderRequest):
    if req.order_type == "Market":
        raise HTTPException(400, "Use /orders/market for market orders")
    return await asyncio.to_thread(
        engine.add_limit_order,
        req.order_type, req.side, req.price, req.quantity, req.order_id,
    )


@app.post("/orders/market", response_model=OrderResponse)
async def submit_market_order(req: MarketOrderRequest):
    return await asyncio.to_thread(
        engine.add_market_order, req.side, req.quantity, req.order_id
    )


@app.put("/orders", response_model=OrderResponse)
async def modify_order(req: ModifyOrderRequest):
    return await asyncio.to_thread(
        engine.modify_order, req.order_id, req.side, req.price, req.quantity
    )


@app.delete("/orders/{order_id}")
async def cancel_order(order_id: int):
    await asyncio.to_thread(engine.cancel_order, order_id)
    return {"order_id": order_id, "cancelled": True}


@app.get("/book", response_model=BookSnapshot)
async def get_book():
    return await asyncio.to_thread(engine.snapshot)


@app.get("/history/trades")
async def history_trades(limit: int = 100):
    if _db is None:
        return {"trades": [], "persistence": False}
    return {"trades": await asyncio.to_thread(_db.recent_trades, limit),
            "persistence": True}


@app.get("/history/orders")
async def history_orders(limit: int = 100):
    if _db is None:
        return {"orders": [], "persistence": False}
    return {"orders": await asyncio.to_thread(_db.recent_orders, limit),
            "persistence": True}


# ---- WebSocket --------------------------------------------------------
@app.websocket("/ws")
async def stream(websocket: WebSocket):
    await websocket.accept()
    q = engine.subscribe()
    # Send an initial snapshot so a fresh client renders immediately.
    with contextlib.suppress(Exception):
        snap = await asyncio.to_thread(engine.snapshot)
        await websocket.send_json({"type": "book", "book": snap})
    try:
        while True:
            event = await q.get()
            await websocket.send_json(event)
    except WebSocketDisconnect:
        pass
    finally:
        engine.unsubscribe(q)
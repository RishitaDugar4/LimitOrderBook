"""Request/response schemas for the order-book API."""

from __future__ import annotations

from typing import Literal, Optional
from pydantic import BaseModel, Field

OrderTypeStr = Literal[
    "GoodTillCancel", "FillAndKill", "FillOrKill", "GoodForDay", "Market"
]
SideStr = Literal["Buy", "Sell"]


class LimitOrderRequest(BaseModel):
    order_type: OrderTypeStr = "GoodTillCancel"
    side: SideStr
    price: int = Field(..., description="Price in integer ticks")
    quantity: int = Field(..., gt=0)
    order_id: Optional[int] = Field(
        None, description="Optional client-supplied id; server mints one if omitted"
    )


class MarketOrderRequest(BaseModel):
    side: SideStr
    quantity: int = Field(..., gt=0)
    order_id: Optional[int] = None


class ModifyOrderRequest(BaseModel):
    order_id: int
    side: SideStr
    price: int
    quantity: int = Field(..., gt=0)


class TradeInfo(BaseModel):
    order_id: int
    price: int
    quantity: int


class Trade(BaseModel):
    bid: TradeInfo
    ask: TradeInfo
    price: int
    quantity: int


class OrderResponse(BaseModel):
    order_id: int
    trades: list[Trade]


class Level(BaseModel):
    price: int
    quantity: int


class BookSnapshot(BaseModel):
    bids: list[Level]
    asks: list[Level]
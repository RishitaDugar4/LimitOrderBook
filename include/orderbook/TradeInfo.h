#pragma once

#include "orderbook/Usings.h"

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};
#pragma once

#include <atomic>

#include "Usings.h"

// Thread-safe monotonically increasing OrderId source. Safe to share
// across multiple threads that mint IDs upstream of the book (e.g.
// several client-facing sessions funneling into the single order-entry
// producer thread); Orderbook itself never assigns IDs -- callers are
// expected to obtain one from here (or their own scheme) before
// constructing an Order.
//
// IDs start at 1 by default: 0 is used internally by Orderbook as the
// "unused" default for OrderCommand::cancelOrderId on Add-type commands,
// so avoiding it here sidesteps any confusion, though it is not otherwise
// reserved or rejected by the book.
class OrderIdGenerator {
public:
    explicit OrderIdGenerator(OrderId start = 1) : next_ { start } {}

    OrderIdGenerator(const OrderIdGenerator&) = delete;
    OrderIdGenerator& operator=(const OrderIdGenerator&) = delete;

    OrderId Next() {
        return next_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<OrderId> next_;
};

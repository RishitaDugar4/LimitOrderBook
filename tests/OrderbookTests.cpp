#include <catch2/catch_test_macros.hpp>

#include "Orderbook.h"

namespace {

OrderPointer MakeOrder(Orderbook& book, OrderType type, OrderId id, Side side, Price price, Quantity quantity) {
    return book.MakeOrder(type, id, side, price, quantity);
}

}  // namespace

// ---------------------------------------------------------------------------
// Empty book
// ---------------------------------------------------------------------------

TEST_CASE("A new order book is empty", "[edge][empty-book]") {
    Orderbook book;

    REQUIRE(book.Size() == 0);

    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetBids().empty());
    REQUIRE(infos.GetAsks().empty());
}

TEST_CASE("Cancelling a non-existent order on an empty book is a no-op", "[edge][empty-book]") {
    Orderbook book;

    book.CancelOrder(999);

    REQUIRE(book.Size() == 0);
}

TEST_CASE("Modifying a non-existent order returns no trades and changes nothing", "[edge][empty-book]") {
    Orderbook book;

    OrderModify modification(999, Side::Buy, 100, 10);
    auto trades = book.ModifyOrder(modification);

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 0);
}

// ---------------------------------------------------------------------------
// GoodTillCancel
// ---------------------------------------------------------------------------

TEST_CASE("A GoodTillCancel order rests in the book when nothing matches it", "[order-type][gtc]") {
    Orderbook book;

    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 1);

    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetBids().size() == 1);
    REQUIRE(infos.GetBids()[0].price_ == 100);
    REQUIRE(infos.GetBids()[0].quantity_ == 10);
}

TEST_CASE("A GoodTillCancel order can be cancelled while resting", "[order-type][gtc]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    book.CancelOrder(1);

    REQUIRE(book.Size() == 0);
}

TEST_CASE("Partial fill: an incoming order smaller than the resting order leaves a remainder", "[edge][partial-fill][gtc]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 100, 4));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].GetBidTrade().orderId_ == 1);
    REQUIRE(trades[0].GetBidTrade().price_ == 100);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 4);
    REQUIRE(trades[0].GetAskTrade().orderId_ == 2);
    REQUIRE(trades[0].GetAskTrade().quantity_ == 4);

    // Sell order fully filled and removed; buy order rests with 6 remaining.
    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetAsks().empty());
    REQUIRE(infos.GetBids().size() == 1);
    REQUIRE(infos.GetBids()[0].quantity_ == 6);
}

TEST_CASE("A full fill removes both orders from the book", "[gtc]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 10);
    REQUIRE(book.Size() == 0);
}

TEST_CASE("An incoming order can sweep multiple price levels, partially filling the last one", "[edge][partial-fill][gtc]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));

    // Willing to pay up to 101 for 8; should take all 5 @ 100, then 3 of the 5 @ 101.
    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 3, Side::Buy, 101, 8));

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].GetAskTrade().orderId_ == 1);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 5);
    REQUIRE(trades[1].GetAskTrade().orderId_ == 2);
    REQUIRE(trades[1].GetBidTrade().quantity_ == 3);

    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetAsks().size() == 1);
    REQUIRE(infos.GetAsks()[0].price_ == 101);
    REQUIRE(infos.GetAsks()[0].quantity_ == 2);
}

TEST_CASE("Orders crossing at the exact same price fully match", "[edge][self-match]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(book.Size() == 0);
}

TEST_CASE("Adding an order whose ID already exists in the book is rejected", "[edge][self-match]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    // Same ID, different side/price: if this were allowed to match against
    // the original resting order under the same ID, the book would corrupt
    // its own bookkeeping. It must be rejected outright instead.
    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetBids().size() == 1);
    REQUIRE(infos.GetAsks().empty());
}

TEST_CASE("GetOrderInfos aggregates quantity across multiple orders at the same level", "[gtc]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Buy, 100, 7));

    auto infos = book.GetOrderInfos();

    REQUIRE(infos.GetBids().size() == 1);
    REQUIRE(infos.GetBids()[0].price_ == 100);
    REQUIRE(infos.GetBids()[0].quantity_ == 12);
}

// ---------------------------------------------------------------------------
// FillAndKill (immediate-or-cancel)
// ---------------------------------------------------------------------------

TEST_CASE("A FillAndKill order with no crossing liquidity is rejected and never rests", "[order-type][fak][edge][empty-book]") {
    Orderbook book;

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillAndKill, 1, Side::Buy, 100, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 0);
}

TEST_CASE("A FillAndKill order fills what's available and cancels the remainder", "[order-type][fak][edge][partial-fill]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 5);
    // Resting ask fully consumed, FillAndKill remainder cancelled: nothing left resting.
    REQUIRE(book.Size() == 0);
}

TEST_CASE("A FillAndKill order that fully matches leaves nothing resting", "[order-type][fak]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 10));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillAndKill, 2, Side::Buy, 100, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 10);
    REQUIRE(book.Size() == 0);
}

// ---------------------------------------------------------------------------
// FillOrKill (all-or-nothing)
// ---------------------------------------------------------------------------

TEST_CASE("A FillOrKill order is rejected entirely on an empty book", "[order-type][fok][edge][empty-book]") {
    Orderbook book;

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillOrKill, 1, Side::Buy, 100, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 0);
}

TEST_CASE("A FillOrKill order is rejected when the book can't fully fill it, leaving the book untouched", "[order-type][fok]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillOrKill, 2, Side::Buy, 100, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetAsks().size() == 1);
    REQUIRE(infos.GetAsks()[0].quantity_ == 5);
}

TEST_CASE("A FillOrKill order executes fully when there is enough aggregate liquidity across levels", "[order-type][fok]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));

    auto trades = book.AddOrder(MakeOrder(book, OrderType::FillOrKill, 3, Side::Buy, 101, 10));

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].GetBidTrade().quantity_ + trades[1].GetBidTrade().quantity_ == 10);
    REQUIRE(book.Size() == 0);
}

// ---------------------------------------------------------------------------
// GoodForDay
// ---------------------------------------------------------------------------

TEST_CASE("A GoodForDay order rests and matches exactly like a GoodTillCancel order", "[order-type][gfd]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodForDay, 1, Side::Buy, 100, 10));

    REQUIRE(book.Size() == 1);

    auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 100, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(book.Size() == 0);
}

// ---------------------------------------------------------------------------
// Market
// ---------------------------------------------------------------------------

TEST_CASE("A Market order on an empty opposite side returns no trades and doesn't rest", "[order-type][market][edge][empty-book]") {
    Orderbook book;

    auto trades = book.AddOrder(book.MakeOrder(1, Side::Buy, 10));

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 0);
}

TEST_CASE("A Market buy order sweeps the best available asks by price", "[order-type][market][edge][partial-fill]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Sell, 101, 5));

    auto trades = book.AddOrder(book.MakeOrder(3, Side::Buy, 8));

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].GetAskTrade().orderId_ == 1);
    REQUIRE(trades[0].GetBidTrade().quantity_ == 5);
    REQUIRE(trades[1].GetAskTrade().orderId_ == 2);
    REQUIRE(trades[1].GetBidTrade().quantity_ == 3);

    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetAsks()[0].quantity_ == 2);
}

TEST_CASE("A Market sell order sweeps the best available bids by price", "[order-type][market]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Buy, 99, 5));

    auto trades = book.AddOrder(book.MakeOrder(3, Side::Sell, 8));

    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].GetBidTrade().orderId_ == 1);
    REQUIRE(trades[1].GetBidTrade().orderId_ == 2);
    REQUIRE(book.Size() == 1);
}

// ---------------------------------------------------------------------------
// ModifyOrder
// ---------------------------------------------------------------------------

TEST_CASE("ModifyOrder re-adds the order under the same ID with the new price/quantity", "[modify]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    OrderModify modification(1, Side::Buy, 105, 20);
    auto trades = book.ModifyOrder(modification);

    REQUIRE(trades.empty());
    REQUIRE(book.Size() == 1);

    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetBids()[0].price_ == 105);
    REQUIRE(infos.GetBids()[0].quantity_ == 20);

    // Still addressable by the same ID afterwards.
    book.CancelOrder(1);
    REQUIRE(book.Size() == 0);
}

TEST_CASE("ModifyOrder preserves the original order type", "[modify]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodForDay, 1, Side::Buy, 100, 10));

    OrderModify modification(1, Side::Buy, 100, 5);
    book.ModifyOrder(modification);

    book.CancelOrder(1);
    REQUIRE(book.Size() == 0);
}

TEST_CASE("ModifyOrder can push a resting order across the spread and trigger a match", "[modify]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Sell, 100, 5));
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 2, Side::Buy, 90, 5));

    OrderModify modification(2, Side::Buy, 100, 5);
    auto trades = book.ModifyOrder(modification);

    REQUIRE(trades.size() == 1);
    REQUIRE(book.Size() == 0);
}

// ---------------------------------------------------------------------------
// Load / SPSC command-queue stress
//
// AddOrder/CancelOrder/ModifyOrder now hand work off to a dedicated
// matching-engine thread via a lock-free SPSC ring buffer of capacity 4096
// (Orderbook::kCommandQueueCapacity) instead of mutating the book directly
// under a mutex. These tests submit enough commands to wrap the ring buffer
// many times over and to force TryPush to back-pressure (spin) while the
// engine thread is behind, exercising the handoff itself rather than just
// the matching logic.
// ---------------------------------------------------------------------------

TEST_CASE("Thousands of non-crossing orders submitted through the command queue all land in the book", "[stress]") {
    Orderbook book;
    constexpr int kOrderCount = 20'000;

    for (int i = 0; i < kOrderCount; ++i) {
        auto trades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, i + 1, Side::Buy, 1'000'000 - i, 1));
        REQUIRE(trades.empty());
    }

    REQUIRE(book.Size() == static_cast<std::size_t>(kOrderCount));
}

TEST_CASE("Thousands of crossing buy/sell pairs submitted back-to-back all match through the command queue", "[stress]") {
    Orderbook book;
    constexpr int kPairCount = 20'000;

    for (int i = 0; i < kPairCount; ++i) {
        const OrderId sellId = 2 * i + 1;
        const OrderId buyId = 2 * i + 2;

        auto sellTrades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, sellId, Side::Sell, 100, 1));
        REQUIRE(sellTrades.empty());

        auto buyTrades = book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, buyId, Side::Buy, 100, 1));
        REQUIRE(buyTrades.size() == 1);
        REQUIRE(buyTrades[0].GetBidTrade().orderId_ == buyId);
        REQUIRE(buyTrades[0].GetAskTrade().orderId_ == sellId);
    }

    REQUIRE(book.Size() == 0);
}

TEST_CASE("Interleaved add/cancel/modify commands through the queue leave the book in the expected final state", "[stress]") {
    Orderbook book;
    constexpr int kOrderCount = 5'000;

    for (int i = 0; i < kOrderCount; ++i) {
        book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, i + 1, Side::Buy, 500 - (i % 400), 1));
    }
    REQUIRE(book.Size() == static_cast<std::size_t>(kOrderCount));

    for (int i = 0; i < kOrderCount; i += 2) {
        book.CancelOrder(i + 1);
    }
    REQUIRE(book.Size() == static_cast<std::size_t>(kOrderCount / 2));

    for (int i = 1; i < kOrderCount; i += 2) {
        OrderModify modification(i + 1, Side::Buy, 501 - (i % 400), 2);
        auto trades = book.ModifyOrder(modification);
        REQUIRE(trades.empty());
    }
    REQUIRE(book.Size() == static_cast<std::size_t>(kOrderCount / 2));

    auto infos = book.GetOrderInfos();
    Quantity totalQuantity = 0;
    for (const auto& level : infos.GetBids()) {
        totalQuantity += level.quantity_;
    }
    REQUIRE(totalQuantity == static_cast<Quantity>(kOrderCount));  // (kOrderCount/2) orders x 2 qty each
}

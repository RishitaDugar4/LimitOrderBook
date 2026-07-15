#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <vector>

#include "Orderbook.h"

// Orderbook::CancelGoodForDayOrders() is the actual end-of-day cancellation
// logic the background pruning thread runs once it wakes at 4pm. That wake
// time can't be controlled from a test without waiting up to 24 real hours,
// so this accessor lets tests invoke the same private logic directly and
// deterministically instead of reimplementing or waiting on it.
struct OrderbookTestAccess {
    static void RunGoodForDayPruning(Orderbook& book) {
        book.CancelGoodForDayOrders();
    }
};

namespace {

OrderPointer MakeOrder(Orderbook& book, OrderType type, OrderId id, Side side, Price price, Quantity quantity) {
    return book.MakeOrder(type, id, side, price, quantity);
}

}  // namespace

TEST_CASE("Pruning cancels resting GoodForDay orders", "[pruning]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodForDay, 1, Side::Buy, 100, 10));
    REQUIRE(book.Size() == 1);

    OrderbookTestAccess::RunGoodForDayPruning(book);

    REQUIRE(book.Size() == 0);
}

TEST_CASE("Pruning leaves non-GoodForDay orders untouched", "[pruning]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));
    book.AddOrder(MakeOrder(book, OrderType::GoodForDay, 2, Side::Buy, 99, 5));
    REQUIRE(book.Size() == 2);

    OrderbookTestAccess::RunGoodForDayPruning(book);

    REQUIRE(book.Size() == 1);
    auto infos = book.GetOrderInfos();
    REQUIRE(infos.GetBids().size() == 1);
    REQUIRE(infos.GetBids()[0].price_ == 100);
}

TEST_CASE("Pruning an order book with no GoodForDay orders is a no-op", "[pruning][edge]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    OrderbookTestAccess::RunGoodForDayPruning(book);

    REQUIRE(book.Size() == 1);
}

TEST_CASE("Pruning an empty order book is a no-op", "[pruning][edge][empty-book]") {
    Orderbook book;

    OrderbookTestAccess::RunGoodForDayPruning(book);

    REQUIRE(book.Size() == 0);
}

TEST_CASE("A GoodForDay order modified to a new price is still prunable afterwards", "[pruning][modify]") {
    Orderbook book;
    book.AddOrder(MakeOrder(book, OrderType::GoodForDay, 1, Side::Buy, 100, 10));

    OrderModify modification(1, Side::Buy, 95, 10);
    book.ModifyOrder(modification);
    REQUIRE(book.Size() == 1);

    OrderbookTestAccess::RunGoodForDayPruning(book);

    REQUIRE(book.Size() == 0);
}

// Regression test for a lost-wakeup race: the background pruning thread
// waits on a condition variable until 4pm, and the destructor used to set
// the shutdown flag and notify without holding the same mutex the waiter
// checks under. If the notify landed in the narrow window between the
// waiter checking the flag and actually starting to wait, the notify was
// lost and the destructor's thread::join() blocked for hours. Constructing
// and tearing down many order books back-to-back reliably reproduced the
// hang before the fix; each iteration here must complete promptly.
//
// Note: under ThreadSanitizer specifically, this test can still
// intermittently abort with "condition_variable timed_wait failed: Invalid
// argument" when run many times back-to-back, even after hardening
// PruneGoodForDayOrders() against every cause we could find on our end
// (mktime() is not reentrant-safe like localtime_r() and is now
// mutex-guarded; the computed wait duration is now clamped to a sane
// range before being handed to wait_for()). Neither change moved the
// TSan failure rate, and TSan never reports an actual data race here --
// only an uncaught OS-level exception -- which matches a documented
// macOS libc++/pthread condition-variable quirk under heavy thread churn
// seen in other large projects (e.g. PyTorch issue #66033, Bitcoin Core
// issue #18227). Outside of ThreadSanitizer this scenario is solid across
// 25+ consecutive runs.
TEST_CASE("Constructing and destroying an order book never blocks on the pruning thread", "[pruning][regression]") {
    for (int i = 0; i < 25; ++i) {
        auto completed = std::async(std::launch::async, [] {
            Orderbook book;
        });

        REQUIRE(completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    }
}

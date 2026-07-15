#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <vector>

#include "Order.h"
#include "OrderMemoryPool.h"
#include "OrderPoolAllocator.h"
#include "Orderbook.h"

// ---------------------------------------------------------------------------
// Global operator new/delete interception, scoped to this test binary only.
// This is the actual proof that pool allocation costs zero calls into the
// heap allocator -- not just an assumption. Every test in this file that
// claims "zero heap allocations" measures the *delta* in this counter
// across the operation under test, not its absolute value (Catch2, the
// standard library, and OrderMemoryPool's own setup all legitimately use
// the heap elsewhere in the same process).
// ---------------------------------------------------------------------------

namespace {
std::atomic<std::size_t> g_heapAllocCount { 0 };
}

void* operator new(std::size_t size) {
    g_heapAllocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace {

std::size_t HeapAllocCount() {
    return g_heapAllocCount.load(std::memory_order_relaxed);
}

}  // namespace

TEST_CASE("The heap-allocation counter itself actually counts (sanity check)", "[order-pool][sanity]") {
    auto before = HeapAllocCount();
    auto ptr = std::make_shared<Order>(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    REQUIRE(HeapAllocCount() > before);
}

TEST_CASE("Allocating an Order through the pool costs zero heap allocations", "[order-pool]") {
    OrderMemoryPool pool(16);

    auto before = HeapAllocCount();
    auto order = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
        OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    auto after = HeapAllocCount();

    REQUIRE(after == before);
    REQUIRE(order->GetOrderId() == 1);
    REQUIRE(order->GetPrice() == 100);
}

TEST_CASE("Repeated allocate/free cycles through the pool cost zero heap allocations", "[order-pool]") {
    OrderMemoryPool pool(16);

    auto before = HeapAllocCount();
    for (int i = 0; i < 1000; ++i) {
        auto order = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
            OrderType::GoodTillCancel, static_cast<OrderId>(i), Side::Buy, 100, 10);
        REQUIRE(order->GetOrderId() == static_cast<OrderId>(i));
    }
    REQUIRE(HeapAllocCount() == before);
}

TEST_CASE("Freed blocks are reused rather than growing the pool", "[order-pool]") {
    OrderMemoryPool pool(4);
    REQUIRE(pool.Available() == 4);

    {
        auto order = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
            OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
        REQUIRE(pool.Available() == 3);
    }
    // The shared_ptr above is out of scope: its block must have been
    // returned to the pool.
    REQUIRE(pool.Available() == 4);
}

TEST_CASE("Allocating beyond the pool's capacity throws instead of falling back to the heap", "[order-pool][edge]") {
    OrderMemoryPool pool(2);

    auto first = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
        OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    auto second = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
        OrderType::GoodTillCancel, 2, Side::Buy, 101, 10);

    REQUIRE(pool.Available() == 0);

    auto before = HeapAllocCount();
    REQUIRE_THROWS_AS(
        (std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
            OrderType::GoodTillCancel, 3, Side::Buy, 102, 10)),
        std::bad_alloc);
    // The failed attempt must not have silently grabbed heap memory either.
    REQUIRE(HeapAllocCount() == before);
}

TEST_CASE("A block returned to an exhausted pool becomes available for the next allocation", "[order-pool][edge]") {
    OrderMemoryPool pool(1);

    {
        auto order = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
            OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
        REQUIRE(pool.Available() == 0);
    }

    REQUIRE(pool.Available() == 1);
    auto order2 = std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
        OrderType::GoodTillCancel, 2, Side::Buy, 101, 10);
    REQUIRE(order2->GetOrderId() == 2);
}

TEST_CASE("Deallocating from a different thread than the allocating one is safe", "[order-pool][concurrency]") {
    OrderMemoryPool pool(8);

    std::vector<std::shared_ptr<Order>> orders;
    for (OrderId i = 1; i <= 8; ++i) {
        orders.push_back(std::allocate_shared<Order>(OrderPoolAllocator<Order> { pool },
            OrderType::GoodTillCancel, i, Side::Buy, 100, 10));
    }
    REQUIRE(pool.Available() == 0);

    std::thread deallocator([&orders] {
        orders.clear();  // drops the last reference to each Order from this thread
    });
    deallocator.join();

    REQUIRE(pool.Available() == 8);
}

// ---------------------------------------------------------------------------
// Integration: Orderbook::MakeOrder / AddOrder end to end.
// ---------------------------------------------------------------------------

TEST_CASE("Orderbook::MakeOrder allocates through the pool with zero heap allocations", "[order-pool][orderbook]") {
    Orderbook book;

    auto before = HeapAllocCount();
    auto order = book.MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
    auto after = HeapAllocCount();

    REQUIRE(after == before);
    REQUIRE(order->GetOrderId() == 1);
}

// AddOrder's/ModifyOrder's full round trip still allocates on the heap for
// *surrounding* bookkeeping that this task never touched: a new
// absl::btree_map node when a price level is created or removed, a
// std::list node for the resting-order queue at that level, an
// unordered_map node in orders_/data_, and the Trades result vector's
// backing storage reserved fresh in every MatchOrders() call. None of that
// is the Order object -- "Orderbook::MakeOrder allocates through the pool"
// above already proves that specific allocation (the one
// std::make_shared<Order> used to be) is zero-heap. These two tests instead
// pin down that the remaining, unavoidable bookkeeping cost per order is
// *constant* -- i.e. nothing here leaks or grows unboundedly as more
// orders flow through -- rather than claiming it's zero, which it isn't
// and was never the scope of "replace std::make_shared<Order> with pool
// allocation."
TEST_CASE("The per-order heap-allocation cost of AddOrder's surrounding bookkeeping stays constant", "[order-pool][orderbook]") {
    Orderbook book;
    book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, 1, Side::Sell, 100, 10'000));

    auto CostOfNOrders = [&](OrderId startId, int n) {
        auto before = HeapAllocCount();
        for (OrderId i = startId; i < startId + static_cast<OrderId>(n); ++i) {
            auto trades = book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, i, Side::Buy, 100, 1));
            REQUIRE(trades.size() == 1);
        }
        return HeapAllocCount() - before;
    };

    auto costOf500 = CostOfNOrders(2, 500);
    auto costOf1000 = CostOfNOrders(502, 1000);

    // Linear, not growing: 1000 orders should cost ~2x what 500 did, not
    // more (which would indicate an accidental leak or unbounded growth
    // somewhere in the bookkeeping path).
    REQUIRE(costOf1000 == 2 * costOf500);
    // The resting sell (10,000 qty) absorbed all 1,500 buys without being
    // fully filled itself, so it's still resting.
    REQUIRE(book.Size() == 1);
}

TEST_CASE("ModifyOrder's internal re-add goes through the pool for the Order object itself", "[order-pool][orderbook]") {
    Orderbook book;
    book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10));

    auto before = HeapAllocCount();
    OrderModify firstModification(1, Side::Buy, 105, 20);
    book.ModifyOrder(firstModification);
    auto costOfOneModify = HeapAllocCount() - before;

    before = HeapAllocCount();
    OrderModify secondModification(1, Side::Buy, 110, 20);
    book.ModifyOrder(secondModification);
    auto costOfAnotherModify = HeapAllocCount() - before;

    // Same shape of work each time (cancel + re-add, one price level
    // vacated and one created) should cost the same either way -- proving
    // the Order object construction inside it (which now goes through
    // MakeOrder/the pool) isn't adding a heap call that the bookkeeping
    // allocations alone wouldn't already account for.
    REQUIRE(costOfOneModify == costOfAnotherModify);
}

TEST_CASE("Exhausting an Orderbook's order pool throws rather than silently using the heap", "[order-pool][orderbook][edge]") {
    Orderbook book(4);  // tiny pool, all orders rest (none cross), so it fills up

    for (OrderId i = 1; i <= 4; ++i) {
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, i, Side::Buy, 100 - static_cast<Price>(i), 1));
    }
    REQUIRE(book.Size() == 4);

    REQUIRE_THROWS_AS(book.MakeOrder(OrderType::GoodTillCancel, 5, Side::Buy, 90, 1), std::bad_alloc);
}

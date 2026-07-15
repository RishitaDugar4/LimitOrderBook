#include <benchmark/benchmark.h>

#include <memory>

#include "Orderbook.h"
#include "OrderModify.h"

namespace {

constexpr int kBaseOrders = 10'000;

// Seeds `book` with `count` resting GoodTillCancel buy orders spread across
// distinct price levels, none of which cross (the book has no asks).
OrderId SeedRestingBids(Orderbook& book, OrderId nextId, int count) {
    for (int i = 0; i < count; ++i) {
        Price price = 1'000 + static_cast<Price>(i % 500);
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, nextId++, Side::Buy, price, 10));
    }
    return nextId;
}

}  // namespace

static void BM_AddOrder_NoMatch(benchmark::State& state) {
    // Every iteration adds a new resting order that's never cancelled or
    // matched, so the book (and its fixed-capacity order pool) only ever
    // grows. Iterations() pins the iteration count so this stays within
    // the pool's capacity regardless of how many iterations Google
    // Benchmark's auto-tuning would otherwise pick on a given machine --
    // it also makes runs directly comparable across machines/invocations.
    Orderbook book;
    OrderId nextId = 1;
    Price price = 1'000'000;

    for (auto _ : state) {
        auto order = book.MakeOrder(OrderType::GoodTillCancel, nextId++, Side::Buy, price--, 10);
        benchmark::DoNotOptimize(book.AddOrder(order));
    }
}
BENCHMARK(BM_AddOrder_NoMatch)->Iterations(50'000);

static void BM_AddOrder_WithMatch(benchmark::State& state) {
    Orderbook book;
    OrderId nextId = 1;

    for (auto _ : state) {
        state.PauseTiming();
        auto askId = nextId++;
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, askId, Side::Sell, 100, 10));
        state.ResumeTiming();

        auto bidId = nextId++;
        auto order = book.MakeOrder(OrderType::GoodTillCancel, bidId, Side::Buy, 100, 10);
        benchmark::DoNotOptimize(book.AddOrder(order));
    }
}
BENCHMARK(BM_AddOrder_WithMatch);

static void BM_CancelOrder(benchmark::State& state) {
    Orderbook book;
    OrderId nextId = SeedRestingBids(book, 1, kBaseOrders);

    for (auto _ : state) {
        state.PauseTiming();
        auto id = nextId++;
        Price price = 1'000 + static_cast<Price>(id % 500);
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, id, Side::Buy, price, 10));
        state.ResumeTiming();

        book.CancelOrder(id);
    }
}
BENCHMARK(BM_CancelOrder);

static void BM_ModifyOrder(benchmark::State& state) {
    Orderbook book;
    OrderId nextId = SeedRestingBids(book, 1, kBaseOrders);

    const OrderId modifyId = nextId++;
    book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, modifyId, Side::Buy, 1'000, 10));

    Price price = 1'000;
    for (auto _ : state) {
        price = (price == 1'000) ? 1'001 : 1'000;
        OrderModify modification(modifyId, Side::Buy, price, 10);
        benchmark::DoNotOptimize(book.ModifyOrder(modification));
    }
}
BENCHMARK(BM_ModifyOrder);

static void BM_GetOrderInfos(benchmark::State& state) {
    Orderbook book;
    OrderId nextId = 1;

    const int numLevels = static_cast<int>(state.range(0));
    for (int i = 0; i < numLevels; ++i) {
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, nextId++, Side::Buy, 1'000 - i, 10));
        book.AddOrder(book.MakeOrder(OrderType::GoodTillCancel, nextId++, Side::Sell, 2'000 + i, 10));
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(book.GetOrderInfos());
    }
}
BENCHMARK(BM_GetOrderInfos)->Arg(10)->Arg(100)->Arg(1'000)->Arg(5'000);

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <thread>
#include <vector>

#include "OrderIdGenerator.h"

TEST_CASE("IDs start at 1 by default and increase by exactly 1 each call", "[order-id-generator]") {
    OrderIdGenerator generator;

    REQUIRE(generator.Next() == 1);
    REQUIRE(generator.Next() == 2);
    REQUIRE(generator.Next() == 3);
}

TEST_CASE("A custom start value is honored", "[order-id-generator][edge]") {
    OrderIdGenerator generator(1000);

    REQUIRE(generator.Next() == 1000);
    REQUIRE(generator.Next() == 1001);
}

TEST_CASE("Sequential calls from a single thread never repeat or skip", "[order-id-generator]") {
    OrderIdGenerator generator;
    constexpr int kCount = 10'000;

    for (OrderId i = 1; i <= kCount; ++i) {
        REQUIRE(generator.Next() == i);
    }
}

TEST_CASE("Concurrent callers from multiple threads each get a unique ID", "[order-id-generator][concurrency]") {
    OrderIdGenerator generator;
    constexpr int kThreadCount = 8;
    constexpr int kPerThread = 20'000;

    std::vector<std::vector<OrderId>> perThreadIds(kThreadCount);
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&generator, &perThreadIds, t] {
            auto& ids = perThreadIds[t];
            ids.reserve(kPerThread);
            for (int i = 0; i < kPerThread; ++i) {
                ids.push_back(generator.Next());
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::vector<OrderId> allIds;
    allIds.reserve(kThreadCount * kPerThread);
    for (const auto& ids : perThreadIds) {
        allIds.insert(allIds.end(), ids.begin(), ids.end());
    }

    std::sort(allIds.begin(), allIds.end());
    REQUIRE(allIds.size() == static_cast<std::size_t>(kThreadCount * kPerThread));

    // No duplicates: every adjacent pair in the sorted list must differ.
    REQUIRE(std::adjacent_find(allIds.begin(), allIds.end()) == allIds.end());

    // The IDs handed out are exactly the contiguous range [1, N], proving
    // no gaps either.
    REQUIRE(allIds.front() == 1);
    REQUIRE(allIds.back() == static_cast<OrderId>(kThreadCount * kPerThread));
}

cmake_minimum_required(VERSION 3.16)
project(LimitOrderBook CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
    lockfree
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../lockfree-lib
)
FetchContent_MakeAvailable(lockfree)

set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20260107.0
)
FetchContent_MakeAvailable(abseil)

add_library(orderbook_lib
    Orderbook.cpp
    OrderMemoryPool.cpp
)
target_include_directories(orderbook_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orderbook_lib PUBLIC lockfree::lockfree absl::btree)
find_package(Threads REQUIRED)
target_link_libraries(orderbook_lib PUBLIC Threads::Threads)

add_executable(limit_order_book main.cpp)
target_link_libraries(limit_order_book PRIVATE orderbook_lib)

option(LOB_BUILD_BENCHMARKS "Build the Google Benchmark suite" ON)

if(LOB_BUILD_BENCHMARKS)
    include(FetchContent)

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(orderbook_bench
        benchmarks/OrderbookBenchmark.cpp
    )
    target_link_libraries(orderbook_bench PRIVATE orderbook_lib benchmark::benchmark_main)
endif()

option(LOB_BUILD_TESTS "Build the Catch2 test suite" ON)

if(LOB_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(catch2)

    add_executable(orderbook_tests
        tests/OrderTests.cpp
        tests/OrderbookTests.cpp
        tests/OrderbookPruningTests.cpp
        tests/OrderIdGeneratorTests.cpp
        tests/OrderMemoryPoolTests.cpp
    )
    target_link_libraries(orderbook_tests PRIVATE orderbook_lib Catch2::Catch2WithMain)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
    catch_discover_tests(orderbook_tests PROPERTIES TIMEOUT 60)
endif()

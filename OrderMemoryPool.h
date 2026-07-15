#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "Order.h"

// Pre-allocated, fixed-capacity pool of fixed-size blocks, each large
// enough to hold an Order plus whatever internal control-block layout
// std::allocate_shared combines with it into a single allocation (see
// OrderPoolAllocator.h). Allocating an Order through this pool costs zero
// calls into the global heap allocator, for predictable, low latency --
// as long as the pool has a free block; once exhausted, Allocate() throws
// rather than silently falling back to the heap.
//
// Threading: Orders are only ever *allocated* from the single order-
// submission producer thread (mirroring Orderbook::AddOrder's existing
// single-producer constraint), but can be *deallocated* from whichever
// thread drops the last shared_ptr reference to an Order -- normally the
// matching-engine thread (on every fill/cancel), but occasionally the
// GoodForDay pruning thread too (cancelling a GoodForDay order at end of
// day). That makes the free list a genuine multi-producer/single-consumer
// structure, so it's guarded by a mutex rather than being lock-free.
// Contention is expected to be negligible (the pruning thread touches it
// at most once a day), and a mutex around a pointer-only free list is
// still far cheaper and more latency-predictable than the heap allocation
// it replaces.
class OrderMemoryPool {
public:
    static constexpr std::size_t kBlockAlign = alignof(std::max_align_t);

    // Padding added on top of sizeof(Order) to comfortably fit whatever
    // control-block layout allocate_shared<Order> actually requests.
    // Verified against the real requested size at pool-construction time
    // (see the assertion in the .cpp) rather than just assumed.
    static constexpr std::size_t kControlBlockPadding = 128;
    static constexpr std::size_t kBlockSize =
        ((sizeof(Order) + kControlBlockPadding + kBlockAlign - 1) / kBlockAlign) * kBlockAlign;

    explicit OrderMemoryPool(std::size_t capacity);

    OrderMemoryPool(const OrderMemoryPool&) = delete;
    OrderMemoryPool& operator=(const OrderMemoryPool&) = delete;

    // Producer-side of the free list, consumer-side of order allocation:
    // only ever called from the order-submission thread. Throws
    // std::bad_alloc if `bytes` doesn't fit in a block, or if the pool is
    // exhausted.
    void* Allocate(std::size_t bytes);

    // Consumer-side of the free list: safe to call from any thread.
    void Deallocate(void* block, std::size_t bytes) noexcept;

    std::size_t Capacity() const noexcept { return capacity_; }
    std::size_t Available() const;

private:
    std::size_t capacity_;
    std::unique_ptr<std::byte[]> storage_;
    std::vector<void*> freeList_;
    mutable std::mutex mutex_;
};

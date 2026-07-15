#pragma once

#include <cstddef>

#include "OrderMemoryPool.h"

// Minimal C++ Allocator adapter over OrderMemoryPool, intended for use
// with std::allocate_shared<Order>. allocate_shared rebinds the allocator
// to its own internal combined object+control-block type at the point it
// actually allocates, which this supports via the templated converting
// constructor below (the modern allocator model synthesizes rebind_alloc
// from that automatically -- no explicit nested rebind<U> needed).
template <typename T>
class OrderPoolAllocator {
public:
    using value_type = T;

    explicit OrderPoolAllocator(OrderMemoryPool& pool) noexcept : pool_ { &pool } {}

    template <typename U>
    OrderPoolAllocator(const OrderPoolAllocator<U>& other) noexcept : pool_ { other.pool_ } {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(pool_->Allocate(n * sizeof(T)));
    }

    void deallocate(T* ptr, std::size_t n) noexcept {
        pool_->Deallocate(ptr, n * sizeof(T));
    }

    OrderMemoryPool* pool_;
};

template <typename T, typename U>
bool operator==(const OrderPoolAllocator<T>& a, const OrderPoolAllocator<U>& b) noexcept {
    return a.pool_ == b.pool_;
}

template <typename T, typename U>
bool operator!=(const OrderPoolAllocator<T>& a, const OrderPoolAllocator<U>& b) noexcept {
    return !(a == b);
}

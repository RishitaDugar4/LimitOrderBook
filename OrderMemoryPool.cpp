#include "OrderMemoryPool.h"

#include <new>
#include <stdexcept>

OrderMemoryPool::OrderMemoryPool(std::size_t capacity) :
    capacity_ { capacity },
    storage_ { std::make_unique<std::byte[]>(capacity * kBlockSize) }
{
    freeList_.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        freeList_.push_back(storage_.get() + i * kBlockSize);
    }
}

void* OrderMemoryPool::Allocate(std::size_t bytes) {
    if (bytes > kBlockSize) {
        throw std::bad_alloc();
    }

    std::scoped_lock lock { mutex_ };
    if (freeList_.empty()) {
        throw std::bad_alloc();
    }

    void* block = freeList_.back();
    freeList_.pop_back();
    return block;
}

void OrderMemoryPool::Deallocate(void* block, std::size_t) noexcept {
    std::scoped_lock lock { mutex_ };
    freeList_.push_back(block);
}

std::size_t OrderMemoryPool::Available() const {
    std::scoped_lock lock { mutex_ };
    return freeList_.size();
}

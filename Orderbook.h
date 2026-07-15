#pragma once

#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <memory>
#include <utility>

#include <lockfree/SpscRingBuffer.h>
#include <absl/container/btree_map.h>

#include "Usings.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "OrderMemoryPool.h"
#include "OrderPoolAllocator.h"
#include "Trade.h"

// Threading model: exactly one producer thread submits orders (AddOrder /
// CancelOrder / ModifyOrder); a single dedicated matching-engine thread is
// the only thread that ever mutates bids_/asks_/orders_/data_ during normal
// operation, fed by a lock-free SPSC queue. ordersMutex_ therefore only
// ever has two possible owners: the matching-engine thread (briefly, while
// applying one command) and the once-a-day GoodForDay pruning thread -- it
// is not used to serialize order submission. Calling AddOrder/CancelOrder/
// ModifyOrder from more than one thread concurrently is undefined behavior,
// since the underlying ring buffer is single-producer.
class Orderbook {
private:
    struct OrderEntry {
        OrderPointer order_ { nullptr };
        OrderPointers::iterator location_;
    };

    struct LevelData {
        Quantity quantity_ { };
        Quantity count_ { };

        enum class Action {
            Add,
            Remove,
            Match,
        };
    };

    struct OrderCommand {
        enum class Type { Add, Cancel };

        Type type;
        OrderPointer order;             // used when type == Add
        OrderId cancelOrderId { 0 };    // used when type == Cancel

        Trades result;
        std::atomic<bool> ready { false };
    };

    static constexpr std::size_t kCommandQueueCapacity = 4096;

    // Declared first (and so, since members are destroyed in reverse
    // declaration order, destroyed *last*) because bids_/asks_/orders_
    // below store Orders allocated from this pool: destroying those
    // containers drops shared_ptr<Order> references, which can call back
    // into orderPool_.Deallocate(). If the pool were declared/destroyed
    // before them, that callback would run on an already-destroyed pool --
    // exactly the bug this ordering avoids (found via a real deterministic
    // crash: ~Orderbook() -> ~btree_map() -> ~OrderPointers() ->
    // ~shared_ptr<Order>() -> OrderMemoryPool::Deallocate() on a mutex
    // that no longer existed).
    OrderMemoryPool orderPool_;

    std::unordered_map<Price, LevelData> data_;

    // absl::btree_map, unlike std::map, does not guarantee that references/
    // pointers/iterators into the container survive an insertion or erasure
    // on that same container -- a rebalance can relocate any element at any
    // time. Every place that holds a reference into bids_/asks_ (e.g. the
    // `bids`/`asks` OrderPointers& in MatchOrders(), or `orders` in
    // CancelOrderInternal()) must stop using that reference before the next
    // mutating call on the SAME map. Mutating the OrderPointers list a
    // reference points at is fine (that's a separate std::list, untouched
    // by btree rebalancing); mutating bids_/asks_ itself while a reference
    // into it is still live afterwards is not.
    absl::btree_map<Price, OrderPointers, std::greater<Price>> bids_;
    absl::btree_map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;
    mutable std::mutex ordersMutex_;

    lockfree::SpscRingBuffer<OrderCommand*, kCommandQueueCapacity> commandQueue_;
    std::atomic<bool> engineShutdown_ { false };
    std::thread matchingEngineThread_;

    std::thread ordersPruneThread_;
    std::condition_variable shutdownConditionVariable_;
    std::atomic<bool> shutdown_ { false };

    void RunMatchingEngine();
    void ApplyCommand(OrderCommand& command);
    Trades Submit(OrderCommand& command);

    Trades AddOrderInternal(OrderPointer order);

    void PruneGoodForDayOrders();
    void CancelGoodForDayOrders();

    void CancelOrders(OrderIds orderids);
    void CancelOrderInternal(OrderId orderId);

    void OnOrderCancelled(OrderPointer order);
    void OnOrderAdded(OrderPointer order);
    void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
    void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

    bool CanFullyFill(Side side, Price price, Quantity quantity) const;
    bool CanMatch(Side side, Price price) const;
    Trades MatchOrders();

    friend struct OrderbookTestAccess;

public:
    static constexpr std::size_t kDefaultOrderPoolCapacity = 100'000;

    explicit Orderbook(std::size_t orderPoolCapacity = kDefaultOrderPoolCapacity);
    Orderbook(const Orderbook&) = delete;
    void operator=(const Orderbook&) = delete;
    Orderbook(Orderbook&&) = delete;
    void operator=(Orderbook&&) = delete;
    ~Orderbook();

    // Constructs an Order out of the pool instead of the heap -- use this
    // in place of std::make_shared<Order>(...) for orders meant to be
    // submitted to this book. Forwards to whichever Order constructor
    // matches Args (the full 5-arg constructor, or the 3-arg Market-order
    // one). Only safe to call from the single order-submission producer
    // thread, same as AddOrder/CancelOrder/ModifyOrder. Throws
    // std::bad_alloc if the pool is exhausted.
    template <typename... Args>
    OrderPointer MakeOrder(Args&&... args) {
        return std::allocate_shared<Order>(OrderPoolAllocator<Order> { orderPool_ }, std::forward<Args>(args)...);
    }

    Trades AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    Trades ModifyOrder(OrderModify order);

    std::size_t Size() const;
    OrderbookLevelInfos GetOrderInfos() const;
};
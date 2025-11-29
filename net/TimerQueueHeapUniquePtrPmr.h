#pragma once
#include "noncopyable.h"
#include "TimeStamp.h"
#include "timer.h"
#include "Callbacks.h"
#include "Channel.h"

#include <memory>
#include <memory_resource>
#include <vector>
#include <unordered_map>
#include <sys/timerfd.h>
#include <unistd.h>

class EventLoop;

namespace mymoduo::net {

// Scheme 1: unique_ptr + pmr pool for Timer objects.
// Each Timer is individually allocated from a pmr::unsynchronized_pool_resource.
// Advantages: Strong RAII, simple cancellation, easier future extension.
class TimerQueueHeapUniquePtrPmr : noncopyable {
public:
    explicit TimerQueueHeapUniquePtrPmr(EventLoop* loop);
    ~TimerQueueHeapUniquePtrPmr();

    int64_t addTimer(TimerCb cb, base::TimeStamp when, double interval);
    void cancel(int64_t sequence);
    void processExpiredForTest(base::TimeStamp now); // for testing without poll

private:
    // pmr pool for Timer allocations (object level) + separate pmr pool for container node/index structures (scheme B)
    std::pmr::unsynchronized_pool_resource timer_pool_{};        // alloc Timer objects
    std::pmr::unsynchronized_pool_resource container_pool_{};    // alloc heap_ & indexMap_ internal nodes/buckets

    // custom deleter using pmr allocator (defined before HeapNode for member type use)
    struct PmrTimerDeleter {
        std::pmr::unsynchronized_pool_resource* pool; // raw resource pointer (trivially swappable)
        void operator()(Timer* t) const noexcept {
            if(t){
                std::pmr::polymorphic_allocator<Timer> alloc(pool);
                std::allocator_traits<decltype(alloc)>::destroy(alloc, t);
                std::allocator_traits<decltype(alloc)>::deallocate(alloc, t, 1);
            }
        }
    };
    using PmrTimerPtr = std::unique_ptr<Timer, PmrTimerDeleter>;

    struct HeapNode {
        base::TimeStamp when;
        int64_t sequence;
        bool canceled;
        bool repeat;
        double interval;
        PmrTimerPtr timer; // pmr allocated with custom deleter
    };

    // heap storage (binary heap by expiration time) using pmr containers
    std::pmr::vector<HeapNode> heap_{ &container_pool_ };
    std::pmr::unordered_map<int64_t, size_t> indexMap_{ &container_pool_ }; // sequence -> index

    EventLoop* loop_;
    int timerfd_;
    Channel timerfdChannel_;

    static int createTimerfd();
    static void readTimerfd(int fd);
    static void setTimerfd(int fd, base::TimeStamp when);

    void handleRead(base::TimeStamp);
    void pushNode(HeapNode node);
    void popRoot();
    void reheapUp(size_t i);
    void reheapDown(size_t i);
    void updateTopTimerfd();
    void executeExpired(base::TimeStamp now);

    PmrTimerPtr makeTimer(TimerCb cb, base::TimeStamp when, double interval);
};

} // namespace mymoduo::net

#include "TimerQueueHeapUniquePtrPmr.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility> // std::move

using namespace mymoduo::net;


int TimerQueueHeapUniquePtrPmr::createTimerfd() {
    return ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}
void TimerQueueHeapUniquePtrPmr::readTimerfd(int fd) {
    uint64_t exp; ::read(fd, &exp, sizeof(exp));
}
void TimerQueueHeapUniquePtrPmr::setTimerfd(int fd, base::TimeStamp when) {
    itimerspec its{};
    int64_t us = when.microsecondsSinceEpoch();
    its.it_value.tv_sec  = us / base::TimeStamp::kMicroSecondsPerSecond;
    its.it_value.tv_nsec = (us % base::TimeStamp::kMicroSecondsPerSecond) * 1000;
    ::timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, nullptr);
}

TimerQueueHeapUniquePtrPmr::TimerQueueHeapUniquePtrPmr(EventLoop* loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(loop_, timerfd_) {
    timerfdChannel_.setReadEventCb([this](base::TimeStamp ts){ handleRead(ts); });
    timerfdChannel_.enableRead();
    heap_.reserve(128);
}

TimerQueueHeapUniquePtrPmr::~TimerQueueHeapUniquePtrPmr() {
    timerfdChannel_.disableAll();
    ::close(timerfd_);
}

TimerQueueHeapUniquePtrPmr::PmrTimerPtr
TimerQueueHeapUniquePtrPmr::makeTimer(TimerCb cb, base::TimeStamp when, double interval) {
    std::pmr::polymorphic_allocator<Timer> alloc{ &timer_pool_ };
    Timer* raw = std::allocator_traits<decltype(alloc)>::allocate(alloc, 1);
    try {
        std::allocator_traits<decltype(alloc)>::construct(alloc, raw, std::move(cb), when, interval);
    } catch(...) {
        std::allocator_traits<decltype(alloc)>::deallocate(alloc, raw, 1);
        throw;
    }
    return PmrTimerPtr(raw, PmrTimerDeleter{ &timer_pool_ });
}

void TimerQueueHeapUniquePtrPmr::reheapUp(size_t i) {
    while(i > 0) {
        size_t p = (i - 1) / 2;
        if(heap_[i].when < heap_[p].when) {
            std::swap(heap_[i].when, heap_[p].when);
            std::swap(heap_[i].sequence, heap_[p].sequence);
            std::swap(heap_[i].canceled, heap_[p].canceled);
            std::swap(heap_[i].repeat, heap_[p].repeat);
            std::swap(heap_[i].interval, heap_[p].interval);
            heap_[i].timer.swap(heap_[p].timer);
            indexMap_[heap_[i].sequence] = i;
            indexMap_[heap_[p].sequence] = p;
            i = p;
        } else break;
    }
}
void TimerQueueHeapUniquePtrPmr::reheapDown(size_t i) {
    size_t n = heap_.size();
    for(;;) {
        size_t l = 2*i + 1, r = l + 1, m = i;
        if(l < n && heap_[l].when < heap_[m].when) m = l;
        if(r < n && heap_[r].when < heap_[m].when) m = r;
        if(m == i) break;
        std::swap(heap_[i].when, heap_[m].when);
        std::swap(heap_[i].sequence, heap_[m].sequence);
        std::swap(heap_[i].canceled, heap_[m].canceled);
        std::swap(heap_[i].repeat, heap_[m].repeat);
        std::swap(heap_[i].interval, heap_[m].interval);
        heap_[i].timer.swap(heap_[m].timer);
        indexMap_[heap_[i].sequence] = i;
        indexMap_[heap_[m].sequence] = m;
        i = m;
    }
}

void TimerQueueHeapUniquePtrPmr::pushNode(HeapNode node) {
    heap_.push_back(std::move(node));
    size_t idx = heap_.size() - 1;
    indexMap_[heap_[idx].sequence] = idx;
    reheapUp(idx);
    updateTopTimerfd();
}
void TimerQueueHeapUniquePtrPmr::popRoot() {
    if(heap_.empty()) return;
    size_t last = heap_.size() - 1;
    std::swap(heap_[0].when, heap_[last].when);
    std::swap(heap_[0].sequence, heap_[last].sequence);
    std::swap(heap_[0].canceled, heap_[last].canceled);
    std::swap(heap_[0].repeat, heap_[last].repeat);
    std::swap(heap_[0].interval, heap_[last].interval);
    heap_[0].timer.swap(heap_[last].timer);
    indexMap_[heap_[0].sequence] = 0;
    indexMap_.erase(heap_[last].sequence);
    heap_.pop_back();
    if(!heap_.empty()) reheapDown(0);
    updateTopTimerfd();
}
void TimerQueueHeapUniquePtrPmr::updateTopTimerfd() {
    if(heap_.empty()) {
        itimerspec its{}; 
        ::timerfd_settime(timerfd_, TFD_TIMER_ABSTIME, &its, nullptr);
    } else {
        setTimerfd(timerfd_, heap_[0].when);
    }
}

//只返回sequence下标(绝对安全)
//虽然返回unique_ptr<Timer>也是安全的 因为move后不会改变地址(unique_ptr移动语义)
//并且Timer是独占timer_pool_的, 能保证其生命周期
int64_t TimerQueueHeapUniquePtrPmr::addTimer(TimerCb cb, base::TimeStamp when, double interval) {
    PmrTimerPtr t = makeTimer(std::move(cb), when, interval);
    int64_t seq = t->getSequence();
    HeapNode node{ when, seq, false, t->repeat(), interval, std::move(t) };
    pushNode(std::move(node));
    return seq;
}

void TimerQueueHeapUniquePtrPmr::cancel(int64_t sequence) {
    auto it = indexMap_.find(sequence);
    if(it == indexMap_.end()) return;
    heap_[it->second].canceled = true; // lazy cancellation
}

void TimerQueueHeapUniquePtrPmr::executeExpired(base::TimeStamp now) {
    while(!heap_.empty() && heap_[0].when < now) {
        HeapNode top = std::move(heap_[0]);
        popRoot();
        if(top.canceled) continue;
        top.timer->run();
        if(top.repeat && !top.canceled) {
            top.timer->restart(now);
            top.when = top.timer->expiration();
            top.canceled = false;
            pushNode(std::move(top));
        }
    }
}

void TimerQueueHeapUniquePtrPmr::handleRead(base::TimeStamp) {
    readTimerfd(timerfd_);
    executeExpired(base::TimeStamp::now());
}

void TimerQueueHeapUniquePtrPmr::processExpiredForTest(base::TimeStamp now) {
    executeExpired(now);
}

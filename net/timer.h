#pragma once

#include "../base/noncopyable.h"
#include "../base/TimeStamp.h"
#include "Callbacks.h"

#include <chrono>
#include <atomic>

// using TimerCb = std::function<void()>;

namespace mymoduo
{
namespace net
{
class Timer : noncopyable
{
public:
//默认无重复
//外部cb会被搬空?
    Timer(TimerCb cb, base::TimeStamp when, double interval = 0)
        : cb_(std::move(cb)),
          expiration_(when),
          interval_(interval),
          repeat_(interval > 0),
          sequence_(++n_created_)
    {}

    void run() const
    {
        cb_();
    }

    int64_t getSequence() const noexcept { return sequence_; }

    base::TimeStamp expiration() const noexcept { return expiration_; }

    bool repeat() const noexcept { return repeat_; }

    static int64_t n_created() noexcept { return n_created_.load(std::memory_order_relaxed); }

    void restart(base::TimeStamp now) noexcept;

    // DESIGN NOTES:
    // 1) If restart needed to change interval_ dynamically, current immutable interval_ member prevents that;
    //    consider storing std::chrono::microseconds mutable interval duration and allowing update.
    // 2) For high-frequency creation/destruction, using std::pmr::polymorphic_allocator<Timer> can reduce alloc cost.
    // 3) Alternative return type for factory pattern (not used here): std::expected<Timer,int> (C++23) for error code.

private:
    TimerCb cb_;
    base::TimeStamp expiration_;
    const double interval_;
    const bool repeat_;
    const int64_t sequence_;

    static std::atomic<int64_t> n_created_;

};
} // end net
} // end mymoduo
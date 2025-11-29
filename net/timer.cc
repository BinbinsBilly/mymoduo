#include "timer.h"

std::atomic<int64_t> mymoduo::net::Timer::n_created_{0};

void mymoduo::net::Timer::restart(base::TimeStamp now) noexcept
{
    if(repeat_)
    {
        expiration_ = now + interval_;
    }
    else
    {
        expiration_ = base::TimeStamp::invalid();
    }
}

// OPTIONAL ALTERNATIVE (commented out):
// Using chrono duration to avoid floating point rounding:
// void Timer::restart(TimeStamp now) noexcept {
//     if(repeat_) {
//         auto micros = static_cast<int64_t>(interval_ * TimeStamp::kMicroSecondsPerSecond);
//         expiration_ = TimeStamp(now.microsecondsSinceEpoch() + micros);
//     } else {
//         expiration_ = TimeStamp::invalid();
//     }
// }
#include "TimeStamp.h"

#include <algorithm>
#include <sstream>  //字符串流
#include <iomanip>  //put_time
#include <chrono>

using namespace mymoduo::base;

TimeStamp::TimeStamp()
    :microsecondsSinceEpoch_(0)
{}

TimeStamp::TimeStamp(int64_t microsecondsSinceEpoch)
    :microsecondsSinceEpoch_(microsecondsSinceEpoch)
{}

void TimeStamp::swap(TimeStamp& that)
{
    std::swap(microsecondsSinceEpoch_, that.microsecondsSinceEpoch_);
}

std::string TimeStamp::toFormatterString() const
{
    const char* format = "%Y-%m-%d %H:%M:%S";
    // microsecondsSinceEpoch_ 存储为微秒，需要转换为秒再交给localtime
    time_t ti = static_cast<time_t>(microsecondsSinceEpoch_ / kMicroSecondsPerSecond);
    std::tm tm = *std::localtime(&ti); //localtime签名:std::tm* std::localtime(const std::time_t* time);
    std::stringstream ss;
    ss << std::put_time(&tm, format);
    return ss.str();
}

bool TimeStamp:: valid() const
{
    return microsecondsSinceEpoch_ > 0;
}

int64_t TimeStamp::microsecondsSinceEpoch() const
{
    return microsecondsSinceEpoch_;
}

time_t TimeStamp::secondsSinceEpoch() const
{
    return static_cast<time_t>(microsecondsSinceEpoch_ / kMicroSecondsPerSecond);
}

TimeStamp TimeStamp:: now()
{
    auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    auto microsecondsincepoch = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch);
    return TimeStamp(microsecondsincepoch.count());
}

TimeStamp TimeStamp::invalid()
{
    return TimeStamp();
}

TimeStamp TimeStamp:: form_time_t(const time_t ti)
{
    return form_time_t(ti, 0);
}
TimeStamp TimeStamp:: form_time_t(const time_t ti, const int microseconds)
{
    return TimeStamp(static_cast<int64_t>(ti) * kMicroSecondsPerSecond + microseconds);//time_t以秒为单位
}


 TimeStamp TimeStamp::from_timeval(const struct timeval& tv)
{
    return TimeStamp(tv.tv_sec * kMicroSecondsPerSecond + tv.tv_usec);
}

template<typename Clock, typename Duration>
TimeStamp TimeStamp::from_time_point(const std::chrono::time_point<Clock, Duration>& tp)
{
    auto sincepoch = tp.time_since_epoch();
    auto microsinceepoch = std::chrono::duration_cast<std::chrono::microseconds>(sincepoch);
    return TimeStamp(microsinceepoch.count());
}


double mymoduo::base::TimeDifference(TimeStamp high, TimeStamp low)
{
    return (abs(high.microsecondsSinceEpoch() - low.microsecondsSinceEpoch())) / TimeStamp::kMicroSecondsPerSecond;
}
TimeStamp mymoduo::base::addTime(TimeStamp ts, double seconds)
{
    return TimeStamp(ts.microsecondsSinceEpoch() + seconds * TimeStamp::kMicroSecondsPerSecond);
}
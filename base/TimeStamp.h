#pragma once

#include "copyable.h"

#include <cstdint>
#include <chrono>
#include <string>
#include <compare>


namespace mymoduo
{
namespace base
{
    class TimeStamp : copyable
    {
        public:
            TimeStamp();
            explicit TimeStamp(int64_t microsecondsSinceEpoch);
            // explicit TimeStamp(time_t ti);
            // explicit TimeStamp(struct timeval tv);
            // template<typename Clock, typename Duration>
            // explicit TimeStamp(std::chrono::time_point<Clock, Duration> tp);
            
            void swap(TimeStamp& that);    //交换两个TimeStamp的时间
            std::string toFormatterString() const;
            bool valid() const;            //是否有效 >0
            int64_t microsecondsSinceEpoch() const;   //返回当前TimeStamp的时间us
            time_t secondsSinceEpoch() const;         //返回当前TimeStamp的时间 s   

            static TimeStamp now();    //返回当前时间的Timestamp
            static TimeStamp invalid();//返回一个无效的TimeStamp
            static TimeStamp form_time_t(const time_t ti);
            static TimeStamp form_time_t(const time_t ti, const int microseconds);
            static TimeStamp from_timeval(const struct timeval& tv);
            template<typename Clock, typename Duration>
            static TimeStamp from_time_point(const std::chrono::time_point<Clock, Duration>& tp);


            // === 比较操作符 ===
            bool operator==(const TimeStamp& other) const 
            { 
                return microsecondsSinceEpoch_ == other.microsecondsSinceEpoch_; 
            }
            
            bool operator<(const TimeStamp& other) const 
            { 
                return microsecondsSinceEpoch_ < other.microsecondsSinceEpoch_; 
            }

            bool operator>(const TimeStamp& other) const 
            { 
                return microsecondsSinceEpoch_ > other.microsecondsSinceEpoch_; 
            }

            // === 运算符 ===
            TimeStamp operator+(double adds) const
            {
                int64_t addus = static_cast<int64_t>(adds * kMicroSecondsPerSecond);
                return TimeStamp(microsecondsSinceEpoch() + addus);
            }

        static constexpr int kMicroSecondsPerSecond = 1000 * 1000;
        private:
            int64_t microsecondsSinceEpoch_;
    };

        double TimeDifference(TimeStamp high, TimeStamp low);
        TimeStamp addTime(TimeStamp timestamp, double seconds);
} // namespace base
} // namespace mymoduo
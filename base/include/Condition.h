//就当练习吧 还是不要封装condition了 过度封装不是好事  而且容易与std的封装在用法上冲突


// /**
//  * @file Condition.h
//  * @brief 条件变量封装，基于 std::condition_variable 实现
//  * @details 提供条件变量的等待和通知功能，配合互斥锁使用，实现线程间的同步。
//  * @author sbilly
//  * @date   2025-11-16
//  * @version 1.0 完整实现
//  */

// #pragma once

// #include <condition_variable>
// #include <mutex>
// #include "noncopyable.h"
// #include "MutexLock.hpp"

// namespace mymoduo
// {
// template<typename MutexType>
// class Condition : public noncopyable
// {
// public:
//     Condition(MutexType& mutex);
//     ~Condition() = default;

//     void wait();
//     void wait(MutexLockGuard& lock);
//     void notify() noexcept;
//     void notifyAll() noexcept;

// private:
//     MutexType& mutex_;
//     std::condition_variable cond_;
// };

// } //end mymoduo
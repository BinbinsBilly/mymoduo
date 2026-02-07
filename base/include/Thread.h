/**
 * @file Thread.h
 * @author sbilly
 * @brief 线程类封装 :封装线程的创建与启动, 提供线程ID获取和线程命名功能
 *        核心操作: 线程启动 start() 和 线程等待 join()
 *        辅助操作: 设置线程名  构造时获取线程需要执行的回调   获取线程id(通过std::future同步获取)
 */

#pragma once

#include "CountDownLatch.hpp"
#include "CurrentThread.h"
#include "noncopyable.h"
#include "Logging.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <cassert>
#include <future>
#include <sys/prctl.h>

namespace mymoduo
{
namespace base
{
class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>;

    Thread(ThreadFunc, const std::string& name = std::string());
    ~Thread();

    //移动语义
    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    void start();
    void join();
    bool started() const noexcept { return started_; }
    std::thread::id threadId() const noexcept { return threadId_; }
    const std::string& name() const noexcept { return name_; }

    static int numCreated() noexcept { return numCreated_.load(); }

private:
    void setDefaultName();

    bool started_;
    bool joined_;
    std::thread thread_;
    std::thread::id threadId_;
    ThreadFunc func_;
    std::string name_;

    static std::atomic<int> numCreated_;
};

} // namespace base
} // namespace mymoduo
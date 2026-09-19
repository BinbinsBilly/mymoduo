/**
 * @file EventLoopThread.h
 * @author sbilly
 * @brief 封装了一个线程和该线程内的EventLoop对象, one loop per thread思想
 *        核心操作: 提供线程执行的回调函数(执行EventLoop::loop); 线程启动startLoop(); 
 *        辅助操作: std::promise和std::future实现同步获取EventLoop对象
 */


#pragma once

#include "Thread.h"
#include "EventLoop.h"
#include "noncopyable.h"

#include <thread>
#include <future>
#include <memory>
#include <semaphore>

namespace mymoduo
{
namespace net
{
class EventLoopThread
{
public:
    using ThreadInitCb = std::function<void(EventLoop*)>;
    EventLoopThread(const ThreadInitCb&, const std::string &name = std:: string());
    ~EventLoopThread();
    // unmovable
    EventLoopThread(const EventLoopThread&&) = delete;
    EventLoopThread& operator=(const EventLoopThread&&) = delete; 
    
    EventLoop* startLoop();
    
private:
    void threadFunc();

    std::unique_ptr<EventLoop> loop_;  // 堆上分配，确保生命周期安全
    //这里使用std::promise和std::future实现同步 等待确保获取到EventLoop对象
    //这里也能使用std::latch实现功能, 那就和大神的moduo语义一致了
    //但是因为这里只要求一次通知 所以用promise更明确一点
    std::promise<EventLoop*> loopPromise_;
    //协调 EventLoop 的析构时机: loop 线程退出 loop() 后在此信号上等待,
    //直到 ~EventLoopThread 释放(此时先于本对象析构的 loop 使用方已全部完成)才销毁 EventLoop,
    //避免 quit 之后主线程(如先析构的 TcpServer)仍在访问 loop 时对象已被释放造成 use-after-free
    std::counting_semaphore<1> destructReleaseSem_{0};
    ThreadInitCb cb_;
    base::Thread thread_;

}; // class EventLoopThread
}  // namespace net
}  // namespace mymoduo
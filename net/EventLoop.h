/**
 * @file EventLoop.h
 * @author sbilly
 * @brief 事件循环类: 上层封装:调用Poll:poller进行事件循环分发; 
 *        核心操作: loop(事件循环); runInLoop(在loop线程中执行回调); queueInLoop(把回调放入队列等待loop线程执行); 
 *        定时器相关操作(runAt/After/Every/cancel); 
 *        channel管理(updateChannel/removeChannel/hasChannel)
 * */

#pragma once

#include "../base/TimeStamp.h"
#include "Channel.h"
#include "Poller.h"
#include "EpollPoller.h"
#include "../base/CurrentThread.h"
#include "Callbacks.h"
#include "TimerQueue.h"

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <vector>
#include <functional>
#include <cassert>
#include <sys/eventfd.h>
#include <string>
#include <unistd.h>
#include <utility>

// Minimal EventLoop abstract interface to allow Channel registration in tests.

namespace mymoduo {
namespace net {

long kernelTid(); //for debugs
class Channel;

class EventLoop {
public:
    using Functor = std::function<void()>;

    static EventLoop* getEventLoopofCurrentThread();
    
    EventLoop();
    ~EventLoop();
    void loop();
    void quit();

    bool isInLoopThread() const
    {
        std::thread::id currentThreadid = CurrentThread::threadId();
        return threadId_ == currentThreadid;
    }

    void assertInLoopThread() const
    {
        if(!isInLoopThread())
        {
            std::cout << "EventLoop::assertInLoopThread - EventLoop " << this
                      << " was created in threadId_ = " << threadId_
                      << ", current thread id = " << CurrentThread::threadId() << std::endl;

            // for debug
        //     std::cout << "[Assert] stored kernelTid=" << /* 同上但用保存时的 */
        //   " current kernelTid=" << kernelTid()
        //   << " stored hash=" << std::hash<std::thread::id>{}(threadId_)
        //   << " current hash=" << std::hash<std::thread::id>{}(CurrentThread::threadId()) << std::endl;
            assert(false);
        }
    }

    //int64_t -> sequence句柄
    //定时器功能
    int64_t runAt(base::TimeStamp time, TimerCb cb);
    int64_t runAfter(double delay, TimerCb cb);
    int64_t runEvery(double interval, TimerCb cb);
    void cancel(int64_t sequence);
    //定时器执行
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);
    
    //channel管理
    void updateChannel(Channel &);
    void removeChannel(Channel &);
    bool hasChannel(Channel&) const;

    int64_t iteration()
    {
        return iteration_;
    }

    // for test
    Poller* poller() const { return Poller_.get(); }
    // for test
    std::thread::id threadId() const { return threadId_; }

private:
    void readWakeup();   // consume eventfd
    void writeWakeup();  // signal eventfd
    void doPendingFunctor();
    size_t pendingFunctorSize() const;

    static constexpr int kPollTimeoutms = 1000;

    using ChannelList = std::vector<Channel*>;

    //注意构造顺序 如果存在依赖关系, 下层必须先构造
    //这里threadId_必须先构造, 因为会构造别的类时会触发断言
    const std::thread::id threadId_;
    std::atomic<bool> looping_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> eventHandeling_{false};
    std::atomic<bool> callingPendingFunc_{false};
    std::vector<Functor> pendingFunctors_;
    int64_t iteration_; //while的次数
    base::TimeStamp returnTime_;
    std::unique_ptr<Poller> Poller_;
    std::unique_ptr<TimerQueue> timerQueue_;
    int wakeupfd_;
    std::unique_ptr<Channel> wakeupChannel_;
    ChannelList activeChannel_;
    Channel* currentActiveChannel_;  //中间量不需要RAII守护 
    mutable std::shared_mutex mutex_;

    static thread_local EventLoop* t_LoopInThisThread;
};

} // namespace net
} // namespace mymoduo

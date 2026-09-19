/**
 * @file TimerQueue.h
 * @author sbilly
 * @brief 通过timerfd实现的定时器队列类: 管理多个定时器timer对象
 *        由上层EpollPoller调度
 *        核心操作: 添加定时器(addtimer) 取消定时器(cancel) 处理定时器触发(handleRead);
 *        辅助操作: 获取过期定时器(getExpiration) 更新定时器列表(updateTimerList) 插入定时器(insert2TimerList) 更新timerfd触发时间(updateTimerfd)
 *        timerqueue构造时就将timerChannel挂到epollpoller的监听树上了
 *        EventLoop类通过TimerQueue类实现定时器功能 addTimer/cancel接口
 *        还提供了向EventLoop线程中添加定时器的接口addTimerInLoop/cancelInLoop
 */

#pragma once

#include "timer.h"
#include "TimeStamp.h"
#include "noncopyable.h"
#include "Callbacks.h"
#include "Logging.h"
namespace mymoduo { namespace net { class EventLoop; class Channel; } }

#include <set>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>

#include <sys/timerfd.h>
#include <unistd.h>

namespace mymoduo
{
namespace net
{
class TimerQueue : noncopyable
{
public:
    TimerQueue(EventLoop*);
    ~TimerQueue();

    TimerQueue(TimerQueue&&) = delete;
    TimerQueue& operator=(TimerQueue&& ohter) = delete;

    // 添加定时器：返回其 sequence 作为句柄
    int64_t addTimer(TimerCb cb, base::TimeStamp when, double interval);
    // 取消定时器：使用 sequence 句柄
    void cancel(int64_t sequence);
private:
    // 增删Timer时 要更新 timerfd 的触发时间
    void updateTimerfd();

    //在loop中实际调用的处理逻辑
    void addTimerInLoop(std::shared_ptr<Timer> newtimer);
    void cancelInLoop(int64_t sequence);
    
    // timer内部的timerfd 读事件
    void handleRead();
    //获取所有过期的timer
    std::vector<std::shared_ptr<Timer>> getExpiration(base::TimeStamp now); 
    //根据是否是可重复timer 重置可期timer的装填
    void updateTimerList(std::vector<std::shared_ptr<Timer>> &&expiration, base::TimeStamp now);
    // 插入到timerlist_ 需要更新timerfd
    bool insert2TimerList(std::shared_ptr<Timer> newTimer);

    struct TimerComp
    {
        //启用异构查找
        using is_transparent = void;
        //shared_ptr 之间查找
        bool operator()(const std::shared_ptr<Timer> &lhs,const std::shared_ptr<Timer> &rhs) const
        {
            //需要严格比较出大小
            if(!(lhs->expiration() == rhs->expiration()))
            {
                return lhs->expiration() < rhs->expiration();
            }
            return lhs.get() < rhs.get();
        }
        //用Timer查找
        bool operator()(const std::shared_ptr<Timer> &lhs, const Timer *rhs) const 
        {   
            if(!(lhs->expiration() == rhs->expiration()))
            {
                return lhs->expiration() < rhs->expiration();
            }
            return lhs.get() < rhs; 
        }
        bool operator()(const std::shared_ptr<Timer> &lhs, const Timer &rhs) const
        {   
            if(!(lhs->expiration() == rhs.expiration()))
            {
                return lhs->expiration() < rhs.expiration();
            }
            return lhs.get() < &rhs; 
        }

        bool operator()(const Timer& lhs, const std::shared_ptr<Timer> &rhs) const
        {
            if(!(lhs.expiration() == rhs->expiration()))
            {
                return lhs.expiration() < rhs->expiration();
            }
            return &lhs < rhs.get(); 
        }

        bool operator()(const Timer* lhs, const std::shared_ptr<Timer> &rhs) const
        {
            if(!(lhs->expiration() == rhs->expiration()))
            {
                return lhs->expiration() < rhs->expiration();
            }
            return lhs < rhs.get(); 
        }
        // 使用TimeStamp进行查找
        bool operator()(const std::shared_ptr<Timer> &lhs, const base::TimeStamp &rhs) const 
        {   
            return lhs->expiration() < rhs;
        }

        bool operator()(const base::TimeStamp &lhs, const std::shared_ptr<Timer> &rhs) const
        {
            return lhs < rhs->expiration();
        } 
    };

    // 主存储定时器 (按过期时间排序)
    using TimerList = std::set<std::shared_ptr<Timer>, TimerComp>;
    // sequence 到 Timer 的索引（shared_ptr 共享所有权），O(1) 通过 sequence 找定时器
    using SequenceIndex = std::unordered_map<int64_t, std::shared_ptr<Timer>>;
    // 延迟取消集合：在回调执行期间存储被取消的定时器 sequence，避免悬垂指针
    using CancelList = std::set<int64_t>;
    bool callingExpired_;

    EventLoop* loop_;
    const int timerfd_;
    std::unique_ptr<Channel> timerfdChannel_;
    TimerList timerlist_;
    SequenceIndex sequenceIndex_;
    CancelList cancelList_;

};// end TimerQueue
} // end net
}// end mymoduo
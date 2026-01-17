/**
 * @file Channel.h
 * @author sbilly
 * @brief Channel类: 封装文件描述符fd 以及fd上感兴趣的事件event_ 和发生的事件revent_, 并且封装了相应事件的回调操作
 *        Channel属于某个EpollPoller, 负责fd上事件的分发和回调调用
 *        核心操作: 设置回调; 设置感兴趣的事件; 处理发生的事件(回调); 更新事件(加入红黑树或从中摘除); 移除(删除)channel
 */

#pragma once

#include "noncopyable.h"
#include "TimeStamp.h"

#include <cassert>
#include <functional>
#include <memory>
#include <sys/poll.h>  //POLLNVAL ...

namespace mymoduo
{
namespace net
{
class EventLoop; // forward declaration inside correct namespace
enum class ChannelStatus;
enum class Event: uint32_t
    {
        None = 0,
        Read = POLLIN | POLLPRI,
        Write = POLLOUT
    };

class Channel : noncopyable
{
public:

    using ReadEventCb = std::function<void(mymoduo::base::TimeStamp)>;
    using WriteEventCb = std::function<void()>;
    using ErrorEventCb = std::function<void()>;
    using CloseEventCb = std::function<void()>;

    explicit Channel(EventLoop* loop, int fd);
    Channel(Channel&& other) = delete;
    Channel& operator=(Channel&& other) = delete;
    ~Channel();

    template<typename T>
    void setReadEventCb(T &&cb) { rCb_ = std::forward<T>(cb); }
    template<typename T>
    void setWriteEventCb(T &&cb) { wCb_ = std::forward<T>(cb); }
    template<typename T>
    void setErrorEventCb(T &&cb) { eCb_ = std::forward<T>(cb); }
    template<typename T> 
    void setCloseEventCb(T &&cb) { cCb_ = std::forward<T>(cb); }
    
    int fd() const noexcept { return fd_; }
    uint32_t events() const noexcept { return event_; }
    int status() const noexcept { return status_; }

    void setrevents(Event event) noexcept { revent_ = static_cast<uint32_t>(event); }
    void setrevents(int event) noexcept { revent_ = event; }
    void setstatus(ChannelStatus status) noexcept { status_ = static_cast<int>(status); }

    bool isNoneEvent() const noexcept { return event_ == static_cast<uint32_t>(Event::None); }

    void enableRead() { addEvent(Event::Read); }
    void enableWrite() { addEvent(Event::Write); } 
    void disableRead() { removeEvent(Event::Read); }
    void disableWrite() { removeEvent(Event::Write); }
    void disableAll() { event_ = static_cast<uint32_t>(Event::None); update(); }
    
    bool isReading() const noexcept { return (event_ & kReadEvent) != 0; }
    bool isWriting() const noexcept { return (event_ & kWriteEvent) != 0; }

    void tie(const std::shared_ptr<void> &obj);
    void handleEvent(mymoduo::base::TimeStamp recvTime);

    EventLoop* ownerLoop() const { return loop_; }

    void remove();

private:
    void addEvent(Event event);
    void removeEvent(Event event);
    void update();
    void handleEventWithGuard(mymoduo::base::TimeStamp receiveTime);

    bool isInLoopThread() const;

    //constexpr 编译器常量
    static constexpr uint32_t kNoneEvent = static_cast<uint32_t>(Event::None);
    static constexpr uint32_t kReadEvent = static_cast<uint32_t>(Event::Read);
    static constexpr uint32_t kWriteEvent = static_cast<uint32_t>(Event::Write);

    EventLoop* loop_;
    const int fd_;
    uint32_t event_;    //for register event
    uint32_t revent_;   //events received from poll;
    int status_;    //标记channel状态的 new; added;deleted;

    std::weak_ptr<void> tie_;    //for lifetime safety
    bool tied_;
    bool eventHandeling_;
    bool addedToLoop_;
    
    ReadEventCb rCb_;
    WriteEventCb wCb_;
    ErrorEventCb eCb_;
    CloseEventCb cCb_;

}; //end Channel


} //end net
} //end mymoduo

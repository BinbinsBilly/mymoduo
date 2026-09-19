#include "Channel.h"
#include "EventLoop.h"

namespace mymoduo
{
namespace net
{
    //for link
    constexpr uint32_t Channel::kNoneEvent;
    constexpr uint32_t Channel::kReadEvent;
    constexpr uint32_t Channel::kWriteEvent;

    Channel::Channel(EventLoop* loop, int fd)
        :loop_(loop),
         fd_(fd),
         event_(static_cast<uint32_t>(Event::None)),
         revent_(static_cast<uint32_t>(Event::None)),
         status_(-1),  //-1 : new
         tied_(false),
         eventHandeling_(false),
         addedToLoop_(false)
         {
            static_assert(sizeof(Event) == sizeof(uint32_t), "Event lenth mismatch of uint32_t");
            assert(loop_ != nullptr && "loop can not be null");
         }

    Channel::~Channel()
    {
        assert(!eventHandeling_);
        if(addedToLoop_)
        {
            remove();
        }
        assert(!addedToLoop_);
    }

    void Channel::tie(const std::shared_ptr<void> &obj) 
    {
        tie_ = obj;
        tied_ = true;
    }

    void Channel::handleEvent(mymoduo::base::TimeStamp recvTime)
    {
        std::shared_ptr<void> guard;
        if(tied_)
        {
            guard = tie_.lock();
            if(guard)
            {
                handleEventWithGuard(recvTime);
            }
        }
        else
        {
            handleEventWithGuard(recvTime);
        }
    }

    void Channel::handleEventWithGuard(mymoduo::base::TimeStamp receiveTime)
    {
        if(revent_ & POLLNVAL) [[unlikely]]
        {
            // invalid poll fd; optionally log
        }
        eventHandeling_ = true;
        //使用guard 析构时更新eventHandeling_的状态
        struct EventGuard { bool &flag; explicit EventGuard(bool &f): flag(f) {} ~EventGuard(){ flag = false; } } guard(eventHandeling_);
        // Error handling first
        if(revent_ & (POLLNVAL | POLLERR))
        {
            if(eCb_) { eCb_(); }
        }
        // peer closed (POLLHUP)
        // 仅当 HUP 且不可读时才走 close 路径，可读时由 read()==0 触发 handleClose，避免双重关闭
        if((revent_ & POLLHUP) && !(revent_ & (POLLIN | POLLPRI)))
        {
            if(cCb_) { cCb_(); }
        }
        // read events
        if(revent_ & (POLLIN | POLLPRI | POLLRDHUP))
        {
            if(rCb_) { rCb_(receiveTime); }
        }
        // write event
        if(revent_ & POLLOUT)
        {
            if(wCb_) { wCb_(); }
        }
    }

    void Channel::addEvent(Event ev)
    {
        const uint32_t oldEvent = event_;
        event_ |= static_cast<uint32_t>(ev);
        if(event_ != oldEvent)
        {
            update();
        }
    }
    void Channel::removeEvent(Event ev)
    {
        const uint32_t oldEvent = event_;
        event_ &= ~static_cast<uint32_t>(ev);
        if(event_ != oldEvent)
        {
            update();
        }
    }

    void Channel::update()
    {
        addedToLoop_ = true;
        loop_->updateChannel(*this);
    }

    void Channel::remove()
    {
        assert(isInLoopThread());
        assert(isNoneEvent());
        addedToLoop_ = false;
        loop_->removeChannel(*this);
    }

    bool Channel::isInLoopThread() const
    {
        return loop_->isInLoopThread();
    }
}
}
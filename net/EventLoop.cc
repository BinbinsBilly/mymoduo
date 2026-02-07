#include "EventLoop.h"
#include "Logging.h"
#include <sys/syscall.h>
#include <unistd.h>

namespace
{
    int createWakeupfd()
    {
        int wakeupfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if(wakeupfd < 0)
        {
            LOG_ERROR << "create eventfd\n";
            return -1;
        }
        return wakeupfd;
    }
} //

namespace mymoduo
{
namespace net
{
//for debug
// long kernelTid() { return syscall(SYS_gettid); }

    thread_local EventLoop* EventLoop::t_LoopInThisThread = nullptr;
    EventLoop* EventLoop::getEventLoopofCurrentThread()
    {
        return t_LoopInThisThread;
    }

    EventLoop::EventLoop()
        :threadId_(CurrentThread::threadId()),
         iteration_(0),
         Poller_(Poller::newDefaultPoller(this)),
         timerQueue_(std::make_unique<TimerQueue>(this)), //!!!这里触发assertInLoop()断言了!!!要修改构造顺序
         wakeupfd_(createWakeupfd()),
         wakeupChannel_(std::make_unique<Channel>(this, wakeupfd_)),
         currentActiveChannel_(nullptr)
    {
        LOG_DEBUG << "EventLoop created" << "in" << "[thread]"<< CurrentThread::tidString();
        //for test
        // std::cout << "EventLoop::EventLoop() - constructing EventLoop " << this
        //           << " in thread " << threadId_ << std::endl;
        if(t_LoopInThisThread != nullptr)
        {
            LOG_ERROR << "thisThread is already owned";
        }
        else
        {
            t_LoopInThisThread = this;
        }
        // 开启 wakeupChannel 读事件 设置都事件回调
        wakeupChannel_->setReadEventCb([this](base::TimeStamp){ this->readWakeup(); });
        wakeupChannel_->enableRead();
        // for debug
        // std::cout << "[Construct] kernelTid=" << kernelTid()
        //   << " std::thread::id=" << threadId_
        //   << " hash=" << std::hash<std::thread::id>{}(threadId_) << std::endl;
    }

    EventLoop::~EventLoop()
    {
        LOG_DEBUG << "EventLoop::~EventLoop - EventLoop destructed in thread " << CurrentThread::tidString();
        if(!wakeupChannel_->isNoneEvent())
        {
            // LOG_DEBUG << "EventLoop::~EventLoop - wakeupChannel is NoneEvent, no need to remove";
            wakeupChannel_->disableAll();
        }
        wakeupChannel_->remove();
        LOG_DEBUG << "EventLoop::~EventLoop - wakeupChannel removed";
        ::close(wakeupfd_);
        t_LoopInThisThread = nullptr;
        LOG_DEBUG << "EventLoop::~EventLoop - EventLoop destructed done";
    }

    void EventLoop::loop() 
    {
        assert(!looping_);
        assertInLoopThread();

        looping_ = true;
        quit_ = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
        LOG_INFO << "EventLoop" << this << "start loop";
#pragma GCC diagnostic pop
        while(!quit_)
        {
            iteration_++;
            activeChannel_.clear();  //先清空
            const base::TimeStamp returnTime = Poller_->Poll(kPollTimeoutms, activeChannel_);
            eventHandeling_ = true;            
            for(Channel* channel : activeChannel_)
            {
                currentActiveChannel_ = channel;
                currentActiveChannel_ -> handleEvent(returnTime);   //根据事件类型调用回调
            }

            currentActiveChannel_ = nullptr;
            eventHandeling_ = false;

            doPendingFunctor();
        }

        looping_ = false;
        //for debug  exit successfully
        // std::cout<< "========================="<<std::endl;
        // std::cout<< "EventLoop::loop() - quit loop" << std::endl;
        // 取消this == null 报错检查 
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
        LOG_INFO << "EventLoop" << this << "stop loop";
#pragma GCC diagnostic pop
    }   

    void EventLoop::quit()
    {
        quit_ = true;
        if(!isInLoopThread())
        {
            writeWakeup(); // 跨线程退出需要唤醒
        }
    }

    int64_t EventLoop::runAt(base::TimeStamp time, TimerCb cb)
    {
        return timerQueue_->addTimer(std::move(cb), time, 0);
    }
    
    int64_t EventLoop::runAfter(double delay, TimerCb cb)
    {
        base::TimeStamp time = addTime(base::TimeStamp::now(), delay);
        return timerQueue_->addTimer(std::move(cb), time, 0);
    }
    
    //interval(s)
    int64_t EventLoop::runEvery(double interval, TimerCb cb)
    {
        // 第一次触发在 interval 之后
        base::TimeStamp first = addTime(base::TimeStamp::now(), interval);
        return timerQueue_->addTimer(std::move(cb), first, interval);
    }

    void EventLoop::cancel(int64_t sequence)
    {
        timerQueue_->cancel(sequence);
    }

    //“Run this callback in the loop thread, no matter who calls me.”
    void EventLoop::runInLoop(Functor cb)
    {
        // LOG_DEBUG << "EventLoop::runInLoop - cb address: " << (void*)&cb;
        //是在当前线程中调用就直接执行
        //同步调用
        if(isInLoopThread())
        {
            // LOG_DEBUG << "EventLoop::runInLoop - in loop thread, execute directly";
            cb();
            // LOG_DEBUG<< "EventLoop::runInLoop -  after cb() execute ";
        }
        else//加到pending functor中
        {
            // LOG_DEBUG<< "EventLoop::runInLoop - not in loop thread, queue in loop";
            queueInLoop(std::move(cb));
        }
        // LOG_DEBUG << "exit EventLoop::runInLoop";
    }

    void EventLoop::queueInLoop(Functor cb)
    {
        {
            std::unique_lock<std::shared_mutex> guard(mutex_);
            pendingFunctors_.push_back(std::move(cb));
        }
        if(!isInLoopThread() || callingPendingFunc_)
        {
            writeWakeup(); // 写 eventfd 触发唤醒 通过唤醒来执行pendingFunctors_
        }
    }

    size_t EventLoop::pendingFunctorSize()const
    {
        {
            std::shared_lock<std::shared_mutex> guard(mutex_);
            return pendingFunctors_.size();
        }
    }

    void EventLoop::updateChannel(Channel& channel)
    {
        assert(channel.ownerLoop() == this);
        assertInLoopThread();
        Poller_->updateChannel(channel);
    }

    void EventLoop::removeChannel(Channel& channel)
    {
        assert(channel.ownerLoop() == this);
        assertInLoopThread();
        Poller_->removeChannel(channel);
    }

    bool EventLoop::hasChannel(Channel& channel) const
    {
        assert(channel.ownerLoop() == this);
        assertInLoopThread();
        return Poller_->hasChannel(channel);
    }

    void EventLoop::readWakeup()
    {
        uint64_t one = 0;
        ssize_t n = ::read(wakeupfd_, &one, sizeof(one));
        if(n != sizeof(one))
        {
            LOG_ERROR << "readWakeup failed\n";
        }
    }

    void EventLoop::writeWakeup()
    {
        uint64_t one = 1;
        ssize_t n = ::write(wakeupfd_, &one, sizeof(one));
        if(n != sizeof(one))
        {
            LOG_ERROR << "writeWakeup failed\n";
        }
    }

    void EventLoop::doPendingFunctor()
    {
        LOG_DEBUG << "EventLoop::doPendingFunctor - start";
        assertInLoopThread();
        LOG_DEBUG << "EventLoop::doPendingFunctor - after assertInLoopThread";
        std::vector<Functor> p_functors;
        callingPendingFunc_ = true;
        //先交换 避免长时间持有锁
        {
            std::unique_lock<std::shared_mutex> guard(mutex_); // 修改需要独占锁
            std::swap(pendingFunctors_, p_functors);
        }
        for(auto &func : p_functors)
        {
            func();
        }
        callingPendingFunc_ = false;
    }

} // end net

} // end mymoduo
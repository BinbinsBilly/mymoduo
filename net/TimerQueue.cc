#include "TimerQueue.h"
#include "EventLoop.h"
#include "Channel.h"

namespace mymoduo
{
namespace net
{
namespace detail
{
    //创建timerfd
    int create_timefd()
    {
        int timefd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if(timefd < 0)
        {
            LOG_ERROR << "create timefd";
        }
        return timefd;
    }

    void updateTimerfd(int timerfd, base::TimeStamp newtime)
    {
        // 使用相对时间 (CLOCK_MONOTONIC) 避免与 TimeStamp(system_clock) 绝对值基准不一致导致永不触发
        int64_t diffUs = newtime.microsecondsSinceEpoch() - base::TimeStamp::now().microsecondsSinceEpoch();
        if(diffUs < 0) diffUs = 0;
        struct itimerspec newits;
        memset(&newits, 0, sizeof(newits));
        newits.it_value.tv_sec  = diffUs / base::TimeStamp::kMicroSecondsPerSecond;
        newits.it_value.tv_nsec = (diffUs % base::TimeStamp::kMicroSecondsPerSecond) * 1000;
        if(::timerfd_settime(timerfd, 0, &newits, NULL) < 0)
        {
            LOG_ERROR << "timerfd_settime relative failed\n";
        }
    }

    void readtimerfd(int timerfd)
    {
        int64_t expiration;
        size_t reads = ::read(timerfd, &expiration, sizeof(expiration));
        if(reads != sizeof(expiration))
        {
            if(!(errno == EAGAIN || errno == EWOULDBLOCK))
            {
                LOG_ERROR << "readtimerfd \n";
            }
        }
    }

} //end detail

    TimerQueue::TimerQueue(EventLoop* loop)
        :callingExpired_(false),
         loop_(loop),
         timerfd_(detail::create_timefd()),
         timerfdChannel_(std::make_unique<Channel>(loop, timerfd_))
    {
        timerfdChannel_->setReadEventCb([this](base::TimeStamp){ this->handleRead(); });
        //enableRead()中会断言. 而在EventLoop中构造TimerQueue时,EventLoop尚未被完全构造, 导致断言失败 
        //所以EventLoop中构造时必须让threadId_先构造
        timerfdChannel_->enableRead(); //在这里就被挂在poller的监听树上了
    }

    TimerQueue::~TimerQueue()
    {
        timerfdChannel_->disableAll();
        timerfdChannel_->remove();
        LOG_DEBUG << "TimerQueue channel removed";
        ::close(timerfd_);
    }

    std::vector<std::shared_ptr<Timer>> TimerQueue::getExpiration(base::TimeStamp now)
    {
        std::vector<std::shared_ptr<Timer>> expired;
        auto end = timerlist_.lower_bound(now);
        for(auto it = timerlist_.begin(); it != end; )
        {
            // 先移出 timer 再删除索引（extract 后迭代器失效）
            auto node = timerlist_.extract(it++);
            sequenceIndex_.erase(node.value()->getSequence());
            expired.push_back(std::move(node.value()));
        }
        return expired;
    }

    void TimerQueue::addTimerInLoop(std::shared_ptr<Timer> newTimer)
    {
        insert2TimerList(std::move(newTimer));
    }

    void TimerQueue::cancelInLoop(int64_t sequence)
    {
        auto mapIt = sequenceIndex_.find(sequence);
        if(mapIt == sequenceIndex_.end())
        {
            if(callingExpired_)
            {
                cancelList_.insert(sequence); // 正在执行的定时器: 延迟取消, 阻止其重启
            }
            return; // 无此定时器
        }
        Timer* raw = mapIt->second.get();
        auto setIt = timerlist_.find(raw); // 异构查找
        if(setIt == timerlist_.end()) { sequenceIndex_.erase(mapIt); return; }
        if(callingExpired_)
        {
            cancelList_.insert(sequence); // 延迟取消（以 sequence 标记更安全）
            return;
        }
        bool earliest = (setIt == timerlist_.begin());
        timerlist_.erase(setIt);
        sequenceIndex_.erase(mapIt);
        if(earliest) updateTimerfd();
    }

    void TimerQueue::updateTimerfd()
    {
        if(timerlist_.empty())
        {
            struct itimerspec its;
            ::memset(&its, 0, sizeof(its));
            //禁用
            its.it_value.tv_sec = 0;
            its.it_value.tv_nsec = 0;
            
            ::timerfd_settime(timerfd_, 0, &its, nullptr);
        }
        else
        {
            //下一个
            detail::updateTimerfd(timerfd_, (*timerlist_.begin())->expiration());
        }
    }


    void TimerQueue::handleRead()
    {
        //assert
        base::TimeStamp now = base::TimeStamp::now();

        detail::readtimerfd(timerfd_);

        auto expired = getExpiration(now);

        callingExpired_ = true;
        //it == std::shared_ptr<Timer>
        for(auto &t : expired)
        {
            if(cancelList_.find(t->getSequence()) == cancelList_.end())
            {
                t->run();
            }
        }
        callingExpired_ = false;
        updateTimerList(std::move(expired), now);
    }

    void TimerQueue::updateTimerList(std::vector<std::shared_ptr<Timer>> &&expired, base::TimeStamp now)
    {
        for(auto &t : expired)
        {
            bool wasCanceled = false;
            if(cancelList_.find(t->getSequence()) != cancelList_.end())
            {
                wasCanceled = true;
                cancelList_.erase(t->getSequence()); // 清理取消集合
            }
            if(t->repeat() && !wasCanceled)
            {
                t->restart(now);
                auto sp = t; // 拷贝一份入 set
                auto [pos, inserted] = timerlist_.insert(sp);
                if(inserted) sequenceIndex_[sp->getSequence()] = sp;
            }
        }
        updateTimerfd();
    }

    bool TimerQueue::insert2TimerList(std::shared_ptr<Timer> newTimer)
    {
        Timer* raw = newTimer.get();
        auto [iter, inserted] = timerlist_.insert(newTimer); // 拷贝入 set
        if(inserted)
        {
            sequenceIndex_[raw->getSequence()] = newTimer;
            if(timerlist_.size() == 1 || (*iter)->expiration() < (*timerlist_.begin())->expiration())
            {
                detail::updateTimerfd(timerfd_, (*iter)->expiration());
            }
        }
        return inserted;
    }

    int64_t TimerQueue::addTimer(TimerCb cb, base::TimeStamp when, double interval) {
        // shared_ptr 可拷贝, 能安全捕获进 std::function; functor 被丢弃时 timer 随引用计数归零自动释放
        auto timer = std::make_shared<Timer>(std::move(cb), when, interval);
        int64_t seq = timer->getSequence();
        //在loop线程中添加定时器
        loop_->runInLoop([this, timer](){ addTimerInLoop(timer); });
        return seq;
    };

    void TimerQueue::cancel(int64_t sequence)
    {
        loop_->runInLoop([this, sequence]()
        {
            this->cancelInLoop(sequence);
        });
    }

} //end net

} //end mumoduo

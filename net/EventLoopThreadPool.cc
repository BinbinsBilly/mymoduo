#include "EventLoopThreadPool.h"

namespace mymoduo
{
namespace net
{

class EventLoopThreadPool::Impl
{
public:
    Impl(EventLoop* baseloop, const std::string& name)
        :baseLoop_(baseloop),
            name_(name),
            started_(false),
            next_(0),
            threadNum_(0)
    {}
    ~Impl() = default;

    void setThreadNum(int num) noexcept
    {
        threadNum_ = num;
    }

    bool start(const threadInitCb& cb)
    {
        LOG_DEBUG << "EventLoopThreadPool::start() - starting ThreadPool " << name_;
        if(started_)
        {
            LOG_INFO << "EventLoopThreadPool::start() - already started";
            return false;   
        }
        if(threadNum_)
        {
            started_ = true;

            threads_.reserve(threadNum_);
            loops_.reserve(threadNum_);
            for(int i = 0; i < threadNum_; ++i)
            {
                std::string threadName = name_ + std::to_string(i);
                // 先获取原始指针, 再移动 unique_ptr 避免使用空指针
                auto threadObj = std::make_unique<EventLoopThread>(cb, threadName);
                EventLoopThread* raw = threadObj.get();
                threads_.emplace_back(std::move(threadObj));
                EventLoop* loop = raw->startLoop();
                if(loop == nullptr)
                {
                    threads_.clear();
                    loops_.clear();
                    LOG_ERROR << "EventLoopThreadPool::start() - failed to start EventLoopThread";
                    return false;
                }
                loops_.emplace_back(std::move(loop));
            }
            assert(static_cast<int>(loops_.size()) == threadNum_);
        }else{
            if(cb)
            {
                cb(baseLoop_);
            }
        }
        LOG_DEBUG << "EventLoopThreadPool::start() exit successfully " << name_;
        return true;
    }

    EventLoop* getNextLoop()
    {
        EventLoop* loop = baseLoop_;
        if(!loops_.empty())
        {
            loop = loops_[next_];
            ++next_;
            if(static_cast<size_t>(next_) >= loops_.size())
            {
                next_ = 0;
            }
        }
        return loop;
    }

    EventLoop* getLoopForHash(size_t hashCode) const
    {
        EventLoop* loop = baseLoop_;
        if(!loops_.empty())
        {
            loop = loops_[hashCode % loops_.size()];
        }
        return loop;
    }

    std::vector<EventLoop*> getAllLoop() const
    {
        if(loops_.empty())
        {
            return {baseLoop_};
        }
        else
        {
            return loops_;
        }
    }

    bool started() const { return started_; }
    std::string_view name() const { return name_; }

private:
    //EventLoop 是栈上对象
    EventLoop* baseLoop_;
    std::string name_;
    bool started_;
    int next_;
    int threadNum_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    //EventLoop 是栈上对象
    std::vector<EventLoop*> loops_;
}; // class EventLoopThreadPool::Impl


    EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseloop, const std::string& name)
        : impl_(std::make_unique<Impl>(baseloop, name))
    {}

    EventLoopThreadPool::~EventLoopThreadPool() = default;

    bool EventLoopThreadPool::start(const threadInitCb cb)
    {
        return impl_->start(std::move(cb));
    }

    void EventLoopThreadPool::setThreadNum(int num) noexcept
    {
        impl_->setThreadNum(num);
    }

    EventLoop* EventLoopThreadPool::getNextLoop()
    {
        return impl_->getNextLoop();
    }

    EventLoop* EventLoopThreadPool::getLoopForHash(size_t hashCode) const
    {
        return impl_->getLoopForHash(hashCode);
    }

    std::vector<EventLoop*> EventLoopThreadPool::getAllLoop() const
    {
        return impl_->getAllLoop();
    }

    bool EventLoopThreadPool::started() const { return impl_->started(); }
    
    std::string_view EventLoopThreadPool::name() const { return impl_->name(); }

   
} // namespace net
} // namespace mymoduo
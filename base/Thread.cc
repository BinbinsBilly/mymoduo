#include "Thread.h"
#include <exception>

namespace mymoduo
{
namespace detail
{
    
} // namespace detail
namespace base
{
    //静态成员变量初始化
    std::atomic<int> Thread::numCreated_{0};

    Thread::Thread(ThreadFunc func, const std::string& name)
        : started_(false),
          joined_(false),
          thread_(),
          threadId_(std::thread::id{}),
          func_(std::move(func)),
          name_(name)
    {
        setDefaultName();   
    }
    Thread::~Thread()
    {
        if(thread_.joinable() && !joined_)
        {
            thread_.detach();
        }
    }

    Thread::Thread(Thread&& other) noexcept
        : started_(other.started_),
          joined_(other.joined_),
          thread_(std::move(other.thread_)),
          threadId_(other.threadId_),
          func_(std::move(other.func_)),
          name_(std::move(other.name_))
    {
        other.started_ = false;
        other.joined_ = false;
        other.threadId_ = std::thread::id{};
    }

    Thread& Thread::operator=(Thread&& other) noexcept
    {
        // 自我赋值检查
        if(this != &other)
        {
            if(thread_.joinable() && !joined_)
            {
                thread_.detach();
            }
            started_ = other.started_;
            joined_ = other.joined_;
            thread_ = std::move(other.thread_);
            threadId_ = other.threadId_;
            func_ = std::move(other.func_);
            name_ = std::move(other.name_);

            other.started_ = false;
            other.joined_ = false;
            other.threadId_ = std::thread::id{};
        }
        return *this;
    }

    void Thread::setDefaultName()
    {
        int num = ++numCreated_;
        if(name_.empty())
        {
            name_ = "Thread" + std::to_string(num);
        }
    }

    void Thread::start()
    {
        assert(!started_);
        started_ = true;

        std::promise<std::thread::id> threadIdPromise;
        std::future<std::thread::id> threadIdFuture = threadIdPromise.get_future();

        // 将 functor 与线程名移入线程自身所有权：
        // 若 Thread 对象析构时未 join（detach），线程仍可能正在执行 func_()，
        // 捕获 this 会导致对已析构成员的悬空访问。
        // promise 通过 future.get() 同步，set_value 先于其析构，按引用捕获安全。
        ThreadFunc func = std::move(func_);
        std::string name = name_.empty() ? std::string("mymoduoThread") : name_;

        thread_ = std::thread([&threadIdPromise, func = std::move(func), name = std::move(name)]() mutable
        {
            //获取线程ID // 获取值后设置promise状态为已满足 外部停止阻塞等待
            threadIdPromise.set_value(CurrentThread::threadId());
            //设置线程名
            CurrentThread::setName(name);
            ::prctl(PR_SET_NAME, CurrentThread::name());
            try
            {
                //执行用户回调
                func();
                CurrentThread::setName("finished");
            }
            catch(const std::exception& ex)
            {
                CurrentThread::setName("crashed");
                LOG_ERROR << "Thread crashed, exception: " << ex.what();
            }
            catch(...)
            {
                CurrentThread::setName("crashed");
                LOG_ERROR << "Thread crashed, unknown exception";
            }
        });
        //这里还可以使用std::latch实现线程ID的同步获取
        threadId_ = threadIdFuture.get();
        assert(threadId_ != std::thread::id{});
    }

    void Thread::join()
    {
        assert(thread_.joinable());
        joined_ = true;
        started_ = false;
        thread_.join();
    }

} // namespace base
} // namespace mymoduo
#include "EventLoopThread.h"

namespace mymoduo
{
namespace net
{
    EventLoopThread::EventLoopThread(const ThreadInitCb& cb, const std::string& name)
        :loop_(nullptr),
         cb_(cb),
         thread_(std::bind(&EventLoopThread::threadFunc, this), name)
    {}
    EventLoopThread::~EventLoopThread()
    {
        // 未 startLoop 的线程: thread_ 从未启动, join 会触发断言/terminate, 直接跳过
        if (!thread_.started()) {
            return;
        }
        if (loop_) {
            loop_->quit();   // 退出事件循环
        }
        //释放信号: 允许 loop 线程销毁 EventLoop;
        //此时先于本对象析构的 loop 使用方(如先析构的 TcpServer)已全部完成
        destructReleaseSem_.release();
        thread_.join();    // 等待线程结束 !!! 这个很重要, 会阻塞等待loop线程退出
        // join 返回时 loop_ 已在 loop 线程内 reset, 此处不可再触碰
    }

    // EventLoop线程函数
    void EventLoopThread::threadFunc()
    {
        loop_ = std::make_unique<EventLoop>(); // 堆上分配，确保生命周期安全
        loopPromise_.set_value(loop_.get());
        if(cb_)  //如果设置了回调 在循环前调用
        {
            cb_(loop_.get());
        }

        LOG_DEBUG << "EventLoopThread" << "[Name]: "<< thread_.name() << "- EventLoop thread started in thread "
                  << CurrentThread::tidString();
        //for test
        // std::thread::id tid = CurrentThread::threadId();
        // std::cout<< "EventLoopThread::threadFunc() - EventLoop thread started in thread "
        //          << tid << std::endl;

        loop_->loop(); // 事件循环 一去不返
        LOG_DEBUG << "EventLoopThread" << "[Name]: "<< thread_.name() << "- EventLoop thread exit in thread "
                  << CurrentThread::tidString();
        // loop 退出后不能立刻销毁 EventLoop: quit 之后主线程可能仍在使用 loop
        // (典型场景: 栈上 TcpServer 先于 loopThread 析构, ~TcpServer 还会向 loop 投递析构任务),
        // 在此等待 ~EventLoopThread 的释放信号, 保证主线程使用完之前对象一直存活, 消除 use-after-free;
        // 期间主线程投递的任务会留在 pendingFunctors_ 中, 随 ~EventLoop 在 loop 线程安全析构
        destructReleaseSem_.acquire();
        // 在 loop 线程内销毁 EventLoop:
        // ~EventLoop 会摘除 wakeupChannel_(断言 isInLoopThread)并清理本线程 TLS,
        // 若推迟到 ~EventLoopThread(通常在主线程)析构, 会触发断言并误清析构线程的 t_LoopInThisThread
        loop_.reset();
        //for test
    }

    //开启线程 线程中执行 Eventloop线程函数
    EventLoop* EventLoopThread::startLoop()
    {
        assert(!thread_.started());
        thread_.start();
        EventLoop* loop = loopPromise_.get_future().get(); //阻塞等待loop创建完
        return loop;
    }
} // namespace net
} // namespace mymoduo
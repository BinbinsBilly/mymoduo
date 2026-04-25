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
        if (loop_) {
            loop_->quit();   // 退出事件循环
        }
        thread_.join();    // 等待线程结束 !!! 这个很重要, 会阻塞等待loop线程退出
        // loop_ 会被 unique_ptr 自动销毁
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
        // loop_ 在析构时自动销毁
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
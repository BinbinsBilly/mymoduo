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
        //线程析构时 自动调用Thread析构函数
        loop_ -> quit();   // 退出事件循环
        thread_.join();    // 等待线程结束 !!! 这个很重要, 会阻塞等待loop线程退出
        loop_ = nullptr;
    }

    // EventLoop线程函数
    void EventLoopThread::threadFunc()
    {
        EventLoop loop; //栈上loop
        loopPromise_.set_value(&loop);
        if(cb_)  //如果设置了回调 在循环前调用
        {
            cb_(&loop);
        }

        LOG_DEBUG << "EventLoopThread" << "[Name]: "<< thread_.name() << "- EventLoop thread started in thread "
                  << CurrentThread::tidString();
        //for test
        // std::thread::id tid = CurrentThread::threadId();
        // std::cout<< "EventLoopThread::threadFunc() - EventLoop thread started in thread "
        //          << tid << std::endl;

        loop.loop(); // 事件循环 一去不返
        LOG_DEBUG << "EventLoopThread" << "[Name]: "<< thread_.name() << "- EventLoop thread exit in thread "
                  << CurrentThread::tidString();
        //for test
    }

    //开启线程 线程中执行 Eventloop线程函数
    EventLoop* EventLoopThread::startLoop()
    {
        assert(!thread_.started());
        thread_.start();
        loop_ = loopPromise_.get_future().get(); //阻塞等待loop创建完
        return loop_;
    }
} // namespace net
} // namespace mymoduo
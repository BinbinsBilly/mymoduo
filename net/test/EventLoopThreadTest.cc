#include "EventLoopThread.h"
#include "EventLoop.h"
#include "CurrentThread.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace mymoduo::net;
using namespace mymoduo;

// 测试启动与回调执行 & quit
static void test_start_and_callback() {
    std::atomic<bool> cbCalled{false};
    {
        EventLoopThread loopThread([&](mymoduo::net::EventLoop* loop){
            cbCalled.store(true, std::memory_order_relaxed);
            assert(loop->isInLoopThread() && "callback not in loop thread");
            // 提前安排退出，避免长时间阻塞
            loop->runAfter(0.05, [loop]{ loop->quit(); });
        }, "ELT1");
        mymoduo::net::EventLoop* loop = loopThread.startLoop();
        (void)loop; // 未直接使用
        // 等待线程退出由析构处理
    }
    assert(cbCalled.load(std::memory_order_relaxed) && "loop init callback not called");
    std::cout << "[OK] test_start_and_callback" << std::endl;
}

// runInLoop: 从其他线程调用应立即在事件循环执行
static void test_runInLoop_executes() {
    std::atomic<bool> ran{false};
    EventLoopThread loopThread(nullptr, "ELT2");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();
    loop->runInLoop([&]{ ran.store(true, std::memory_order_release); });
    loop->runAfter(0.05, [loop]{ loop->quit(); });
    // 等待结束
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(ran.load(std::memory_order_acquire) && "runInLoop functor not executed");
    std::cout << "[OK] test_runInLoop_executes" << std::endl;
}

// queueInLoop 顺序保持 (FIFO) 简单验证
static void test_queueInLoop_order() {
    std::vector<int> seq;
    EventLoopThread loopThread(nullptr, "ELT3");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();
    loop->queueInLoop([&]{ seq.push_back(1); });
    loop->queueInLoop([&]{ seq.push_back(2); });
    loop->queueInLoop([&]{ seq.push_back(3); });
    loop->runAfter(0.05, [loop]{ loop->quit(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    assert(seq.size() == 3 && seq[0]==1 && seq[1]==2 && seq[2]==3 && "queueInLoop order broken");
    std::cout << "[OK] test_queueInLoop_order" << std::endl;
}

// 析构时序: EventLoop 必须在 loop 线程内销毁, 且不得破坏主线程的 EventLoop 注册
static void test_destruct_in_loop_thread() {
    mymoduo::net::EventLoop mainLoop; // 主线程自身的 EventLoop
    {
        EventLoopThread loopThread(nullptr, "ELT4");
        mymoduo::net::EventLoop* loop = loopThread.startLoop();
        loop->runAfter(0.05, [loop]{ loop->quit(); });
        // 作用域结束: ~EventLoopThread 中 quit + join, EventLoop 应已在 loop 线程内销毁
    }
    // 若 EventLoop 在主线程析构: removeChannel 的 assertInLoopThread 会失败,
    // 且 t_LoopInThisThread 会被误清, 导致下面断言失败
    assert(mymoduo::net::EventLoop::getEventLoopofCurrentThread() == &mainLoop);
    std::cout << "[OK] test_destruct_in_loop_thread" << std::endl;
}

int main() {
    test_start_and_callback();
    test_runInLoop_executes();
    test_queueInLoop_order();
    test_destruct_in_loop_thread();
    std::cout << "[ALL PASS] EventLoopThread tests" << std::endl;
    return 0;
}

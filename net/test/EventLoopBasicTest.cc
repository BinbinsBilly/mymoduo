// EventLoopBasicTest.cc - 拆分多功能测试：runAfter / runEvery / queueInLoop / 跨线程提交 / quit
#include "EventLoop.h"
#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>
#include <vector>

using namespace mymoduo::net;
using namespace mymoduo;

// 在同一线程构造并运行 loop，避免构造与运行线程不一致
template<typename F>
static void runLoop(F&& setup) {
    mymoduo::net::EventLoop loop;
    setup(loop);
    loop.loop();
}

static void testRunAfter() {
    std::cout << "[testRunAfter]" << std::endl;
    std::atomic<int> fired{0};
    runLoop([&](mymoduo::net::EventLoop& loop){
        loop.runAfter(0.03, [&]{ fired.fetch_add(1); });
        loop.runAfter(0.06, [&]{ loop.quit(); });
    });
    assert(fired.load()==1);
}

static void testRunEvery() {
    std::cout << "[testRunEvery]" << std::endl;
    std::atomic<int> count{0};
    runLoop([&](mymoduo::net::EventLoop& loop){
        loop.runEvery(0.02, [&]{ count.fetch_add(1); });
        loop.runAfter(0.11, [&]{ loop.quit(); });
    });
    assert(count.load() >= 4); // 预期约5~6次，容忍下限
}

static void testQueueInLoopSameThread() {
    std::cout << "[testQueueInLoopSameThread]" << std::endl;
    std::atomic<int> count{0};
    runLoop([&](mymoduo::net::EventLoop& loop){
        loop.queueInLoop([&]{ count.fetch_add(1); });
        loop.runAfter(0.02, [&]{ loop.quit(); });
    });
    assert(count.load()==1);
}

static void testCrossThreadQueueAndRunAfter() {
    std::cout << "[testCrossThreadQueueAndRunAfter]" << std::endl;
    std::atomic<int> qCount{0};
    std::atomic<int> afterCount{0};
    // 在新线程构造与运行 loop
    std::thread loopThread([&]{
        mymoduo::net::EventLoop loop;
        // 退出点
        loop.runAfter(0.09, [&]{ loop.quit(); });
        // 主线程稍后再跨线程提交任务与定时器
        std::thread submitter([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            loop.queueInLoop([&]{ qCount.fetch_add(1); });
            loop.runAfter(0.02, [&]{ afterCount.fetch_add(1); });
        });
        loop.loop();
        submitter.join();
    });
    loopThread.join();
    assert(qCount.load()==1);
    assert(afterCount.load()==1);
}

static void testQuitEarly() {
    std::cout << "[testQuitEarly]" << std::endl;
    std::atomic<int> marker{0};
    runLoop([&](mymoduo::net::EventLoop& loop){
        loop.runAfter(0.02, [&]{ marker.fetch_add(1); loop.quit(); });
        loop.runAfter(0.10, [&]{ marker.fetch_add(1); }); // 不应执行
    });
    assert(marker.load()==1);
}

int main() {
    std::cout << "EventLoopBasicTest started" << std::endl;
    testRunAfter();
    testRunEvery();
    testQueueInLoopSameThread();
    testCrossThreadQueueAndRunAfter();
    testQuitEarly();
    std::cout << "EventLoopBasicTest ALL passed" << std::endl;
    return 0;
}

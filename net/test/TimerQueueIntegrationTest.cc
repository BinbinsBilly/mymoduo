// 分类定时器集成测试：分别验证 runAfter / runEvery / cancel / 跨线程调度与退出。
#include "EventLoop.h"
#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>
#include <vector>
#include <functional>

using namespace mymoduo::net;

// 简单辅助：在当前线程构造并启动 loop，调用 setup 注册任务，loop.loop() 直到 quit。
template<typename F>
static void runLoopWithSetup(F&& setup) {
    mymoduo::net::EventLoop loop; // 构造与运行保持同一线程避免线程ID迁移问题
    setup(loop);
    loop.loop();
}

// 测试一次性定时器 runAfter 正确触发一次
static void testRunAfterSingle() {
    std::cout << "[testRunAfterSingle] start" << std::endl;
    std::atomic<int> fired{0};
    runLoopWithSetup([&](mymoduo::net::EventLoop& loop){
        loop.runAfter(0.05, [&]{ fired.fetch_add(1); });
        loop.runAfter(0.10, [&]{ loop.quit(); });
    });
    assert(fired.load() == 1);
    std::cout << "[testRunAfterSingle] passed fired=" << fired.load() << std::endl;
}

// 测试重复定时器 runEvery：验证至少触发若干次（不要求精确次数，避免调度抖动）
static void testRunEveryRepeat() {
    std::cout << "[testRunEveryRepeat] start" << std::endl;
    std::atomic<int> count{0};
    runLoopWithSetup([&](mymoduo::net::EventLoop& loop){
        loop.runEvery(0.02, [&]{ count.fetch_add(1); });
        loop.runAfter(0.11, [&]{ loop.quit(); });
    });
    // 理论次数约 5~6 次，要求下限 4 以抗抖动
    assert(count.load() >= 4);
    std::cout << "[testRunEveryRepeat] passed count=" << count.load() << std::endl;
}

// 测试 cancel：取消未触发的一次性定时器
static void testCancelTimer() {
    std::cout << "[testCancelTimer] start" << std::endl;
    std::atomic<int> fired{0};
    runLoopWithSetup([&](mymoduo::net::EventLoop& loop){
        int64_t seq = loop.runAfter(0.05, [&]{ fired.fetch_add(1); });
        loop.cancel(seq);
        loop.runAfter(0.08, [&]{ loop.quit(); });
    });
    assert(fired.load() == 0);
    std::cout << "[testCancelTimer] passed fired=" << fired.load() << std::endl;
}

// 测试跨线程调度：在 loop 运行后由其它线程调用 runAfter/queueInLoop 添加任务
static void testCrossThreadSchedule() {
    std::cout << "[testCrossThreadSchedule] start" << std::endl;
    std::atomic<int> crossCount{0};
    std::atomic<int> localCount{0};
    mymoduo::net::EventLoop loop; // 直接手动控制生命周期以测试跨线程

    std::thread worker([&]{
        // 等待 loop 进入轮询（粗略睡眠）然后跨线程提交任务
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        loop.runAfter(0.02, [&]{ crossCount.fetch_add(1); });
        loop.queueInLoop([&]{ localCount.fetch_add(1); });
    });

    loop.runAfter(0.07, [&]{ loop.quit(); });
    loop.loop();
    worker.join();

    // queueInLoop 保证执行一次； runAfter 将在 ~0.02 后触发
    assert(localCount.load() == 1);
    assert(crossCount.load() == 1);
    std::cout << "[testCrossThreadSchedule] passed cross=" << crossCount.load() << " local=" << localCount.load() << std::endl;
}

int main() {
    testRunAfterSingle();
    testRunEveryRepeat();
    testCancelTimer();
    testCrossThreadSchedule();
    std::cout << "TimerQueueIntegrationTest ALL passed" << std::endl;
    return 0;
}

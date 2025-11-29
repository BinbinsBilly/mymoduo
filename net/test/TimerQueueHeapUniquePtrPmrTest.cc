// TimerQueueHeapUniquePtrPmrTest.cc
// 基于手动推进时间的单元测试，避免实际 EventLoop loop() 与 timerfd 触发。

#include "TimerQueueHeapUniquePtrPmr.h"
#include "EventLoop.h"
#include "TimeStamp.h"
// 避免多次构造 EventLoop 引发线程断言：使用单例。
static mymoduo::net::EventLoop g_loop;

#include <cassert>
#include <iostream>
#include <atomic>
#include <vector>

using namespace mymoduo;            // TimeStamp
using namespace mymoduo::net;       // TimerQueueHeapUniquePtrPmr, EventLoop

namespace{
using namespace mymoduo::base; // TimeStamp

static void testBasicOrder() {
    TimerQueueHeapUniquePtrPmr queue(&g_loop);

    TimeStamp base = TimeStamp::now();
    std::vector<int> order;

    // 预定三个一次性定时器：按时间顺序 (5ms,10ms,15ms) 执行
    queue.addTimer([&]{ order.push_back(2); }, base + 0.010, 0.0); // sequence 不保证顺序，逻辑上按时间应第二执行
    queue.addTimer([&]{ order.push_back(1); }, base + 0.005, 0.0);
    queue.addTimer([&]{ order.push_back(3); }, base + 0.015, 0.0);

    // 手动推进时间 (注意 executeExpired 使用 < now 条件)
    queue.processExpiredForTest(base + 0.006);  // 应只执行 5ms 定时器
    assert(order.size() == 1 && order[0] == 1);

    queue.processExpiredForTest(base + 0.011);  // 再执行 10ms 定时器
    assert(order.size() == 2 && order[1] == 2);

    queue.processExpiredForTest(base + 0.016);  // 最后执行 15ms 定时器
    assert(order.size() == 3 && order[2] == 3);

    std::cout << "testBasicOrder passed" << std::endl;
}

static void testCancelSingle() {
    TimerQueueHeapUniquePtrPmr queue(&g_loop);
    TimeStamp base = TimeStamp::now();
    std::atomic<int> fired{0};

    int64_t seq = queue.addTimer([&]{ fired.fetch_add(1); }, base + 0.010, 0.0);
    // 立即取消
    queue.cancel(seq);
    queue.processExpiredForTest(base + 0.020); // 推进时间超过到期
    assert(fired.load() == 0); // 不应触发
    std::cout << "testCancelSingle passed" << std::endl;
}

static void testRepeatTimer() {
    TimerQueueHeapUniquePtrPmr queue(&g_loop);
    TimeStamp base = TimeStamp::now();
    std::atomic<int> count{0};

    // 重复定时器：首次 2ms，到期后每 5ms 重新启动
    queue.addTimer([&]{ count.fetch_add(1); }, base + 0.002, 0.005);

    // 推进到 3ms -> 第一次触发
    queue.processExpiredForTest(base + 0.003);
    assert(count.load() == 1);
    std::cout<< "Intermediate count=" << count.load() << std::endl;

    // 推进到 8ms -> 第二次触发 (3ms + 5ms = 8ms 到期，<9ms)
    queue.processExpiredForTest(base + 0.009);
    assert(count.load() == 2);
    std::cout<< "Intermediate count=" << count.load() << std::endl;

    // 推进到 9 + 5ms -> 第三次触发 (14ms 到期，<15ms)
    queue.processExpiredForTest(base + 0.015);
    assert(count.load() == 3);
    std::cout<< "Intermediate count=" << count.load() << std::endl;

    // 再推进到 14 + 4ms -> 第四次触发 (19ms 到期，<20ms)
    queue.processExpiredForTest(base + 0.021);
    assert(count.load() == 4);
    std::cout<< "Intermediate count=" << count.load() << std::endl;
    queue.processExpiredForTest(base + 0.027);
    assert(count.load() == 5);
    std::cout<< "Intermediate count=" << count.load() << std::endl;
    queue.processExpiredForTest(base + 0.033);
    assert(count.load() == 6);
    std::cout<< "Intermediate count=" << count.load() << std::endl;
    std::cout << "testRepeatTimer passed count=" << count.load() << std::endl;
}
} // namespace

int main() {
    testBasicOrder();
    testCancelSingle();
    testRepeatTimer();
    std::cout << "TimerQueueHeapUniquePtrPmrTest all passed" << std::endl;
    return 0;
}


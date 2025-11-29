#include "timer.h"
#include "TimeStamp.h"
#include <cassert>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

using mymoduo::net::Timer; // Timer 在 namespace mymoduo::net
using mymoduo::base::TimeStamp;
namespace{
using namespace mymoduo::net;

static void testSequenceIncrements() {
    TimerCb cb = []{};
    mymoduo::base::TimeStamp now = mymoduo::base::TimeStamp::now();
    Timer t1(cb, now, 0.0);
    int64_t seq1 = t1.getSequence();
    TimerCb cb2 = []{};
    Timer t2(cb2, now, 0.0);
    int64_t seq2 = t2.getSequence();
    assert(seq2 == seq1 + 1);
    assert(Timer::n_created() >= 2);
}

static void testRunInvokesCallback() {
    std::atomic<int> counter{0};
    TimerCb cb = [&counter]{ ++counter; };
    Timer t(cb, mymoduo::base::TimeStamp::now(), 0.0);
    t.run();
    assert(counter.load() == 1);
}

static void testRestartRepeat() {
    std::atomic<int> counter{0};
    TimerCb cb = [&counter]{ ++counter; };
    mymoduo::base::TimeStamp start = mymoduo::base::TimeStamp::now();
    double interval = 1.0; // 1 second
    Timer t(cb, start, interval); // repeat timer
    mymoduo::base::TimeStamp exp1 = t.expiration();
    assert(exp1 == start); // 初始 expiration 为传入 when
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    mymoduo::base::TimeStamp now2 = mymoduo::base::TimeStamp::now();
    t.restart(now2);
    mymoduo::base::TimeStamp exp2 = t.expiration();
    // 允许少量误差, exp2 应大于 exp1 且接近 now2 + interval
    assert(exp2 > exp1);
    mymoduo::base::TimeStamp expected = now2 + interval;
    // 判断两者差距不超过 5ms
    int64_t diff = exp2.microsecondsSinceEpoch() - expected.microsecondsSinceEpoch();
    if (diff < 0) diff = -diff;
    assert(diff < 5000); // 5ms 容差
}

static void testRestartNonRepeat() {
    TimerCb cb = []{};
    Timer t(cb, mymoduo::base::TimeStamp::now(), 0.0); // non-repeating
    mymoduo::base::TimeStamp expBefore = t.expiration();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    t.restart(mymoduo::base::TimeStamp::now());
    mymoduo::base::TimeStamp expAfter = t.expiration();
    // 非重复 restart 后 expiration 变为 invalid
    assert(!expAfter.valid());
    assert(expBefore.valid());
}
} // namespace

int main() {
    testSequenceIncrements();
    testRunInvokesCallback();
    testRestartRepeat();
    testRestartNonRepeat();
    std::cout << "All Timer tests passed." << std::endl;
    return 0;
}


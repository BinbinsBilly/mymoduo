#include "Thread.h"
#include "CurrentThread.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace mymoduo::base;
using namespace mymoduo;

// 单个线程启动与 join
static void test_basic_start_join() {
    std::atomic<bool> done{false};
    Thread t([&]{ done.store(true, std::memory_order_release); });
    assert(!t.started());
    t.start();
    t.join();
    assert(done.load(std::memory_order_acquire) && "basic start/join failed");
    std::cout << "[OK] test_basic_start_join" << std::endl;
}

// numCreated 计数递增
static void test_num_created_increment() {
    int before = Thread::numCreated();
    constexpr int N = 5;
    std::vector<Thread> threads;
    std::atomic<int> counter{0};
    for(int i=0;i<N;++i) {
        threads.emplace_back(Thread([&]{ counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for(auto &th : threads) th.start();
    for(auto &th : threads) th.join();
    int after = Thread::numCreated();
    assert(counter.load() == N && "thread functions not all executed");
    assert(after - before >= N && "numCreated did not increment correctly");
    std::cout << "[OK] test_num_created_increment (before=" << before << ", after=" << after << ")" << std::endl;
}

// 自定义名称设置
static void test_thread_name_set() {
    std::promise<std::string> namePromise;
    auto fut = namePromise.get_future();
    Thread t([&]{ namePromise.set_value(CurrentThread::name()); });
    std::string customName = "WorkerA";
    // 手动修改构造的名称（因为构造时传递）
    // 重新构造以带名字
    Thread t2([&]{ namePromise.set_value(CurrentThread::name()); }, customName);
    t2.start();
    auto got = fut.get();
    t2.join();
    assert(got == customName && "custom thread name mismatch");
    std::cout << "[OK] test_thread_name_set (name=" << got << ")" << std::endl;
}

// 析构时未 join -> detach 行为
static void test_detach_on_destructor() {
    std::atomic<bool> ran{false};
    {
        Thread t([&]{ std::this_thread::sleep_for(std::chrono::milliseconds(40)); ran.store(true, std::memory_order_release); });
        t.start();
        // 不调用 join, 出作用域触发 detach
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    assert(ran.load(std::memory_order_acquire) && "detached thread did not finish in time");
    std::cout << "[OK] test_detach_on_destructor" << std::endl;
}

int main() {
    test_basic_start_join();
    test_num_created_increment();
    test_thread_name_set();
    test_detach_on_destructor();
    std::cout << "[ALL PASS] Thread tests" << std::endl;
    return 0;
}

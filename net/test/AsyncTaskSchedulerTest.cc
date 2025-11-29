#include "AsyncTaskScheduler.hpp"
#include "EventLoop.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

using namespace mymoduo::net;

// Test 1: void task + void callback
static void test_void_task_void_cb() {
     mymoduo::net::EventLoop loop; // 单线程内构造与运行
    AsyncTaskScheduler scheduler(&loop);
    std::atomic<int> hit{0};
    scheduler.scheduleTask([](){ /* do work */ }, [&](){ hit.fetch_add(1, std::memory_order_relaxed); });
    loop.runAfter(0.05, [&loop]{ loop.quit(); });
    loop.loop();
    assert(hit.load() == 1 && "void task + void callback not executed");
    std::cout << "[OK] test_void_task_void_cb" << std::endl;
}

// Test 2: non-void task result forwarded to callback
static void test_return_value() {
    mymoduo::net::EventLoop loop;
    AsyncTaskScheduler scheduler(&loop);
    std::atomic<int> received{0};
    scheduler.scheduleTask([](){ return 42; }, [&received](int v){ received.store(v, std::memory_order_relaxed); });
    loop.runAfter(0.05, [&loop]{ loop.quit(); });
    loop.loop();
    assert(received.load() == 42 && "return value not forwarded");
    std::cout << "[OK] test_return_value" << std::endl;
}

// Test 3: execution occurs inside loop thread
static void test_runs_in_loop_thread() {
    mymoduo::net::EventLoop loop;
    AsyncTaskScheduler scheduler(&loop);
    std::atomic<bool> inLoop{false};
    scheduler.scheduleTask([](){ return 7; }, [&](int){
        if(loop.isInLoopThread()) inLoop.store(true, std::memory_order_relaxed);
    });
    loop.runAfter(0.05, [&loop]{ loop.quit(); });
    loop.loop();
    assert(inLoop.load() && "callback did not run in loop thread");
    std::cout << "[OK] test_runs_in_loop_thread" << std::endl;
}

    // Test 4: cross-thread scheduling (schedule from another std::thread)
    static void test_cross_thread_schedule() {
        mymoduo::net::EventLoop loop;
        AsyncTaskScheduler scheduler(&loop);
        std::atomic<bool> done{false};
        std::atomic<int> value{0};

        std::thread worker([&](){
            scheduler.scheduleTask([](){ return 99; }, [&](int v){
                value.store(v, std::memory_order_relaxed);
                done.store(true, std::memory_order_release);
                loop.quit();
            });
        });

        // Safety timeout in case callback not executed
        loop.runAfter(1.0, [&](){ if(!done.load(std::memory_order_acquire)) loop.quit(); });
        loop.loop();
        worker.join();
        assert(done.load(std::memory_order_acquire) && value.load() == 99 && "cross-thread schedule failed");
        std::cout << "[OK] test_cross_thread_schedule" << std::endl;
    }

int main() {
    test_void_task_void_cb();
    test_return_value();
    test_runs_in_loop_thread();
        test_cross_thread_schedule();
    std::cout << "[ALL PASS] AsyncTaskScheduler tests" << std::endl;
    return 0;
}

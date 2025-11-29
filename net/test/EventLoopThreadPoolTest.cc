#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "CurrentThread.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace mymoduo::net;
using namespace mymoduo;

// 基础: 无线程模式
static void test_no_threads() {
    mymoduo::net::EventLoop baseLoop; // 不启动 loop.loop() 仅测试接口行为
    EventLoopThreadPool pool(&baseLoop, "PoolNoThread");
    std::atomic<bool> cbCalled{false};
    bool ok = pool.start([&](mymoduo::net::EventLoop* loop){
        assert(loop == &baseLoop);
        cbCalled.store(true, std::memory_order_relaxed);
    });
    assert(ok && "start should succeed");
    assert(!pool.started() && "started() must be false when threadNum==0");
    assert(cbCalled.load() && "callback not invoked for base loop");
    mymoduo::net::EventLoop* l1 = pool.getNextLoop();
    mymoduo::net::EventLoop* l2 = pool.getLoopForHash(123);
    auto all = pool.getAllLoop();
    assert(l1 == &baseLoop && l2 == &baseLoop && all.size()==1 && all[0]==&baseLoop);
    std::cout << "[OK] test_no_threads" << std::endl;
}

// 多线程: 基本启动 + 回调 + 负载均衡 (需要公开 setThreadNum)
static void test_multi_threads_basic() {
    mymoduo::net::EventLoop baseLoop; // 基线程
    EventLoopThreadPool pool(&baseLoop, "PoolMt");
    pool.setThreadNum(3);
    std::atomic<int> initCount{0};
    bool ok = pool.start([&](mymoduo::net::EventLoop* loop){
        // 每个新线程 loop 调用一次
        assert(loop != nullptr);
        initCount.fetch_add(1, std::memory_order_relaxed);
        // 安排稍后退出，避免测试卡死（由各线程在析构时 quit）
    });
    assert(ok && pool.started());
    // 线程初始化可能仍在进行，等待片刻
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(initCount.load() == 3 && "init callback not called 3 times");

    auto all = pool.getAllLoop();
    assert(all.size() == 3 && "pool should expose 3 loops");

    // round-robin 测试
    std::vector<mymoduo::net::EventLoop*> seq;
    for(int i=0;i<6;++i) seq.push_back(pool.getNextLoop());
    assert(seq[0]==all[0] && seq[1]==all[1] && seq[2]==all[2]);
    assert(seq[3]==all[0] && seq[4]==all[1] && seq[5]==all[2]);

    // hash 测试
    for(size_t h=0; h<10; ++h) {
        mymoduo::net::EventLoop* expected = all[h % all.size()];
        assert(pool.getLoopForHash(h) == expected);
    }
    std::cout << "[OK] test_multi_threads_basic" << std::endl;
}

// runInLoop 调度到各自线程验证线程ID收集
static void test_run_in_each_loop() {
    mymoduo::net::EventLoop baseLoop; // 主线程 loop 不参与线程池集合
    EventLoopThreadPool pool(&baseLoop, "PoolRun");
    pool.setThreadNum(2);
    pool.start(nullptr);
    auto loops = pool.getAllLoop();
    assert(loops.size()==2);
    std::vector<std::thread::id> ids(loops.size());
    std::atomic<int> done{0};
    for(size_t i=0;i<loops.size();++i) {
        loops[i]->runInLoop([&ids,&done,i]{
            ids[i] = CurrentThread::threadId();
            done.fetch_add(1, std::memory_order_release);
        });
    }
    // 简单等待收集完成
    for(int tries=0; tries<50 && done.load(std::memory_order_acquire)!=static_cast<int>(loops.size()); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(done.load()==static_cast<int>(loops.size()) && "not all runInLoop executed");
    assert(ids[0] != ids[1] && "loops should be on distinct threads");
    assert(ids[0] != baseLoop.threadId() && ids[1] != baseLoop.threadId() && "loop threads must differ from base thread");
    std::cout << "[OK] test_run_in_each_loop" << std::endl;
}

int main() {
    test_no_threads();
    test_multi_threads_basic();
    test_run_in_each_loop();
    std::cout << "[ALL PASS] EventLoopThreadPool tests" << std::endl;
    return 0;
}

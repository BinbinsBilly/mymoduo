#include "CurrentThread.h"
#include <thread>
#include <vector>
#include <unordered_set>
#include <cassert>
#include <iostream>
#include <atomic>
#include <mutex>

// Updated CurrentThreadTest: stricter assertions, verify main thread id storage

int main() {
    using namespace mymoduo::CurrentThread;

    // 主线程基本信息
    size_t mainTidVal1 = tid();
    size_t mainTidVal2 = tid();
    const std::string &mainTidStr = tidString();
    size_t mainTidStrLen = tidStringLength();

    assert(mainTidVal1 == mainTidVal2);          // 缓存一致
    assert(!mainTidStr.empty());
    assert(mainTidStrLen == mainTidStr.size());
    assert(isMainThread());                      // 主线程判定

    std::cout << "[Main] tid=" << mainTidVal1 << " str=" << mainTidStr
              << " len=" << mainTidStrLen << std::endl;

    // 多线程测试: 各线程缓存一致 + 非主线程判定
    constexpr int kThreadCount = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    std::unordered_set<size_t> tidsSet;
    std::mutex setMutex;

    std::atomic<int> cacheMismatch{0};
    std::atomic<int> falseMainFlag{0};

    for(int i=0;i<kThreadCount;++i) {
        threads.emplace_back([&]{
            size_t t1 = tid();
            size_t t2 = tid();
            const std::string &s = tidString();
            size_t sl = tidStringLength();
            assert(t1 == t2);                     // thread_local 缓存
            assert(s.size() == sl);               // 长度一致
            if(isMainThread()) {                  // 子线程不应是主线程
                falseMainFlag.fetch_add(1, std::memory_order_relaxed);
            }
            {
                std::lock_guard<std::mutex> lg(setMutex);
                tidsSet.insert(t1);
            }
        });
    }
    for(auto &th: threads) th.join();

    // 不应包含主线程 tid
    assert(tidsSet.find(mainTidVal1) == tidsSet.end());
    // 期望全部不同（高概率，不绝对保证，但若冲突则重试）
    assert(static_cast<int>(tidsSet.size()) == kThreadCount);
    assert(falseMainFlag.load() == 0);

    // 并发压力测试：多线程重复调用 isMainThread() 验证稳定性
    {
        constexpr int stressThreads = 12;
        constexpr int callsPerThread = 5000;
        std::atomic<int> nonMainTrue{0};
        std::vector<std::thread> stress;
        stress.reserve(stressThreads);
        for(int i=0;i<stressThreads;++i) {
            stress.emplace_back([&]{
                for(int c=0;c<callsPerThread;++c) {
                    if(isMainThread()) {
                        // 非主线程意外为 true
                        nonMainTrue.fetch_add(1, std::memory_order_relaxed);
                    }
                    // 访问其它缓存接口增加交叉压力
                    (void)tid();
                    (void)tidStringLength();
                    if((c & 0xFF) == 0) std::this_thread::yield();
                }
            });
        }
        // 主线程也反复调用确保一致性
        for(int c=0;c<callsPerThread;++c) {
            assert(isMainThread());
            (void)tid();
            if((c & 0x3FF) == 0) std::this_thread::yield();
        }
        for(auto &t: stress) t.join();
        assert(nonMainTrue.load() == 0);
        std::cout << "[Stress] isMainThread 并发测试通过" << std::endl;
    }

    std::cout << "[OK] CurrentThread 所有断言通过" << std::endl;
    return 0;
}

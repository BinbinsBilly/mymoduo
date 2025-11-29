// // New ConditionTest.cc: tests guarded (adopt_lock path) and unguarded (predicate path) waits
// // Updated ConditionTest.cc: corrected template usage and guarded waits
// #include "Condition.h"
// #include "MutexLock.hpp"
// #include "Logging.h"

// #include <thread>
// #include <vector>
// #include <queue>
// #include <atomic>
// #include <cassert>
// #include <iostream>
// #include <chrono>

// using namespace mymoduo;

// // Guarded wait: hold MutexLockGuard, call cond.wait(lg)
// static void testNotifyOneGuarded() {
//     LOG_INFO << "testNotifyOneGuarded start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     bool ready = false;
//     bool passed = false;

//     std::thread waiter([&]{
//         MutexLockGuard lg(m);
//         while(!ready) {
//             cond.wait(lg);
//         }
//         passed = true;
//     });

//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     {
//         MutexLockGuard lg(m);
//         ready = true;
//         cond.notify();
//     }
//     waiter.join();
//     assert(passed);
//     LOG_INFO << "testNotifyOneGuarded OK";
// }

// // Unguarded wait: internal unique_lock
// static void testNotifyOneUnguarded() {
//     LOG_INFO << "testNotifyOneUnguarded start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     std::atomic<bool> fired{false};

//     std::thread w([&]{
//         while(!fired.load(std::memory_order_acquire)) {
//             cond.wait();
//         }
//     });

//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     {
//         MutexLockGuard lg(m);
//         fired.store(true, std::memory_order_release);
//         cond.notify();
//     }
//     w.join();
//     assert(fired.load());
//     LOG_INFO << "testNotifyOneUnguarded OK";
// }

// // notifyAll with guarded waiters
// static void testNotifyAllGuarded() {
//     LOG_INFO << "testNotifyAllGuarded start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     const int N = 5;
//     std::atomic<int> waiting{0};
//     bool ready = false;
//     std::vector<std::thread> threads;
//     threads.reserve(N);

//     for(int i=0;i<N;++i) {
//         threads.emplace_back([&]{
//             MutexLockGuard lg(m);
//             waiting.fetch_add(1, std::memory_order_release);
//             while(!ready) {
//                 cond.wait(lg);
//             }
//         });
//     }
//     while(waiting.load(std::memory_order_acquire) < N) std::this_thread::yield();
//     std::this_thread::sleep_for(std::chrono::milliseconds(20));
//     {
//         MutexLockGuard lg(m);
//         ready = true;
//         cond.notifyAll();
//     }
//     for(auto &t: threads) t.join();
//     LOG_INFO << "testNotifyAllGuarded OK";
// }

// // Producer-consumer guarded pattern
// static void testProducerConsumerGuarded() {
//     LOG_INFO << "testProducerConsumerGuarded start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     std::queue<int> q;
//     const int items = 400;
//     bool done = false;
//     std::atomic<long long> sum{0};

//     std::thread consumer([&]{
//         long long localSum = 0;
//         for(;;) {
//             MutexLockGuard lg(m);
//             while(q.empty() && !done) {
//                 cond.wait(lg);
//             }
//             if(q.empty() && done) break;
//             int v = q.front(); q.pop();
//             localSum += v;
//         }
//         sum.store(localSum, std::memory_order_release);
//     });

//     std::thread producer([&]{
//         for(int i=1;i<=items;++i) {
//             {
//                 MutexLockGuard lg(m);
//                 q.push(i);
//                 cond.notify();
//             }
//             if(i % 120 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
//         }
//         {
//             MutexLockGuard lg(m);
//             done = true;
//             cond.notifyAll();
//         }
//     });

//     producer.join();
//     consumer.join();
//     long long expected = (static_cast<long long>(items) * (items + 1)) / 2;
//     assert(sum.load(std::memory_order_acquire) == expected);
//     LOG_INFO << "testProducerConsumerGuarded OK";
// }

// // notify 在 waiter 实际进入 wait 之前发出，依靠外层谓词避免“丢失唤醒”
// static void testNotifyBeforeWaitGuarded() {
//     LOG_INFO << "testNotifyBeforeWaitGuarded start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     bool ready = true; // 先置 true
//     std::atomic<bool> done{false};

//     // 先通知，再启动线程
//     {
//         MutexLockGuard lg(m);
//         cond.notify(); // 如果没有谓词，潜在丢失唤醒；这里 ready 已经 true
//     }

//     std::thread waiter([&]{
//         MutexLockGuard lg(m);
//         while(!ready) { // ready 已为 true，不进入 wait
//             cond.wait(lg);
//         }
//         done.store(true, std::memory_order_release);
//     });
//     waiter.join();
//     assert(done.load(std::memory_order_acquire));
//     LOG_INFO << "testNotifyBeforeWaitGuarded OK";
// }

// // 处理可能的“虚假唤醒”：多次 notify 而谓词不满足时继续等待
// static void testSpuriousWakeResilience() {
//     LOG_INFO << "testSpuriousWakeResilience start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     bool ready = false;
//     std::atomic<int> wakeCount{0};

//     std::thread waiter([&]{
//         MutexLockGuard lg(m);
//         while(!ready) {
//             cond.wait(lg);
//             ++wakeCount; // 统计唤醒次数，虚假唤醒将 >1
//         }
//     });

//     // 模拟若干虚假唤醒
//     for(int i=0;i<3;++i) {
//         {
//             MutexLockGuard lg(m);
//             cond.notify(); // 未改变 ready，waiter 会再次等待
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(5));
//     }
//     {
//         MutexLockGuard lg(m);
//         ready = true;
//         cond.notify();
//     }
//     waiter.join();
//     assert(wakeCount.load() >= 1); // 至少被唤醒一次
//     LOG_INFO << "testSpuriousWakeResilience OK wakes=" << wakeCount.load();
// }

// // 在一次 notifyAll 后，新加入的等待者不会被“过去的广播”唤醒，需要新的通知
// static void testLateWaiterAfterNotifyAll() {
//     LOG_INFO << "testLateWaiterAfterNotifyAll start";
//     Mutex m;
//     Condition<Mutex> cond(m);
//     bool phase1 = false;
//     int initialWaiters = 3;
//     std::atomic<int> passedPhase1{0};
//     std::vector<std::thread> threads;
//     threads.reserve(initialWaiters);
//     for(int i=0;i<initialWaiters;++i) {
//         threads.emplace_back([&]{
//             MutexLockGuard lg(m);
//             while(!phase1) {
//                 cond.wait(lg);
//             }
//             ++passedPhase1;
//         });
//     }
//     // 等待进入等待状态
//     std::this_thread::sleep_for(std::chrono::milliseconds(20));
//     {
//         MutexLockGuard lg(m);
//         phase1 = true;
//         cond.notifyAll();
//     }
//     for(auto &t: threads) t.join();
//     assert(passedPhase1.load() == initialWaiters);

//     // 新的等待者，对旧的 notifyAll 不敏感
//     bool phase2 = false;
//     std::atomic<bool> phase2Done{false};
//     std::thread late([&]{
//         MutexLockGuard lg(m);
//         while(!phase2) {
//             cond.wait(lg);
//         }
//         phase2Done.store(true, std::memory_order_release);
//     });
//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     assert(!phase2Done.load()); // 尚未被唤醒
//     {
//         MutexLockGuard lg(m);
//         phase2 = true;
//         cond.notifyAll();
//     }
//     late.join();
//     assert(phase2Done.load());
//     LOG_INFO << "testLateWaiterAfterNotifyAll OK";
// }

// int main() {
//     LOG_INFO << "Condition tests start";
//     testNotifyOneGuarded();
//     testNotifyOneUnguarded();
//     testNotifyAllGuarded();
//     testProducerConsumerGuarded();
//     testNotifyBeforeWaitGuarded();
//     testSpuriousWakeResilience();
//     testLateWaiterAfterNotifyAll();
//     LOG_INFO << "Condition tests passed";
//     std::cout << "All Condition tests passed." << std::endl;
//     return 0;
// }

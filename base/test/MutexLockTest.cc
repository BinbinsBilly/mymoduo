// #include "MutexLock.hpp"
// #include "Atomic.hpp"
// #include "Logging.h"

// #include <thread>
// #include <vector>
// #include <cassert>
// #include <iostream>
// #include <chrono>

// using namespace mymoduo;

// static void testMutexLockBasic() {
//     Mutex lock;
//     AtomicInt64 counter(0);
//     const int threads = 4;           // 减少线程降低竞争
//     const int iters = 20000;         // 减少迭代次数加快测试
//     std::vector<std::thread> v;
//     v.reserve(threads);
//     for (int t = 0; t < threads; ++t) {
//         v.emplace_back([&](){
//             for (int i = 0; i < iters; ++i) {
//                 MutexLockGuard g(lock);
//                 counter.getAndAdd(1);
//             }
//         });
//     }
//     for (auto &th : v) th.join();
//     assert(counter.get() == static_cast<long long>(threads) * iters);
//     LOG_INFO << "testMutexLockBasic OK";
// }

// // 验证 holder_ 追踪当前线程持有状态
// static void testMutexHolderTracking() {
//     Mutex m;
//     assert(!m.isHeldByCurrentThread());
//     {
//         MutexLockGuard g(m);
//         //error 
//         assert(m.isHeldByCurrentThread());
//     }
//     assert(!m.isHeldByCurrentThread());
//     // try_lock path
//     if(m.try_lock()) {
//         assert(m.isHeldByCurrentThread());
//         m.unlock();
//     } else {
//         assert(false && "try_lock unexpectedly failed in single-thread context");
//     }
//     LOG_INFO << "testMutexHolderTracking OK";
// }

// static void testRWMutexReadersWriters() {
//     RWMutex rw;
//     int sharedValue = 0;
//     const int writers = 2;
//     const int readers = 4;
//     const int writerIters = 5000;    // 减少写迭代次数

//     std::vector<std::thread> wv;
//     std::vector<std::thread> rv;

//     for (int w = 0; w < writers; ++w) {
//         wv.emplace_back([&]() {
//             for (int i = 0; i < writerIters; ++i) {
//                 WriteLockGuard g(rw);
//                 ++sharedValue;
//             }
//         });
//     }

//     for (int r = 0; r < readers; ++r) {
//         rv.emplace_back([&]() {
//             int last = -1;
//             const int maxLoops = writerIters * 10; // 防止意外无限循环
//             int loops = 0;
//             while (true) {
//                 {
//                     ReadLockGuard g(rw);
//                     int snap = sharedValue;
//                     assert(snap >= last); // 不应出现回退
//                     last = snap;
//                     if (snap == writers * writerIters) break; // 已全部写完
//                 }
//                 if (++loops > maxLoops) {
//                     assert(false && "Reader loop timeout");
//                     break;
//                 }
//                 std::this_thread::sleep_for(std::chrono::microseconds(50));
//             }
//         });
//     }

//     for (auto &th : wv) th.join();
//     for (auto &th : rv) th.join();
//     assert(sharedValue == writers * writerIters);
//     LOG_INFO << "testRWMutexReadersWriters OK";
// }

// // 验证 MutexLockGuard 作用域自动释放与阻塞行为
// static void testMutexLockGuardBlocking() {
//     Mutex lock;
//     bool secondEntered = false;
//     std::thread t;
//     {
//         MutexLockGuard g(lock);   //主线程拿锁
//         t = std::thread([&](){
//             // 这里会阻塞直到外层 guard 退出
//             MutexLockGuard g2(lock); //子线程拿锁
//             secondEntered = true;
//         });
//         std::this_thread::sleep_for(std::chrono::milliseconds(10));
//         // 此时第二个线程尚未进入临界区
//         assert(secondEntered == false);
//         // 不要在持锁作用域内 join，否则死锁：主线程等待子线程，子线程等待锁
//     }
//     t.join();
//     // 作用域结束后第二个线程已经获取到锁
//     assert(secondEntered == true);
//     LOG_INFO << "testMutexLockGuardBlocking OK";
// }

// // 验证 RWMutex 的读锁阻塞写锁，读锁释放后写锁继续
// static void testRWMutexGuardBlocking() {
//     RWMutex rw;
//     int value = 0;
//     std::atomic<bool> readerStarted{false};
//     std::atomic<bool> writerDone{false};

//     std::thread reader([&]{
//         ReadLockGuard rg(rw);
//         readerStarted.store(true, std::memory_order_release);
//         std::this_thread::sleep_for(std::chrono::milliseconds(30));
//         (void)value; // 模拟读操作
//     });

//     // 等待读者拿到锁
//     while(!readerStarted.load(std::memory_order_acquire)) {
//         std::this_thread::yield();
//     }

//     std::thread writer([&]{
//         WriteLockGuard wg(rw); // 需等待 reader 释放
//         value = 1;
//         writerDone.store(true, std::memory_order_release);
//     });

//     std::this_thread::sleep_for(std::chrono::milliseconds(10));
//     // 写线程尚未完成（被读锁阻塞）
//     assert(writerDone.load(std::memory_order_acquire) == false);

//     reader.join();
//     writer.join();
//     // 写线程最终完成并更新 value
//     assert(writerDone.load(std::memory_order_acquire) == true);
//     assert(value == 1);
//     LOG_INFO << "testRWMutexGuardBlocking OK";
// }

// // 两个线程以一致的锁顺序获取多锁，验证无死锁
// static void testConsistentLockOrderNoDeadlock() {
//     LOG_INFO << "testConsistentLockOrderNoDeadlock start";
//     Mutex m1, m2;
//     std::atomic<int> finished{0};
//     auto worker = [&](int loops){
//         for(int i=0;i<loops;++i) {
//             // 固定顺序：先 m1 后 m2
//             MutexLockGuard g1(m1);
//             MutexLockGuard g2(m2);
//             assert(m1.isHeldByCurrentThread());
//             assert(m2.isHeldByCurrentThread());
//         }
//         finished.fetch_add(1, std::memory_order_release);
//     };
//     std::thread t1(worker, 1000);
//     std::thread t2(worker, 1000);
//     t1.join(); t2.join();
//     assert(finished.load() == 2);
//     LOG_INFO << "testConsistentLockOrderNoDeadlock OK";
// }

// // 演示潜在的锁顺序反转，通过统一排序策略避免死锁
// static void testLockOrderInversionAvoided() {
//     LOG_INFO << "testLockOrderInversionAvoided start";
//     Mutex a, b;
//     std::atomic<int> progress{0};
//     auto canonicalLock = [&](Mutex& x, Mutex& y){
//         Mutex* first = &x < &y ? &x : &y;
//         Mutex* second = &x < &y ? &y : &x;
//         MutexLockGuard g1(*first);
//         MutexLockGuard g2(*second);
//         ++progress;
//     };
//     std::thread t1([&]{ for(int i=0;i<2000;++i) canonicalLock(a,b); });
//     std::thread t2([&]{ for(int i=0;i<2000;++i) canonicalLock(b,a); }); // 逻辑上传入反序，内部统一排序
//     t1.join(); t2.join();
//     assert(progress.load() == 4000);
//     LOG_INFO << "testLockOrderInversionAvoided OK";
// }

// // 使用 try_lock + 退避策略获取两把锁，避免直接造成死锁
// static void testDualMutexTryLockBackoff() {
//     LOG_INFO << "testDualMutexTryLockBackoff start";
//     Mutex m1, m2;
//     std::atomic<int> successPairs{0};
//     auto worker = [&](int target){
//         int got = 0;
//         while(got < target) {
//             if(m1.try_lock()) {
//                 if(m2.try_lock()) {
//                     // 两锁都拿到
//                     ++got;
//                     m2.unlock();
//                     m1.unlock();
//                 } else {
//                     m1.unlock(); // 释放后重试
//                     std::this_thread::yield();
//                 }
//             } else {
//                 std::this_thread::yield();
//             }
//         }
//         successPairs.fetch_add(got, std::memory_order_release);
//     };
//     std::thread t1(worker, 500);
//     std::thread t2(worker, 500);
//     t1.join(); t2.join();
//     assert(successPairs.load() == 1000);
//     LOG_INFO << "testDualMutexTryLockBackoff OK";
// }

// int main() {
//     LOG_INFO << "MutexLock tests start";
//     testMutexLockBasic();
//     testMutexHolderTracking();
//     testRWMutexReadersWriters();
//     testMutexLockGuardBlocking();
//     testRWMutexGuardBlocking();
//     testConsistentLockOrderNoDeadlock();
//     testLockOrderInversionAvoided();
//     testDualMutexTryLockBackoff();
//     LOG_INFO << "MutexLock tests passed";
//     std::cout << "All MutexLock tests passed." << std::endl;
//     return 0;
// }

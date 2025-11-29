// #include "Atomic.hpp"
// #include "Logging.h"

// #include <thread>
// #include <vector>
// #include <cassert>
// #include <iostream>

// using mymoduo::AtomicInt64;
// using mymoduo::AtomicInt32;

// static void testBasicOps() {
//     AtomicInt32 a; // default 0
//     assert(a.get() == 0);
//     a.set(5);
//     assert(a.get() == 5);
//     auto old = a.getAndAdd(3); // old=5 new=8
//     assert(old == 5);
//     assert(a.get() == 8);
//     auto afterAdd = a.addAndGet(2); // new=10 returns 10
//     assert(afterAdd == 10);
//     auto oldSub = a.getAndSub(4); // old=10 new=6
//     assert(oldSub == 10);
//     assert(a.get() == 6);
//     auto afterSub = a.subAndGet(1); // new=5 returns 5
//     assert(afterSub == 5);
//     auto incOld = a.getAndIncrement(); // old=5 new=6
//     assert(incOld == 5);
//     assert(a.get() == 6);
//     auto incNew = a.incrementAndGet(); // new=7 returned 7
//     assert(incNew == 7);
//     auto decOld = a.getAndDecrement(); // old=7 new=6
//     assert(decOld == 7);
//     assert(a.get() == 6);
//     auto decNew = a.decrementAndGet(); // new=5 returned 5
//     assert(decNew == 5);
//     auto exchOld = a.getAndSet(42);
//     assert(exchOld == 5);
//     assert(a.get() == 42);
//     a += 8; // 50
//     assert(a.get() == 50);
//     a -= 20; // 30
//     assert(a.get() == 30);
// }

// static void testOperatorIncDec() {
//     AtomicInt32 b(10);
//     auto pre = ++b; // 11
//     assert(pre == 11 && b.get() == 11);
//     auto post = b++; // returns old 11 new 12
//     assert(post == 11 && b.get() == 12);
//     auto preDec = --b; // 11
//     assert(preDec == 11 && b.get() == 11);
//     auto postDec = b--; // returns old 11 new 10
//     assert(postDec == 11 && b.get() == 10);
// }

// static void testConcurrency() {
//     AtomicInt64 counter(0);
//     const int threads = 8;
//     const int iters = 100000;
//     std::vector<std::thread> v;
//     v.reserve(threads);
//     for(int t=0; t<threads; ++t) {
//         v.emplace_back([&counter, iters]{
//             for(int i=0;i<iters;++i) {
//                 counter.incrementAndGet();
//             }
//         });
//     }
//     for(auto &th : v) th.join();
//     // final value should be threads * iters
//     assert(counter.get() == static_cast<long long>(threads) * iters);
// }

// int main() {
//     LOG_INFO << "Atomic tests start";
//     testBasicOps();
//     testOperatorIncDec();
//     testConcurrency();
//     LOG_INFO << "Atomic tests passed";
//     std::cout << "All Atomic tests passed." << std::endl;
//     return 0;
// }

//就当练习吧 还是不要封装mutex了 过度封装不是好事  而且容易与std的封装在用法上冲突



// #pragma once

// #include "noncopyable.h"
// #include <mutex>
// #include <shared_mutex>
// #include <atomic>
// #include <thread>
// #include <cassert>

// // Clang 线程安全注解宏（GCC/MSVC 下为空）
// #if defined(__clang__)
// #define CAPABILITY(x) __attribute__((capability(x)))
// #define SCOPED_CAPABILITY __attribute__((scoped_lockable))
// #define ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
// #define RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
// #define ACQUIRE_SHARED(...) __attribute__((acquire_shared_capability(__VA_ARGS__)))
// #define RELEASE_SHARED(...) __attribute__((release_shared_capability(__VA_ARGS__)))
// #else
// #define CAPABILITY(x)
// #define SCOPED_CAPABILITY
// #define ACQUIRE(...)
// #define RELEASE(...)
// #define ACQUIRE_SHARED(...)
// #define RELEASE_SHARED(...)
// #define TRY_ACQUIRE(...)
// #define TRY_ACQUIRE_SHARED(...)
// #endif

// namespace mymoduo 
// {

// /**
//  * @brief 互斥锁类，基于 std::mutex，支持线程持有者追踪
//  */
// class CAPABILITY("mutex") Mutex : public noncopyable 
// {
// public:
//     using mutex_type = std::mutex;

//     Mutex() : holder_(std::thread::id{}) {}
//     ~Mutex() = default;

//     void lock() ACQUIRE() {
//         mutex_.lock();
//         assignHolder();
//     }

//     bool try_lock() TRY_ACQUIRE(true) {
//         if (mutex_.try_lock()) {
//             assignHolder();
//             return true;
//         }
//         return false;
//     }

//     void unlock() RELEASE() {
//         unassignHolder();
//         mutex_.unlock();
//     }

//     // 提供给 std::unique_lock 等标准库设施使用
//     mutex_type& native_handle() {
//         return mutex_;
//     }

//     // 断言当前线程持有锁（调试用）
//     void assertHeld() const {
//         assert(isHeldByCurrentThread());
//     }

//     // 检查当前线程是否持有锁
//     bool isHeldByCurrentThread() const {
//         return holder_.load(std::memory_order_relaxed) == std::this_thread::get_id();
//     }


//     // 允许条件变量辅助守卫访问 holder_ 状态
// private: 
//     template<typename MutexType> friend class UnassignGuard;
//     friend class MutexLockGuard;
//     void assignHolder() {
//         holder_.store(std::this_thread::get_id(), std::memory_order_relaxed);
//     }

//     void unassignHolder() {
//         holder_.store(std::thread::id{}, std::memory_order_relaxed);
//     }

// private:
//     mutable mutex_type mutex_;
//     std::atomic<std::thread::id> holder_;
// };

// /**
//  * @brief 读写锁类，基于 std::shared_mutex
//  */
// class CAPABILITY("rw_mutex") RWMutex : public noncopyable {
// public:
//     using mutex_type = std::shared_mutex;

//     RWMutex() = default;
//     ~RWMutex() = default;

//     // 写锁
//     void lock() ACQUIRE() {
//         mutex_.lock();
//     }

//     bool try_lock() TRY_ACQUIRE(true) {
//         return mutex_.try_lock();
//     }

//     void unlock() RELEASE() {
//         mutex_.unlock();
//     }

//     // 读锁
//     void lock_shared() ACQUIRE_SHARED() {
//         mutex_.lock_shared();
//     }

//     bool try_lock_shared() TRY_ACQUIRE_SHARED(true) {
//         return mutex_.try_lock_shared();
//     }

//     void unlock_shared() RELEASE_SHARED() {
//         mutex_.unlock_shared();
//     }

//     // 提供给标准库设施使用
//     mutex_type& native_handle() { return mutex_; }

// private:
//     mutable mutex_type mutex_;
// };

// // 前置声明
// class CAPABILITY("mutex") Mutex;
// class CAPABILITY("rw_mutex") RWMutex;

// /**
//  * @brief Mutex 的 RAII 守卫，使用 std::unique_lock 实现
//  * 
//  * 优点：
//  * 1. 自动处理异常安全
//  * 2. 支持延迟加锁、超时等高级特性（通过构造函数参数）
//  * 3. 代码简洁，无需手写 unlock()
//  */
// class SCOPED_CAPABILITY MutexLockGuard : public noncopyable {
// public:
//     // 立即加锁
//     explicit MutexLockGuard(Mutex& mutex) ACQUIRE(mutex) 
//         : mutex_(mutex), lock_(mutex.native_handle()) 
//         {
//             mutex_.assignHolder();
//         }

//     // 延迟加锁（需后续调用 lock()）
//     explicit MutexLockGuard(Mutex& mutex, std::defer_lock_t) 
//         : mutex_(mutex), lock_(mutex.native_handle(), std::defer_lock) {}

//     ~MutexLockGuard() RELEASE() 
//     {
//         mutex_.unassignHolder();
//         // std::unique_lock 析构时自动解锁
//         // 无需手动调用 unlock()
//     }

//     // 允许在 guard 生命周期内手动加锁/解锁
//     void lock() ACQUIRE() { lock_.lock(); }
//     void unlock() RELEASE() { lock_.unlock(); }
//     bool try_lock() TRY_ACQUIRE(true) { return lock_.try_lock(); }

//     std::unique_lock<Mutex::mutex_type>& getNativeUniqueLock() {  return lock_; }

// private:
//     Mutex& mutex_;  // 仅用于 Clang 注解关联
//     std::unique_lock<Mutex::mutex_type> lock_;
// };

// /**
//  * @brief 读锁守卫
//  */
// class SCOPED_CAPABILITY ReadLockGuard : public noncopyable {
// public:
//     explicit ReadLockGuard(RWMutex& rwmutex) ACQUIRE_SHARED(rwmutex)
//         : rwmutex_(rwmutex), lock_(rwmutex.native_handle()) {}

//     ~ReadLockGuard() RELEASE_SHARED() {
//         // std::shared_lock 析构自动解锁
//     }

// private:
//     RWMutex& rwmutex_;
//     std::shared_lock<RWMutex::mutex_type> lock_;
// };

// /**
//  * @brief 写锁守卫
//  */
// class SCOPED_CAPABILITY WriteLockGuard : public noncopyable {
// public:
//     explicit WriteLockGuard(RWMutex& rwmutex) ACQUIRE(rwmutex)
//         : rwmutex_(rwmutex), lock_(rwmutex.native_handle()) {}

//     ~WriteLockGuard() RELEASE() {
//         // std::unique_lock 析构自动解锁
//     }

// private:
//     RWMutex& rwmutex_;
//     std::unique_lock<RWMutex::mutex_type> lock_;
// };


// // 在wait() 期间会解锁 此时要改变Mutex中的 holder_ 状态, 否则assertLock会失败
// // 在wait() 返回后 又要恢复 holder_ 状态
// /**
//  * @brief wait期间释放锁 清除holder状态 wait()返回后重新获取锁 设置holder状态
//  * @tparam MutexType 互斥锁类型 Mutex 因为condition要求独占
//  */
// template<typename MutexType>
// class UnassignGuard : public noncopyable
// {
// public:
//     UnassignGuard(MutexType& mutex)
//         :mutex_(mutex)
//     {
//         mutex_.unassignHolder();
//     }    
//     ~UnassignGuard()
//     {
//         mutex_.assignHolder();
//     }
// private:
//     MutexType& mutex_;
// }; // end UnassignGuard


// }// end mymoduo





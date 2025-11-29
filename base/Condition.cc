// #include "Condition.h"

// using namespace mymoduo;
// template<typename MutexType>
// Condition<MutexType>::Condition(MutexType& mutex)
//     :mutex_(mutex),
//      cond_()
// {}

// //调用wait之前 未被守护 也未加锁
// template<typename MutexType>
// void Condition<MutexType>::wait()
// {
//     UnassignGuard<MutexType> ug(mutex_);
//     std::unique_lock<typename MutexType::mutex_type> lock(mutex_.native_handle());
//     cond_.wait(lock);
// }

// //调用wait之前 必须被守护
// template<typename MutexType>
// void Condition<MutexType>::wait(MutexLockGuard& lock)
// {
//     UnassignGuard<MutexType> ug(mutex_);
//     cond_.wait(lock.getNativeUniqueLock());
// }
// template<typename MutexType>
// void Condition<MutexType>::notify() noexcept
// {
//     cond_.notify_one();
// }
// template<typename MutexType>
// void Condition<MutexType>::notifyAll() noexcept
// {
//     cond_.notify_all();
// }

// // 显式实例化常用类型，避免模板定义分离导致链接错误
// template class mymoduo::Condition<mymoduo::Mutex>;
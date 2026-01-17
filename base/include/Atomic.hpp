// /**
//  * @file  Atomic.hpp
//  * @brief 原子整数封装，提供线程安全的整数类型操作 C++11改写为 AtomicT模板类
//  * @details 为整型 (如 int32_t, int64_t) 提供原子增减、读取等基础功能接口。
//  * @author sbilly
//  * @date   2025-11-16
//  * @version 1.0 完整实现
//  * @copyright 
//  * @license 
//  */
// #pragma once

// #include "noncopyable.h"

// #include <atomic>
// #include <cstdint>
// #include <type_traits>

// namespace mymoduo
// {
// namespace detail
// {
// /**
//  * @brief 线程安全的原子整数类型
//  * 
//  * @tparam T 整数类型 (int32_t, int64_t等)
//  */

//     template <typename T>
//     class AtomicT : noncopyable
//     {
//         static_assert(std::is_integral_v<T>, "AtomicT requires an integral type");
//     public:
//         /**
//          * @brief 默认构造函数，初始化为0
//          */
//         AtomicT() noexcept ;
//         /**
//          * @brief 用指定值构造原子整数
//          * @param value 初始值
//          */
//         inline explicit AtomicT(const T value) noexcept ;
//         // 不支持移动构造和赋值
//         AtomicT(AtomicT&&) = delete;
//         AtomicT& operator=(AtomicT&&) = delete;

//         /**
//          * @brief 原子读取当前值
//          * @return 当前值
//          */
//         inline T get() const noexcept;

//         inline operator T() const noexcept;
//         /**
//          * @brief 原子设置新值
//          * @param newValue 新值
//          */
//         inline void set(const T newValue) noexcept;

//         /**
//          * @brief 原子增加指定值
//          * @param x 增加的值
//          * @return 原始值
//          */
//         inline T getAndAdd(const T x) noexcept;

//         /**
//          * @brief 原子增加指定值
//          * @param x 增加的值
//          * @return 增加后的值
//          */
//         inline T addAndGet(const T x) noexcept;

//         /**
//          * @brief 原子减少指定值
//          * @param x 减少的值
//          * @return 原始值
//          */
//         inline T getAndSub(const T x) noexcept;
//         /**
//          * @brief 原子减少指定值
//          * @param x 减少的值
//          * @return 减少后的值
//          */
//         inline T subAndGet(const T x) noexcept;

//         /**
//          * @brief 原子+1操作
//          * @return 原始值
//          */
//         inline T getAndIncrement() noexcept;

//         /**
//          * @brief 原子+1操作
//          * @return 增加后的值
//          */
//         inline T incrementAndGet() noexcept;

//         /**
//          * @brief 原子-1操作
//          * @return 原始值
//          */
//         inline T getAndDecrement() noexcept;
//         /**
//          * @brief 原子-1操作
//          * @return 减少后的值
//          */
//         inline T decrementAndGet() noexcept;

//         /**
//          * @brief 原子设置新值，并返回旧值
//          * @param newValue 新值
//          * @return 旧值
//          */
//         inline T getAndSet(const T newValue) noexcept;

//         /**
//          * @brief 原子增加指定值
//          * @param x 增加的值
//          * @return null
//          */
//         inline void add(const T x) noexcept;

//         //重载运算符
//         inline T operator++() noexcept;        //前置++
//         inline T operator++(int) noexcept;     //后置++
//         inline T operator--() noexcept;        // 前缀--
//         inline T operator--(int) noexcept;     // 后缀--
        
//         inline bool operator == (int) noexcept;

//         inline AtomicT& operator+=(const T val);  //因为支持链式操作所以返回引用
//         inline AtomicT& operator-=(const T val);

//     private:
//         std::atomic<T> value_;
//     }; //end AtomicT
// } //end detail
//     using AtomicInt32 = detail::AtomicT<int32_t>;
//     using AtomicInt64 = detail::AtomicT<int64_t>;
// } //end mymoduo

// using namespace mymoduo::detail;

// template<typename T>
// inline AtomicT<T>::AtomicT() noexcept
//     :value_(0)
// {}

// template<typename T>
// inline AtomicT<T>::AtomicT(const T value) noexcept
//     :value_(value)
// {}

// template<typename T>
// inline T AtomicT<T>::get() const noexcept
// {
//     //acquire 获取
//     return value_.load(std::memory_order_acquire);
// }

// template<typename T>
// inline AtomicT<T>::operator T() const noexcept
// {
//     return get();
// }

// template<typename T>
// inline void AtomicT<T>::set(const T newValue) noexcept
// {
//     //release 通知
//     value_.store(newValue, std::memory_order_release);
// }

// template<typename T>
// inline T AtomicT<T>::getAndAdd(const T valAdd) noexcept
// {
//     //有write有read 使用memory_order_acq_rel
//     return value_.fetch_add(valAdd, std::memory_order_acq_rel);
// }

// template<typename T>
// inline T AtomicT<T>::addAndGet(const T valAdd) noexcept
// {
//     return value_.fetch_add(valAdd, std::memory_order_acq_rel) + valAdd;
// }

// template<typename T>
// inline T AtomicT<T>::getAndSub(const T valSub) noexcept
// {
//     return value_.fetch_sub(valSub, std::memory_order_acq_rel);
// }

// template<typename T>
// inline T AtomicT<T>::subAndGet(const T valSub) noexcept
// {
//     return value_.fetch_sub(valSub, std::memory_order_acq_rel) - valSub;
// }

// template<typename T>
// inline T AtomicT<T>::getAndIncrement() noexcept
// {
//     return getAndAdd(1);
// }

// template<typename T>
// inline T AtomicT<T>::incrementAndGet() noexcept
// {
//     return addAndGet(1);
// }

// template<typename T>
// inline T AtomicT<T>::getAndDecrement() noexcept
// {
//     return getAndSub(1);
// }

// template<typename T>
// inline T AtomicT<T>::decrementAndGet() noexcept
// {
//     return subAndGet(1);
// }

// template<typename T>
// inline T AtomicT<T>::getAndSet(const T newVal) noexcept
// {
//     return value_.exchange(newVal, std::memory_order_acq_rel);
// }

// template<typename T>
// inline void AtomicT<T>::add(const T valAdd) noexcept
// {
//     getAndAdd(valAdd);
// }

// template<typename T>
// inline T AtomicT<T>::operator++() noexcept
// {
//     return incrementAndGet();
// }

// template<typename T>
// inline T AtomicT<T>::operator++(int) noexcept
// {
//     return getAndIncrement();
// }

// template<typename T>
// inline T AtomicT<T>::operator--() noexcept
// {
//     return decrementAndGet();
// }
// template<typename T>
// inline T AtomicT<T>::operator--(int) noexcept
// {
//     return getAndDecrement();
// }

// template<typename T>
// inline AtomicT<T>& AtomicT<T>::operator+=(const T val)
// {
//     add(val);
//     return *this;
// }

// template<typename T>
// inline AtomicT<T>& AtomicT<T>::operator-=(const T val)
// {
//     add(-val);
//     return *this;
// }












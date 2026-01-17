/**
 * @file CurrentThread.h
 * @brief 获取当前线程ID的封装
 * @details 提供获取当前线程ID的函数封装，便于日志等模块 条件判断等
 * @author sbilly
 * @date   2025-11-17
 * @version 1.0 完整实现
 * @note   用c++11标准线程库实现 跨平台  不需要封装为类 避免调用时不必要的开销
 * @note   其实也可以封装为单例模式?  那就练习单例吧
 * @note   不要设计冗余 没必要返回const int, const 为多余
 */

/**
 * @version 1.1
 * @date    2025-11-18
 * @note    Added pthread ID retrieval(must be kernel tid) for isMainThread check
 */

/**
    * @version 1.2
    * @date    2025-11-19
    * @note    Changed isMainThread implementation to use stored main thread ID
    *          c++11标准库方案 with isMainThread instance
 */

// note: 在.cpp定义的函数没办法内联展开 只能在.h定义

#pragma once

#include <thread>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <iostream>

namespace mymoduo
{
namespace CurrentThread
{
namespace detail
{
class storeMainThreadId
{
public:
    storeMainThreadId()
    {
        mainthreadId_ = initMainThreadId();
    }
    ~storeMainThreadId() = default;
    static std::thread::id getMainThreadId()
    {
        return mainthreadId_;
    }
private:
    static std::thread::id initMainThreadId()
    {
        return std::this_thread::get_id();
    }
    static std::thread::id mainthreadId_;
};
// 在此处声明
extern storeMainThreadId MainThreadId;
static std::thread::id getMainThreadId()
{
    return MainThreadId.getMainThreadId();
}

class threadCache
{
public:
    threadCache();
    ~threadCache() = default;
    // inline pthread_t pthreadId() const noexcept
    // {
    //     return static_cast<int>(::syscall(SYS_gettid));
    // }
    inline std::thread::id threadId() const noexcept
    {
        return threadId_;
    }
    inline const threadCache& NativeCache()
    {
        return *this;
    }
    inline size_t tid() const noexcept
    {
        return cachedTid_;
    }
    inline const std::string& tidString() const noexcept
    {
        return stringTid_;
    }
    inline size_t tidStringLength() const noexcept
    {
        return tidStringLength_;
    }
    inline const std::string& name() const noexcept
    {
        return threadName_;
    }
    inline void setName(const std::string& name) noexcept
    {
        threadName_ = name;
    }

private:
    size_t hashThreadId(const std::thread::id& threadId);
    std::string toStringTid() const noexcept;
private:
    // pthread_t pthreadId_;
    std::thread::id threadId_;
    size_t cachedTid_;
    std::string stringTid_;
    size_t tidStringLength_;
    std::string threadName_;
}; //end threadCache
// 单一翻译单元定义的线程局部缓存实例，在CurrentThread.cc中定义。
extern thread_local threadCache g_threadCache;
} //end detail    

// 公共接口
inline detail::threadCache& getThreadCacheInstance() { return detail::g_threadCache; }

inline std::thread::id threadId() noexcept
{
    return getThreadCacheInstance().threadId();
}

inline size_t tid() noexcept
{
    return getThreadCacheInstance().tid();
}

inline const std::string& tidString() noexcept
{
    return getThreadCacheInstance().tidString();
}

inline size_t tidStringLength() noexcept
{
    return getThreadCacheInstance().tidStringLength();
}

inline const std::string& name() noexcept
{
    return getThreadCacheInstance().name();
}
inline void setName(const std::string& name)
{
    getThreadCacheInstance().setName(name);
}


inline bool isMainThread() noexcept
{
    // std::cout << "currentThread::isMainThread called\n";
    // std::cout << "currentThread: " << threadId() << ", mainThread: " << detail::getMainThreadId() << std::endl;
    return threadId() == detail::getMainThreadId(); 
}


//FIXEDME: 这里的实现没办法只依赖 c++标准库 因为没有获取进程ID的标准库接口
/* inline bool isMainThread() noexcept */
/* { */
/*     printf("CurrentThread::isMainThread called\n"); */
/*     printf("CurrentThread pthreadId: %d, getpid(): %d\n", static_cast<int>(getThreadCacheInstance().pthreadId()), static_cast<int>(::getpid())); */
/*     return static_cast<int>(getThreadCacheInstance().pthreadId()) == static_cast<int>(::getpid()); */
/* } */

} //end CurrentThread
} //end mymoduo

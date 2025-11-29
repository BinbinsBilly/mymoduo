#include "CurrentThread.h"

// 放入正确命名空间，避免生成全局符号导致链接失败
namespace mymoduo {
namespace CurrentThread {
namespace detail {

// 定义静态成员存储主线程ID
std::thread::id storeMainThreadId::mainthreadId_{};
// 全局实例（主线程ID存储）
storeMainThreadId MainThreadId{};

// 线程局部缓存单实例（与头文件 extern 对应）
thread_local threadCache g_threadCache;

} // namespace detail
} // namespace CurrentThread
} // namespace mymoduo

namespace mymoduo {
namespace CurrentThread {
namespace detail {

threadCache::threadCache()
    : threadId_(std::this_thread::get_id()),
      cachedTid_(hashThreadId(threadId_)),
      stringTid_(toStringTid()),
      tidStringLength_(static_cast<size_t>(stringTid_.size())),
      threadName_("unknown")
{
    //for test  观察在调用前被正确构造, 判断是否读取了脏数据
    // std::cout << "[threadCache Construct] kernelTid=" << static_cast<pid_t>(::syscall(SYS_gettid))
    //   << " std::thread::id=" << threadId_
    //   << " hash=" << std::hash<std::thread::id>{}(threadId_) << std::endl;
}

size_t threadCache::hashThreadId(const std::thread::id& threadId)
{
    return std::hash<std::thread::id>{}(threadId);
    //不要int截断
    // return static_cast<int>(std::hash<std::thread::id>{}(threadId));
}
std::string threadCache::toStringTid() const noexcept
{
    return std::to_string(cachedTid_);
}
} // namespace detail
} // namespace CurrentThread
} // namespace mymoduo




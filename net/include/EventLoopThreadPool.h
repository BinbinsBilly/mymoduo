/**
 * @file EventLoopThreadPool.h
 * @author sbilly
 * @brief 事件循环线程池类, 负责EventLoopThread的调度, 管理多个EventLoopThread对象, 提供多线程的EventLoop模型
 *        核心操作: 启动线程池 start(); 获取下一个EventLoop对象 getNextLoop();
 *        辅助操作: 设置线程数量 setThreadNum(); 获取所有EventLoop对象 getAllLoop();
 */
#pragma once

#include "EventLoop.h"
#include "noncopyable.h"
#include "EventLoopThread.h"

#include <vector>
#include <functional>
#include <memory>
#include <Callbacks.h>

namespace mymoduo
{
namespace net
{
class EventLoopThreadPool : noncopyable
{
public:
    EventLoopThreadPool(EventLoop* baseloop, const std::string&);
    ~EventLoopThreadPool();


    bool start(const threadInitCb cb);
    // 设置线程数量 (需在 start 前调用)
    void setThreadNum(int num) noexcept;

    //顺序方式获取EvnetLoop*
    EventLoop* getNextLoop();
    //Hash方式获取EventLoop*
    EventLoop*  getLoopForHash(size_t hashCode) const;
    //获取所有的EventLoop*
    std::vector<EventLoop*> getAllLoop() const;

    bool started() const;
    std::string_view name() const;

private:
    class Impl;  //implement 内部类隐藏实现, 外部类提供接口
    std::unique_ptr<Impl> impl_;

};// class EventLoopThreadPool
} // namespace net    
} // namespace mymoduo

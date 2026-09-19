/**
 * @brief 时间轮心跳检查机制 类似循环队列
 */

#pragma once

#include "noncopyable.h"
#include "EventLoop.h"
#include "TcpConn.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mymoduo
{
namespace net
{
class HeartBeat : noncopyable, public std::enable_shared_from_this<HeartBeat>
{
public:

    HeartBeat(EventLoop* loop, int slotsMax = 3600, double timeout = 120);
    ~HeartBeat();

    HeartBeat(const HeartBeat&) = delete;
    HeartBeat& operator=(const HeartBeat&) = delete;
    HeartBeat(HeartBeat&&) = delete;
    HeartBeat& operator=(HeartBeat&&) = delete;

    //构造完成后启动每秒tick定时器: 构造函数中weak_from_this不可用, 必须由shared_ptr管理后再调用
    void start();

    //有新连接时或读消息时更新槽位
    void update(const TcpConnPtr& newConn);
    //断开连接时清除槽位
    void remove(const TcpConnPtr& conn);

    void setRemoveConnectionCb(const std::function<void(const TcpConnPtr&)>& cb)
    {
        removeConnectionCb_ = cb;
    }

private:
    void tick();  //时间轮盘滴答一次

    EventLoop* loop_;
    const int slots_;       //槽位
    const double timeout_;  //超时检查时间
    std::vector<std::unordered_set<TcpConnPtr>> wheel_;//轮盘
    std::unordered_map<TcpConnPtr, int> conn2slots_;   //Conn 到 slot的映射

    int currentSlot_; //当前槽位

    int64_t tickTimerSeq_ = 0; //tick定时器序号, ~HeartBeat中cancel

    std::function<void(const TcpConnPtr&)> removeConnectionCb_;

};

} // namespace net
} // namespace mymoduo
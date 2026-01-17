/**
 * @brief 时间轮心跳检查机制 类似循环队列
 */

#pragma once

#include "noncopyable.h"
#include "EventLoop.h"
#include "TcpConn.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mymoduo
{
namespace net
{
class HeartBeat : noncopyable
{
public:

    HeartBeat(EventLoop* loop, int slotsMax = 3600, double timeout = 120);
    ~HeartBeat();

    HeartBeat(const HeartBeat&) = delete;
    HeartBeat& operator=(const HeartBeat&) = delete;
    HeartBeat(HeartBeat&&) = delete;
    HeartBeat& operator=(HeartBeat&&) = delete;

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

    std::function<void(const TcpConnPtr&)> removeConnectionCb_;

};

} // namespace net
} // namespace mymoduo
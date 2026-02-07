#pragma once

#include "TimeStamp.h"

#include <functional>
#include <memory>

namespace mymoduo
{
namespace net
{

using namespace mymoduo::base;

class TcpConn;
class Buffer;
class EventLoop;

using TcpConnPtr = std::shared_ptr<TcpConn>;

using TimerCb = std::function<void()>;

using threadInitCb = std::function<void(mymoduo::net::EventLoop*)>;

using TcpConnetionCb = std::function<void(const TcpConnPtr&)>;
using TcpMessageCb = std::function<void(const TcpConnPtr&, Buffer*, mymoduo::base::TimeStamp)>;
using TcpWriteCompleteCb = std::function<void(const TcpConnPtr&)>;
using HighWaterMarkCb = std::function<void(const TcpConnPtr&, size_t)>;
using TcpHandleCloseCb = std::function<void(const TcpConnPtr&)>;
using HeartBeatUpdateCb = std::function<void(const TcpConnPtr&)>;
using HeartBeatRemoveCb = std::function<void(const TcpConnPtr&)>;

} // namespace net
} // namespace mymoduo

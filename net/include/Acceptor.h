/**
 * @file Acceptor.h
 * @author sbilly
 * @brief 连接器类:  有独有的linstenfd,  负责监听新事件连接, 并通过回调函数分发新连接到对应的EventLoop 
 */

#pragma once

#include "socket.h"
#include "Channel.h"
#include "InetAddress.hpp"
#include "noncopyable.h"
#include "EventLoop.h"

#include <functional>
#include <fcntl.h>

namespace mymoduo
{
namespace net
{

using NewConnCb = std::function<void(Socket&&, const InetAddress&)>;

class Acceptor : noncopyable
{
public:
    Acceptor(EventLoop* loop, const InetAddress& linstenAddr, bool reusePort);
    ~Acceptor();

    void setNewConnCb(const NewConnCb cb) { cb_ = cb; }
    constexpr bool isListenning() { return listenning_; }
    
    void listen();
    void handleRead();

private:
    EventLoop* loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    int idleFd_;
    bool listenning_;
    NewConnCb cb_;
    
}; // class Acceptor
} // namespace net
} // namespace mymoduo
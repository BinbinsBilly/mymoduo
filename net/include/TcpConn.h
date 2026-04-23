/**
 * @file TcpConn.h
 * @author sbilly
 * @brief TcpConn类: 掌管着一个TCP连接的所有信息和操作 所有的收发操作都通过TcpConn来完成
 *
 */
#pragma once

#include "InetAddress.hpp"
#include "EventLoop.h"
#include "socket.h"
#include "noncopyable.h"
#include <functional>
#include "Callbacks.h"
#include "Buffer.h"
#include "TimeStamp.h"

namespace mymoduo
{
namespace net
{


class TcpConn : noncopyable, public std::enable_shared_from_this<TcpConn>
{
public:
    enum class StateE : char
    {
        kConnecting,
        kConnected,
        kDisconnecting,
        kDisconnected
    };

    TcpConn(EventLoop* loop,
            const std::string& name,
            Socket&& connSocket,
            const InetAddress& loaclAddr,
            const InetAddress& peerAddr);
    ~TcpConn();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    InetAddress localAddress() const { return localAddr_; }
    InetAddress peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == StateE::kConnected; }

    void send(const void* data, size_t len);
    void send(const std::string& msg);

    //关闭与客户端的连接
    void shutdown();

    void setConnectionCb(const TcpConnetionCb& cb) { connectionCb_ = std::move(cb); }
    void setMessageCb(const TcpMessageCb& cb) { messageCb_ = std::move(cb); }
    void setWriteCompleteCb(const TcpWriteCompleteCb& cb) { writeCompleteCb_ = std::move(cb); }
    void setHighWaterMarkCb(const HighWaterMarkCb& cb, size_t highWaterMark)
    {
        highWaterMarkCb_ = std::move(cb);
        highWaterMark_ = highWaterMark;
    }
    void setHeartBeatUpdateCb(const HeartBeatUpdateCb& cb)
    {
        heartBeatUpdateCb_ = std::move(cb);
    }
    void setHeartBeatRemoveCb(const HeartBeatRemoveCb& cb)
    {
        heartBeatRemoveCb_ = std::move(cb);
    }

    void setHandleCloseCb(const TcpHandleCloseCb& cb) { handleCloseCb_ = std::move(cb); }

    void connectEstablished(); //与客户端连接已经建立完成 开启channel
    void connectDestroyed();   //关闭与客户端的连接后 清理channel等资源

private:
    void handleRead(base::TimeStamp recvTime);
    void handleWrite();
    void handleClose();
    void handleError();
    void handleHighWaterMark(size_t len);

    void sendInLoop(const void* data, size_t len);
    void shutdownInLoop();
    void setState(StateE s) { state_ = s; }

    EventLoop* loop_; //这个肯定不是baseLoop(如果有subloop的话)

    std::atomic<StateE> state_;
    
    const std::string ipPort_;
    const std::string name_;
    
    std::unique_ptr<Socket> connSocket_;
    std::unique_ptr<Channel> connChannel_;

    InetAddress localAddr_;
    InetAddress peerAddr_;

    TcpConnetionCb connectionCb_;
    TcpMessageCb messageCb_;
    TcpWriteCompleteCb writeCompleteCb_;
    TcpHandleCloseCb handleCloseCb_;
    HighWaterMarkCb highWaterMarkCb_;
    HeartBeatUpdateCb heartBeatUpdateCb_;
    HeartBeatRemoveCb heartBeatRemoveCb_;
    
    size_t highWaterMark_;

    base::TimeStamp lastHeartBeatUpdateTime_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;

}; // forward declaration
} // end net
} // end mymoduo

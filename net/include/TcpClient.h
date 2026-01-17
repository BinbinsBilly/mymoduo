#pragma once

#include "noncopyable.h"
#include "Connector.h"

#include <optional>
#include <shared_mutex>

namespace mymoduo
{
namespace net
{
class TcpClient
{
public:
    TcpClient(EventLoop* loop, const InetAddress& serverAddr, const std::string name = "TcpClient");
    ~TcpClient();

    void start();   //开始连接
    void stop();   //关闭连接

    //连接建立后 关闭与server的连接
    void disconnect();

    //设置回调
    void setConnetionCb(TcpConnetionCb cb) { connectionCb_ = std::move(cb); }
    void setMessageCb(TcpMessageCb cb) { messageCb_ = std::move(cb); }
    void setWriteCompleteCb(TcpWriteCompleteCb cb) { writeCompleteCb_ = std::move(cb); } 
    
    bool isConnected() const;

    void enableRetry() { retry_ = true; }

private:
    void connection(int sockfd);
    void removeConnection();

    EventLoop* loop_;
    InetAddress serverAddr_;
    const std::string name_;
    
    std::atomic<bool> connect_;
    std::atomic<bool> retry_;

    std::shared_ptr<Connector> connector_;
    
    std::optional<TcpConnetionCb> connectionCb_;
    std::optional<TcpMessageCb> messageCb_;
    std::optional<TcpWriteCompleteCb> writeCompleteCb_;

    mutable std::shared_mutex shared_mutex_;
    TcpConnPtr tcpConnetion_; //GUARDBY mutex;:

}; // end class TcpClient

}// end mymoduo
}// end net

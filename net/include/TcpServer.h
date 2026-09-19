#pragma once

#include "EventLoop.h"
#include "noncopyable.h"
#include "TcpConn.h"
#include "EventLoopThreadPool.h"
#include "Acceptor.h"
#include "Callbacks.h"
#include "Logging.h"
#include "HeartBeat.h"


namespace mymoduo
{
namespace net
{
class TcpServer : noncopyable
{
using ConnectionMap = std::unordered_map<std::string, std::shared_ptr<TcpConn>>;

public:
    enum class Option : bool
    {
        kNoReuse = false,
        kReusePort = true
    };


    TcpServer(EventLoop* loop, const InetAddress& linstenaddr, const std::string &name, int slots = 3600, double timeout = 120,
             Option opt = Option::kNoReuse);
    ~TcpServer();

    void setThreadNum(int numThreads);
    void setThreadInitCb(const threadInitCb& cb) { threadInitCb_ = std::move(cb); }
    std::shared_ptr<EventLoopThreadPool> threadPool() const { return threadPool_; }

    //这几个函数都属用户态回调函数
    void setConnectionCb(const TcpConnetionCb& cb) { connectionCb_ = std::move(cb); }
    void setMessageCb(const TcpMessageCb& cb) { messageCb_ = std::move(cb); }
    void setWriteCompleteCb(const TcpWriteCompleteCb& cb) { writeCompleteCb_ = std::move(cb); }
    void setHighWaterMarkCb(const HighWaterMarkCb& cb) { highWaterMarkCb_ = std::move(cb); }

    //这个函数与用户态回调函数不同 属于连接关闭时做清理操作的回调函数 因为TcpConn属于TcpServer, 所以必须由TcpServer来清理
    void setHandleCloseCb(const TcpHandleCloseCb& cb) { handleCloseCb_ = std::move(cb); }

    void start();

private:
    void newConnection(Socket&& connSocket, const InetAddress& peerAddr);
    void removeConnection(const TcpConnPtr& conn);
    void removeConnectionInLoop(const TcpConnPtr& conn);

    EventLoop* loop_;   //base loop, acceptor loop

    const std::string ipPort_;  //ip监听地址
    const std::string name_;    //服务器名称

    std::unique_ptr<Acceptor> acceptor_;  //for listenning new connection

    //心跳检测
    //析构时所有权移交 baseLoop(见 ~TcpServer): ~HeartBeat 需 cancel tick 定时器, 须在 loop 线程内析构;
    //TcpConn 侧回调持有 weak_ptr, HeartBeat 析构后残留回调自动变为 no-op
    std::shared_ptr<HeartBeat> heartBeat_;

    std::shared_ptr<EventLoopThreadPool> threadPool_;

    TcpConnetionCb connectionCb_;
    TcpMessageCb messageCb_;
    TcpWriteCompleteCb writeCompleteCb_;
    HighWaterMarkCb highWaterMarkCb_;

    TcpHandleCloseCb handleCloseCb_;

    threadInitCb threadInitCb_;

    std::atomic<int32_t> started_;

    int nextConnId_;

    ConnectionMap connectionMap_;
};

}// end net
}// end mymoduo
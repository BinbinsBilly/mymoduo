#include "TcpServer.h"

template<typename T>
T* CHECK_NOT_NULL(T* ptr)
{
    if(ptr)
    {
        return ptr;
    }
    LOG_FATAL << "TcpServer::CHECK_NOT_NULL - ptr is nullptr";
    return nullptr;
}
namespace mymoduo
{
namespace net
{
    TcpServer::TcpServer(EventLoop* loop, const InetAddress& localAddr, const std::string& name,
                         int slots, double timeout, Option opt)
        :loop_(CHECK_NOT_NULL(loop))
        ,ipPort_(std::string(localAddr.toIpPort()))
        ,name_(name)
        ,acceptor_(std::make_unique<Acceptor>(loop, localAddr, opt == Option::kReusePort))
        ,threadPool_(std::make_shared<EventLoopThreadPool>(loop, name_))
        ,started_(0)
        ,nextConnId_(1)
        ,heartBeat_(std::make_unique<HeartBeat>(loop, slots, timeout))
    {
        acceptor_->setNewConnCb([this](Socket&& s, const InetAddress& peer){ this->newConnection(std::move(s), peer); });
        heartBeat_->setRemoveConnectionCb([this](const TcpConnPtr& conn){ this->removeConnection(conn); });
    }

    TcpServer::~TcpServer()
    {
        /*这里用reset 的原因:
        1. TcpServer析构时, 说明程序要退出了, 需要把所有的连接都断开
        2. 不能用erase 因为在for(:)中erase会导致迭代器失效
        3. 使用reset后, 智能指针引用计数减1, 当引用计数为0时, TcpConn对象会自动析构,但析构前还要执行清理操作
           所以用局部变量保留引用, 当离开当前作用域时析构TcpConn对象
           并且使用lambda捕获conn, 能保证conn的生命周期不会早于ioLoop的任务队列执行完成
        */
        loop_->assertInLoopThread();
        LOG_INFO << "TcpServer::~TcpServer - server " << name_ << " destructing";
        for(auto& item : connectionMap_)
        {
            TcpConnPtr conn = item.second;  // 将类成员中的智能指针转移为局部变量的智能指针
            item.second.reset();          // 重置类成员中的智能指针，释放对连接对象的所有权
            //conn所属的ioLoop中执行连接销毁
            conn->getLoop()->runInLoop([conn](){
                conn->connectDestroyed();
            });
        } 
    }

void TcpServer::setThreadNum(int numSubThreads)
{
    threadPool_->setThreadNum(numSubThreads);
}

void TcpServer::start()
{
    LOG_DEBUG << "TcpServer::start - server " << name_ << " starting";
    if(started_++ == 0)
    {
        threadPool_->start(threadInitCb_);  //启动底层线程池
        loop_->runInLoop([this](){         //开启监听新连接
            acceptor_->listen();
        });
    }
    LOG_DEBUG << "successfully started TcpServer: " << name_ ;
}

void TcpServer::newConnection(Socket&& connSocket, const InetAddress& peerAddr)
{
    loop_->assertInLoopThread();
    EventLoop* ioLoop = threadPool_->getNextLoop();
    char buf[64]{0};
    ::snprintf(buf, sizeof(buf), "%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    LOG_INFO << "add Connection, conn name = " << connName << "Port = " << ipPort_ ;
    

    sockaddr_in local;
    memset(&local, 0, sizeof(local));    
    socklen_t socklen = sizeof(local);
    if(::getsockname(connSocket.fd(), (sockaddr*)&local, &socklen) < 0)
    {
        LOG_ERROR << "::getsockname()";
    }
    InetAddress localaddr(local);

    TcpConnPtr newconn= std::make_shared<TcpConn>( ioLoop,
                                                  connName,
                                                  std::move(connSocket),
                                                  localaddr,
                                                  peerAddr);
    connectionMap_[connName] = newconn;

    //用户态回调函数的设置
    newconn->setConnectionCb(connectionCb_);
    newconn->setMessageCb(messageCb_);
    newconn->setWriteCompleteCb(writeCompleteCb_);
    newconn->setHeartBeatUpdateCb([this](const TcpConnPtr& conn){
        this->heartBeat_->update(conn);
    });
    newconn->setHeartBeatRemoveCb([this](const TcpConnPtr& conn){
        this->heartBeat_->remove(conn);
    });
    
    // 连接关闭的回调, 由TcpServer来执行清理工作
    newconn->setHandleCloseCb([this](const TcpConnPtr& conn){
        this->removeConnection(conn);
    });

    //连接建立
    ioLoop->runInLoop([newconn](){
        newconn->connectEstablished();
    });
    //添加心跳检测 在TcpConn中进行
}

void TcpServer::removeConnection(const TcpConnPtr& conn)
{
    //移除心跳检测  在tcpConn中进行
    //转到baseLoop_中执行
    loop_->runInLoop([this, conn](){
        this->removeConnectionInLoop(conn);
    });
}

void TcpServer::removeConnectionInLoop(const TcpConnPtr& conn)
{
    //在管理线程baseLoop_中执行
    loop_->assertInLoopThread();
    LOG_INFO << "TcpServer: removeConnection, name = " << conn->name();
    size_t n = connectionMap_.erase(conn->name());
    (void)n;

    //在conn所属的ioLoop中销毁连接
    //如何选中conn所属的ioLoop???
    //TcpConn保存了自己所属的EventLoop
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn](){
        conn->connectDestroyed();
    });
}


} // namespace net
} // namespace mymoduo
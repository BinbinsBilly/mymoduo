#include "TcpClient.h"
#include "TcpConn.h"

namespace mymoduo
{
namespace net
{
namespace detail
{

} //end namespace detail

TcpClient::TcpClient(EventLoop* loop, const InetAddress& serverAddr, const std::string name)
    : loop_(loop)
     ,serverAddr_(serverAddr)
     ,name_(name)
     ,connect_(false) 
     ,retry_(false)
     ,connector_(std::make_shared<Connector>(loop, serverAddr))
     ,tcpConnetion_(nullptr)
    {
        connector_->setConnetionCb([this](int sockfd) { this->connection(sockfd); });
    }

TcpClient::~TcpClient()
{
    LOG_DEBUG << "TcpClient::~TcpClient - TcpClient destructing";
    TcpConnPtr conn{nullptr};
    {
        std::unique_lock lock(shared_mutex_);
        conn = std::move(tcpConnetion_);
    }
    LOG_DEBUG << "std::move(tcpConnetion_) done in ~TcpClient";
    if(conn)
    {
        EventLoop* ioLoop = conn->getLoop();
        ioLoop->runInLoop([conn](){
            conn->connectDestroyed();
        });
        LOG_DEBUG << "TcpConn destroyed in ~TcpClient";
    }else{
        LOG_DEBUG << "no TcpConn in ~TcpClient";
        connector_->stop();
    }
}

bool TcpClient::isConnected() const
{
    std::shared_lock lock(shared_mutex_);
    return tcpConnetion_ && tcpConnetion_->connected();
}

void TcpClient:: start()
{
    if(!connect_.load(std::memory_order_acquire))
    {
        connect_ = true;
        connector_->start();//开始连接到server
    }else
    {
        LOG_ERROR << "already connecting, connect_ != false";
    }
}

//发起连接请求期间 关闭连接请求
void TcpClient::stop()
{
    if(connect_.load(std::memory_order_acquire))
    {
        connect_ = false;
        retry_ = false; //停止重试
        connector_->stop(); //停止连接  如何retry_ == false 则不会重试
        
        //这里只是关闭连接请求, 若retry_ == true 会重试.  因此不能关闭connection
        //因此在removeConnection回调时若重新建立了连接请求, removeConnetion退出时,
        //event!=KNoneEvent, 会断言失败
        //关闭connection要调用disconnect
    }
}

void TcpClient::disconnect()
{
    retry_ = false; //关闭重试
    {
        std::shared_lock lock(shared_mutex_);
        if(tcpConnetion_)
        {
            //关闭写端
            tcpConnetion_->shutdown(); 
            //关闭写端后就会调用 handleClose 进而调用removeConnection
        }
    }
}

void TcpClient::removeConnection()
{
    TcpConnPtr conntemp;
    {
        //这里不用assert
        //因为TcpConn不属于某个loop, 只是TcpConn中的channel属于某个loop
        std::unique_lock lock(shared_mutex_);
        if(tcpConnetion_)
        {
            conntemp = std::move(tcpConnetion_);
            if(tcpConnetion_)
            {
                tcpConnetion_.reset();
            }
        }
    }
    // Only schedule connectDestroyed if we actually have a connection.
    if(conntemp)
    {
        // capture shared_ptr to keep object alive until invoked in loop thread
        loop_->queueInLoop([conntemp](){
            LOG_DEBUG << "removeConnection queued for TcpConn ptr=" << conntemp.get() << " use_count=" << conntemp.use_count();
            conntemp->connectDestroyed();
        });
    }
    if(retry_.load(std::memory_order_acquire))
    {
        LOG_INFO << "TcpClient::removeConnection - Reconnecting to "
                 << serverAddr_.toIpPort();
        connector_->restart();
    }
}

void TcpClient::connection(int sockfd)
{
    //set connetion name
    std::string name;
    Socket sock(sockfd);
    if(!sock.isvalid())
    {
        LOG_ERROR << "TcpClient::connection - invalid socket";
        return;
    }
    std::variant<struct sockaddr_in, struct sockaddr_in6> localaddr = Socket::getLocalAddrVariant(sockfd);
    InetAddress localInet;
    std::visit([&name, &localInet, this](auto& addr){
        using T = std::decay_t<decltype(addr)>;
        if constexpr (std::is_same_v<T, struct sockaddr_in>)
        {
            name = name_ + " - " + std::to_string(ntohs(addr.sin_port));
            localInet = InetAddress(addr);
        } else if constexpr (std::is_same_v<T, struct sockaddr_in6>)
        {
            name = name_ + " - " + std::to_string(ntohs(addr.sin6_port));
            localInet = InetAddress(addr);
        }
    }, localaddr);
    // if(std::holds_alternative<struct sockaddr_in>(localaddr))
    // {
    //     name = name_ + " - " + std::to_string(std::get<struct
    //                 sockaddr_in>(localaddr).sin_port);
    // }else{
    //     name = name_ + " - " + std::to_string(std::get<struct
    //                 sockaddr_in6>(localaddr).sin6_port);
    // }
    TcpConnPtr conn = std::make_shared<TcpConn>(loop_, name, std::move(sock), localInet, serverAddr_);
    if(messageCb_)
    {
        conn->setMessageCb(messageCb_.value());
    }
    if(connectionCb_)
    {
        conn->setConnectionCb(connectionCb_.value());
    }
    if(writeCompleteCb_)
    {
        conn->setWriteCompleteCb(writeCompleteCb_.value());
    }
    conn->setHandleCloseCb([this](const TcpConnPtr&){
        this->removeConnection();
    });
    {
        std::unique_lock lock(shared_mutex_);
        tcpConnetion_ = std::move(conn);
    }
    tcpConnetion_->connectEstablished(); //连接建立
    LOG_DEBUG << "TcpClient::connection - TcpConn established, name: " << tcpConnetion_->name()
              << ", localAddr: " << tcpConnetion_->localAddress().toIpPort()
              << ", peerAddr: " << tcpConnetion_->peerAddress().toIpPort();
}


}//end mymoduo
}//end net

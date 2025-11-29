#include "TcpConn.h"
#include "Logging.h"

namespace mymoduo :: net
{
    TcpConn::TcpConn(EventLoop* loop,
        const std::string& name,
        Socket&& connSocket,
        const InetAddress& localAddr,
        const InetAddress& peerAddr)
        :loop_(loop)
        ,state_(StateE::kConnecting)
        ,name_(name)
        ,connSocket_(std::make_unique<Socket>(std::move(connSocket)))
        ,connChannel_(std::make_unique<Channel>(loop, connSocket_->fd()))
        ,localAddr_(localAddr)
        ,peerAddr_(peerAddr)
        ,highWaterMark_(64 * 1024 * 1024) //默认64MB
        ,inputBuffer_(64 * 1024)  //64KB
        ,outputBuffer_(64 * 1024) //64KB
    {
        connSocket_->setReuseAddr(ReuseAddr::ENABLE);
        connSocket_->setReusePort(ReusePort::ENABLE);
        connSocket_->setKeepAlive(KeepAlive::ENABLE);
        connSocket_->bindaddress(localAddr);

        connChannel_->setReadEventCb([this](mymoduo::base::TimeStamp timestamp){ this->handleRead(timestamp); });
        connChannel_->setWriteEventCb([this](){ this->handleWrite(); });
        connChannel_->setCloseEventCb([this](){ this->handleClose(); });
        connChannel_->setErrorEventCb([this](){ this->handleError(); });

        LOG_INFO << "TcpConn::TcpConn - new connection [" << name_
                 << "] from " << peerAddr_.toIpPort()
                 << " to " << localAddr_.toIpPort();
    }
    TcpConn::~TcpConn()
    {
        LOG_INFO << "TcpConn::~TcpConn - connection [" << name_
                 << "] from " << peerAddr_.toIpPort()
                 << " to " << localAddr_.toIpPort()
                 << " is down";
    }


    void TcpConn::connectEstablished()
    {
        setState(StateE::kConnected);
        connChannel_->enableRead();
        connChannel_->tie(shared_from_this());
        if(connectionCb_)
        {
            connectionCb_(shared_from_this());
        }
    }

    //test
    void TcpConn::connectDestroyed()
    {
        LOG_DEBUG << "TcpConn::connectDestroyed - connection";
        // Ensure channel is disabled and removed regardless of previous state.
        if(state_ != StateE::kDisconnected)
        {
            setState(StateE::kDisconnected);
        }
        if(connChannel_)
        {
            connChannel_->disableAll();  // 清理工作: 取消对 fd 的所有事件
            LOG_DEBUG << "TcpConnection channel - disableAll called";
            if(connectionCb_)
            {
                connectionCb_(shared_from_this());
            }
            connChannel_->remove();
            connChannel_.reset();
        }
        LOG_DEBUG << "TcpConnChannel removed from loop";
    }

    void TcpConn::send(const void* data, size_t len)
    {
        const std::string* msg = static_cast<const std::string*>(data);
        if(state_ == StateE::kConnected)
        {
            if(loop_->isInLoopThread())
            {
                sendInLoop(data, len);
            }else{
                loop_->queueInLoop([this, msg, len](){
                    this->sendInLoop(msg->c_str(), len);
                });
            }
        }
    }
    void TcpConn::send(const std::string& msg)
    {
        if(state_ == StateE::kConnected)
        {
            if(loop_ -> isInLoopThread())
            {
                sendInLoop(msg.c_str(), msg.size());
            }else{
                loop_->queueInLoop([this, msg](){
                    sendInLoop(msg.c_str(), msg.size());
                });
            }
        }
    }

    void TcpConn::shutdown()
    {
        if(state_ == StateE::kConnected)
        {
            setState(StateE::kDisconnecting);
            loop_->queueInLoop([this](){
                this->shutdownInLoop();
            });
        }
    }

    void TcpConn::shutdownInLoop()
    {
        if(!connChannel_->isWriting()) //outputBuffer_中无数据待发送
        {
            connSocket_->shutdownWrite();
        }else{
            LOG_INFO << "TcpConn::shutdownInLoop - fd = " << connChannel_->fd()
                     << " outputBuffer_ has data to be sent, please wait writing complete";
        }
    }

    void TcpConn::handleRead(base::TimeStamp recvTime)
    {
        int savederror = 0;
        ssize_t n = inputBuffer_.readFd(connChannel_->fd(), &savederror);
        if(n > 0)
        {
            //用户态的回调
            if(messageCb_)
            {
                messageCb_(shared_from_this(), &inputBuffer_, recvTime);
            }
        }else if(n == 0)
        {
            //连接关闭, 我们要做的清理工作
            handleClose();
        }else
        {
            errno = savederror;
            LOG_ERROR << "TcpConn::handleRead - read error: " << strerror(savederror);
            handleError();
        }
    }
    void TcpConn::handleWrite()
    {
        if(connChannel_->isWriting())
        {
            int savederror = 0;
            ssize_t n = outputBuffer_.writeFd(connChannel_->fd(), &savederror);
            if(n > 0)
            {
                outputBuffer_.retrieve(n);
                if(outputBuffer_.readableBytes() == 0) //buff缓冲区中无数据, 发送完毕
                {
                    connChannel_->disableWrite();
                    if(writeCompleteCb_)
                    {

                        //这里能否再起一个线程执行复杂的回调函数?
                        //在加入一个线程池专门处理回调函数?

                        //这里其实可以直接调回调了
                        //但可能出于运行效率考虑, 还是放到pendingFunctors中异步执行
                        decltype(auto) self = shared_from_this();
                        loop_->queueInLoop([self, this](){
                            writeCompleteCb_(self);
                        });
                    }
                }
                //数据发送完毕后, 如果当前状态正在关闭连接, 则关闭写端
                //对端被通知可读事件, ERRNO = 0, read返回0(EOF事件), handleClose()
                if(state_ == StateE::kDisconnecting)
                {
                    shutdownInLoop();
                }
            }else{
                LOG_ERROR << "tcpConn handleWrite() n <= 0"<< strerror(savederror);
            }
        }else{
            LOG_ERROR << "tcpConn fd = " << connChannel_->fd()<< "no more accept writing";
        }
    }

    void TcpConn::handleClose()
    {
        LOG_INFO << "tcpConn fd = " << connChannel_->fd() ;
        setState(StateE::kDisconnected);
        connChannel_->disableAll();
        decltype(auto) guardThis = shared_from_this();
        if (connectionCb_) {
            connectionCb_(guardThis);
        }
        if (handleCloseCb_) {
            handleCloseCb_(guardThis);
        }
    }

    void TcpConn::handleError()
    {
        int err = connSocket_->getSocketError();
        LOG_ERROR << "TcpConn::handleError - SO_ERROR = " << err << " " << strerror(err);
    }

    void TcpConn::handleHighWaterMark(size_t len){
        if(highWaterMarkCb_)
        {
            decltype(auto) guardThis = shared_from_this();
            loop_->queueInLoop([this, guardThis, len](){
                highWaterMarkCb_(guardThis, len);
            });
        }
    }

    void TcpConn::sendInLoop(const void*data, size_t len)
    {
        ssize_t nwrote{0};
        size_t remaining{len};
        int faultError{0};

        if(state_ == StateE::kDisconnecting || (state_ == StateE::kDisconnected))
        {
            LOG_ERROR << "disconnecting or disconneted";
        }
        if(!connChannel_->isWriting() || outputBuffer_.readableBytes() == 0 || remaining == 0)
        {
            LOG_ERROR << "nothing to write, readable bytes = 0";
        }else{
            nwrote = ::write(connChannel_->fd(), data, len);
            if(nwrote >= 0) //理论上来说不会等于0
            {
                //记录还有多少未写入的数据
                remaining -= nwrote;
                if(remaining == 0 && writeCompleteCb_)
                {
                    decltype(auto) guardThis = shared_from_this();
                    loop_->queueInLoop([this, guardThis](){
                        this->writeCompleteCb_(guardThis);
                    });
                }
            }else{ // nwrote < 0
                //注意写入部分的情况不会返回0也不会设置errno
                //只有当下次进入时才会返回-1并设置errno为EAGAIN
                nwrote = 0;
                faultError = errno;
                if(faultError != EAGAIN && faultError != EINTR)  //n < 0 && errno == EAGAIN：缓冲区满
                {
                    //真错误
                    LOG_ERROR << "TcpConn::sendInLoop - write error: " << strerror(faultError);
                    if(faultError == EPIPE || faultError == ECONNRESET)
                    {
                        //对端关闭连接
                        handleClose();
                    }
                }else{
                    //缓冲区满了  或   被信号中断
                    LOG_INFO << "TcpConn::sendInLoop - write later";
                    faultError = 0; //不是错误
                }
            }
        }
        //剩余数据添加到outputBuffer_中
        if(remaining > 0 && faultError == 0)
        {
            size_t oldLen = outputBuffer_.readableBytes();
            if(oldLen + remaining > highWaterMark_
                && oldLen < highWaterMark_   //说明上次已经调用过了
                && highWaterMarkCb_)
            {
                size_t newLen = oldLen + remaining;
                decltype(auto) guardThis = shared_from_this();
                loop_->queueInLoop([this, guardThis, newLen](){
                    highWaterMarkCb_(guardThis, newLen);
                });
            }
            // 剩余数据添加到outputBuffer_中 等待下一次可写事件(可写缓冲区0->1)触发再写入内核缓冲区
            outputBuffer_.append(static_cast<const char*>(data) + nwrote, remaining);
            if(!connChannel_->isWriting())
            {
                connChannel_->enableWrite();
            }
        }
    }

} //end namespace mymoduo::net
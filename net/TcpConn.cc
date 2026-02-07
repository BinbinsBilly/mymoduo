#include "TcpConn.h"
#include "Logging.h"

namespace mymoduo :: net
{
    TcpConn::TcpConn(EventLoop* loop,
        const std::string& name,
        Socket&& connSocket,
        const InetAddress& localAddr,
        const InetAddress& peerAddr)
        :loop_(loop)//该Conn属于的io线程
        ,state_(StateE::kConnecting)
        ,name_(name)
        ,connSocket_(std::make_unique<Socket>(std::move(connSocket)))
        ,connChannel_(std::make_unique<Channel>(loop, connSocket_->fd()))
        ,localAddr_(localAddr)
        ,peerAddr_(peerAddr)
        ,highWaterMark_(64 * 1024 * 1024)   //默认64MB
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
        if(heartBeatUpdateCb_)
        {
            heartBeatUpdateCb_(shared_from_this());
        }
    }

    void TcpConn::connectDestroyed()
    {
        LOG_DEBUG << "TcpConn::connectDestroyed - connection";
        // Ensure channel is disabled and removed regardless of previous state.
            bool wasConnected = (state_ == StateE::kConnected);
            if(state_ != StateE::kDisconnected)
            {
                setState(StateE::kDisconnected);
            }
        if(connChannel_)
        {
            if(!connChannel_->isNoneEvent())
            {
                connChannel_->disableAll();  // 清理工作: 取消对 fd 的所有事件
            }
            LOG_DEBUG << "TcpConnection channel - disableAll called";
            connChannel_->remove();
            connChannel_.reset();
        }
            if(wasConnected)
            {
                if(connectionCb_)
                {
                    connectionCb_(shared_from_this());
                }
                if(heartBeatRemoveCb_)
                {
                    heartBeatRemoveCb_(shared_from_this());
                }
            }
        LOG_DEBUG << "TcpConnChannel removed from loop";
        //for test
    }

    void TcpConn::send(const void* data, size_t len)
    {
            if(state_ == StateE::kConnected)
            {
                if(loop_->isInLoopThread())
                {
                    sendInLoop(data, len);
                }else{
                    auto self = shared_from_this();
                    auto message = std::make_shared<std::string>(
                        static_cast<const char*>(data),
                        len
                    );
                    loop_->queueInLoop([self, message](){
                        self->sendInLoop(message->data(), message->size());
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
                auto self = shared_from_this();
                loop_->queueInLoop([self, msg](){
                    self->sendInLoop(msg.c_str(), msg.size());
                });
            }
        }
    }

    void TcpConn::shutdown()
    {
        if(state_ == StateE::kConnected)
        {
            setState(StateE::kDisconnecting);
            auto self = shared_from_this();
            loop_->queueInLoop([self](){
                self->shutdownInLoop();
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
            if(heartBeatUpdateCb_)
            {
                heartBeatUpdateCb_(shared_from_this());
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
                        auto self = shared_from_this();
                        loop_->queueInLoop([self](){
                            if(self->writeCompleteCb_) {
                                self->writeCompleteCb_(self);
                            }
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
        if(heartBeatRemoveCb_)
        {
            heartBeatRemoveCb_(guardThis);
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

        if(state_ == StateE::kDisconnected)
        {
            LOG_ERROR << "sendInLoop on disconnected connection";
            return;
        }
        if(!connChannel_)
        {
            LOG_ERROR << "sendInLoop without channel";
            return;
        }
        if(remaining == 0)
        {
            return;
        }

        if(!connChannel_->isWriting() && outputBuffer_.readableBytes() == 0)
        {
            nwrote = ::write(connChannel_->fd(), data, len);
            if(nwrote >= 0) //理论上来说不会等于0
            {
                remaining -= nwrote;
                if(remaining == 0 && writeCompleteCb_)
                {
                    auto guardThis = shared_from_this();
                    loop_->queueInLoop([guardThis](){
                        if(guardThis->writeCompleteCb_) {
                            guardThis->writeCompleteCb_(guardThis);
                        }
                    });
                }
            }else{ // nwrote < 0
                nwrote = 0;
                faultError = errno;
                if(faultError != EAGAIN && faultError != EINTR)
                {
                    LOG_ERROR << "TcpConn::sendInLoop - write error: " << strerror(faultError);
                    if(faultError == EPIPE || faultError == ECONNRESET)
                    {
                        handleClose();
                    }
                }else{
                    LOG_INFO << "TcpConn::sendInLoop - write later";
                    faultError = 0;
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
                auto guardThis = shared_from_this();
                loop_->queueInLoop([guardThis, newLen](){
                    if(guardThis->highWaterMarkCb_) {
                        guardThis->highWaterMarkCb_(guardThis, newLen);
                    }
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
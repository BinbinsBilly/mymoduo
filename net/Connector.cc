#include "Connector.h"

namespace mymoduo
{
namespace net
{

std::optional<int> createSocketNBandCE(int family)
{
    int ret = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(ret < 0)
    {
        LOG_ERROR << "createSocketNBandCE - socket error: " << errno;
        return std::nullopt;
    }
    return ret;
}

int sockconnect(int sockfd, const InetAddress& serverAddr)
{
    return ::connect(sockfd, serverAddr.getSockAddr(),
                    serverAddr.family() == AF_INET ? sizeof(struct sockaddr_in) :
                    sizeof(struct sockaddr_in6));
}

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr, int retryDelayMs)
    :loop_(loop)
    ,serverAddr_(serverAddr)
    ,connect_(false)
    ,state_(StateE::kDisconnected)
    ,channel_(nullptr)
    ,retryDelayMs_(retryDelayMs)
{}

Connector::~Connector()
{
    LOG_DEBUG << "Connector::~Connector - Connector destructed";
    // channel_ is invalid, we can not use shared_from_this() in destructor
    // we should remove channel directly
    Channel* rawChannel = channel_.release(); // release ownership
    if(rawChannel)
    {
        // Must close the socket if we are destroying the connector while it has an active channel/socket
        int sockfd = rawChannel->fd();
        
        // Ensure channel is cleaned up properly.
        // If we are in the loop thread, we disable and remove immediately, but queue deletion
        // to handle the case where we might be inside a Channel callback.
        if(loop_ && loop_->isInLoopThread())
        {
            if(!rawChannel->isNoneEvent())
            {
                rawChannel->disableAll();
            }
            rawChannel->remove();
            
            // Defer deletion to avoid use-after-free if we assume we might be in a callback
            loop_->queueInLoop([rawChannel](){
                delete rawChannel;
            });
        }
        else if(loop_)
        {
            // If not in loop thread, post the whole cleanup to the loop.
            loop_->runInLoop([rawChannel](){
                if(!rawChannel->isNoneEvent())
                {
                    rawChannel->disableAll();
                }
                rawChannel->remove();
                delete rawChannel;
            });
        } 
        else 
        {
             // Loop is gone or null, just delete.
             delete rawChannel;
        }
        ::close(sockfd);
    }
    LOG_DEBUG << "Connector::~Connector - Connector destructed done";
}

void Connector::start()
{
    if(state_.load(std::memory_order_acquire) == StateE::kDisconnected)
    {
        connect_ = true;
        // Use shared_from_this to keep Connector alive during async call
        loop_->runInLoop([self = shared_from_this()](){ self->startInLoop(); });
    }else{
        LOG_ERROR << "Connector unavaiable, state_ != kDisconnected ";
    }
}

void Connector::startInLoop()
{
    loop_->assertInLoopThread();
    if(connect_.load(std::memory_order_acquire)) 
    {
        connect();
    }else{
        LOG_ERROR << "connect_ flag is false";
    }
}

void Connector::restart()
{
    LOG_DEBUG <<  "=========================================";
    LOG_DEBUG << "Connector::restart - Restarting connector";
    loop_->assertInLoopThread();
    if(state_.load(std::memory_order_acquire) == StateE::kConnecting)
    {
        // Cleanup existing channel and socket
        int sockfd = removeAndResetChannel();
        ::close(sockfd);
    }
    setState(StateE::kDisconnected);
    //重设retry时间为初始值
    retryDelayMs_.store(kInitRetryDelayMs, std::memory_order_release);
    connect_ = true;
    startInLoop();
}

void Connector::stop()
{
    connect_ = false;
    // Use shared_from_this to keep Connector alive for the stop operation
    loop_->runInLoop([self = shared_from_this()](){ self->stopInLoop(); });
}
void Connector::stopInLoop()
{
    loop_->assertInLoopThread();
    if(state_.load(std::memory_order_acquire) == StateE::kConnecting)
    {
        setState(StateE::kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

void Connector::connect()
{
    std::optional<int> sockfdOpt = createSocketNBandCE(serverAddr_.family());
    if(sockfdOpt)
    {
        int ret = sockconnect(sockfdOpt.value(), serverAddr_);
        int savedErrno = (ret == 0) ? 0 : errno;
        const auto [shouldConnect, shouldRetry, shouldClose] = 
            [&savedErrno]() -> std::tuple<bool, bool, bool>
            {
                switch(savedErrno)
                {
                    case 0:
                    case EINPROGRESS:
                    case EINTR:
                    case EISCONN:
                        return {true, false, false};
                    case EAGAIN:
                    case EADDRINUSE:
                    case EADDRNOTAVAIL:
                    case ECONNREFUSED:
                    case ENETUNREACH:
                        return {false, true, true};
                    default:
                        return {false, false, true};
                }
            }();
        if(shouldConnect)
        {
            connecting(sockfdOpt.value());
        }else if(shouldRetry)
        {
            LOG_DEBUG << "connecting shouleRetry"; 
            retry(sockfdOpt.value());
        }else{
            ::close(sockfdOpt.value());
            LOG_ERROR << "Connector::connect - connect error: " << savedErrno;
        }
    }else{
        LOG_ERROR << "Connector::connect - create socket error";
    }
}

void Connector::connecting(int sockfd)
{
    loop_->assertInLoopThread();
    assert(!channel_);
    setState(StateE::kConnecting);

    channel_ = std::make_unique<Channel>(loop_, sockfd);
    // Use weak_ptr for channel callbacks to prevent access to destroyed Connector
    std::weak_ptr<Connector> weakSelf(shared_from_this());
    channel_->setWriteEventCb([weakSelf](){ 
        if(auto self = weakSelf.lock()) {
            self->handleWrite(); 
        }
    });
    channel_->setErrorEventCb([weakSelf](){ 
        if(auto self = weakSelf.lock()) {
            self->handleError(); 
        }
    });

    channel_->enableWrite();//连接建立
}

void Connector::handleWrite()
{
    if(state_.load(std::memory_order_acquire) == StateE::kConnecting)
    {
        int savedError{0};
        //channle是一次性的，连接建立后就不需要了
        int sockfd = removeAndResetChannel(); //channel的销毁必须是在pendingfunctor中, 因为此时for(:activeChannel)
        if (sockfd < 0) 
        {
            LOG_WARN << "Connector::handleWrite - channel already removed";
            return;
        }

        //非阻塞connect完成后，内核会发送一个写事件通知我们连接建立成功，或者连接失败
        //可写时间只代表连接完成, 成功或失败是未知的    
        //必须通过getsockopt获取SO_ERROR来判断连接是否成功
        savedError = Socket::getSocketError(sockfd);
        if(savedError)
        {
            LOG_ERROR << "Socket Error:" << strerror(savedError);
            retry(sockfd);
        }else if(Socket::selfConnection(sockfd)){
            retry(sockfd);
        }else{
            //连接成功
            setState(StateE::kConnected);
            if(connetionCb_)
            {
                connetionCb_(sockfd);
            }else{
                LOG_ERROR << "no connection callback is set";
                ::close(sockfd);
            }
        }
    }else{
        setState(StateE::kDisconnected);
        LOG_ERROR << "Connector::handleWrite - not in kConnecting state";
    }
}

int Connector::removeAndResetChannel()
{
    loop_->assertInLoopThread();
    if (!channel_) return -1;

    int sockfd = channel_->fd();
    if(!channel_->isNoneEvent())
    {
        channel_->disableAll();
    }
    channel_->remove();
    // Move channel ownership to pending functor. 
    // Use shared_ptr because std::function requires copyable callable.
    std::shared_ptr<Channel> guard(channel_.release());
    loop_->queueInLoop([guard]() {
        // Channel destructs when guard is destroyed
    });
    // channel_ is now nullptr
    
    LOG_DEBUG << "Connector channel removed and reset";
    return sockfd;
}

void Connector::resetChannel() 
{
    // Deprecated/Unused with new removeAndResetChannel implementation
    if(channel_) channel_.reset(); 
}

void Connector::handleError()
{
    LOG_ERROR << "Connector::handleError - SO_ERROR = " << errno << " " << strerror(errno);
    if(state_.load(std::memory_order_acquire) == StateE::kConnecting)
    {
        int connsock = removeAndResetChannel();
        if (connsock < 0) return;
        retry(connsock);
    }
}

void Connector::retry(int sockfd)
{
    ::close(sockfd);
   
    if(state_.load(std::memory_order_acquire) == StateE::kConnecting)
    {
        LOG_DEBUG << "=========================================";
        LOG_DEBUG << "Connector::retry - Retry connecting";
        if(connect_.load(std::memory_order_acquire))
        {
            LOG_INFO << "Connector::retry - Retry connecting to "
                    << " in " << retryDelayMs_.load() << " milliseconds.";
            
            int delay = std::clamp(retryDelayMs_.load() * 2, kInitRetryDelayMs, kMaxRetryDelayMs);
            
            // Use weak_ptr for retry timer to prevent keeping Connector alive indefinitely
            std::weak_ptr<Connector> weakSelf(shared_from_this());
            loop_->runAfter(delay / 1000.0, [weakSelf]() { 
                if(auto self = weakSelf.lock()) {
                    self->startInLoop(); 
                }
            });
            //retry时间指数退避
            retryDelayMs_.store(delay, std::memory_order_release);
        }else{
            LOG_DEBUG << "Connector::retry - do not connect";
        }
    }else{
        LOG_ERROR << "Connector::retry - not in kConnecting state";
    }
}

} //end mymoduo
} //end net
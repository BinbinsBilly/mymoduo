#include "socket.h"

namespace mymoduo::net 
{
using namespace mymoduo::net;

std::optional<Socket> Socket::create(int family, int type, int protocol)
{
    int sockfd = ::socket(family, type, protocol);
    if(sockfd < 0)
    {
        return std::nullopt;
    }
    return Socket(sockfd);
}

// DESIGN ALTERNATIVE:
// In C++23 one could use std::expected<Socket, int> returning errno instead of std::optional.
// Example (not compiled here):
//   std::expected<Socket,int> Socket::create(int fam,int type,int proto) {
//       int fd = ::socket(fam,type,proto);
//       if(fd < 0) return std::unexpected(errno);
//       return Socket(fd);
//   }
// This lets caller distinguish different failure reasons without separate errno check.

Socket::Socket(int sockfd)
    :sockfd_(new int(sockfd))
{}

Socket::Socket(Socket&& other) noexcept
    :sockfd_(std::move(other.sockfd_))
{
    other.sockfd_ = nullptr;
}

Socket& Socket::operator=(Socket &&other) noexcept
{
    if(this != &other) //必须检查自赋值
    {
        sockfd_ = std::move(other.sockfd_);
        other.sockfd_ = nullptr;
    }
    return *this;
}

//因为sockfd_已经被unique_ptr管理，所以析构函数可以使用默认实现
Socket::~Socket() = default;

int Socket::fd() const
{
    //先判空再解引用: moved-from Socket 的 sockfd_ 为 nullptr; fd==0 是合法描述符
    return (sockfd_ && *sockfd_ >= 0) ? *sockfd_ : -1;
}

bool Socket::getTcpInfo(tcp_info* info) const
{
    if(!isvalid() || !info)
    {
        return false;
    }
    socklen_t len = sizeof(*info);
    ::memset(info, 0 , sizeof(*info));
    return ::getsockopt(*sockfd_, SOL_TCP, TCP_INFO, info, &len) == 0;
}

std::optional<std::string> Socket::getTcpInfoString() const
{
    struct tcp_info info;
    if(getTcpInfo(&info))
    {
        char buf[1024];
        ::snprintf(buf, sizeof(buf),
                   "unacked=%u rto=%u ato=%u snd_mss=%u rcv_mss=%u "
                   "lost=%u retrans=%u rtt=%u rttvar=%u "
                   "snd_ssthresh=%u snd_cwnd=%u total_retrans=%u",
                   info.tcpi_unacked, info.tcpi_rto, info.tcpi_ato,
                   info.tcpi_snd_mss, info.tcpi_rcv_mss,
                   info.tcpi_lost, info.tcpi_retrans,
                   info.tcpi_rtt, info.tcpi_rttvar,
                   info.tcpi_snd_ssthresh, info.tcpi_snd_cwnd,
                   info.tcpi_total_retrans);
        return std::string(buf);
    }
    return std::nullopt;
}

int Socket::getSocketError() const 
{
    int optval;
    socklen_t optlen = sizeof(optval);
    if(!isvalid() || ::getsockopt(*sockfd_, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        return errno;
    }
    return optval;
}
int Socket::getSocketError(int fd)
{
    int optval;
    socklen_t optlen = sizeof(optval);
    if(fd < 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        return errno;
    }
    return optval;
}

struct sockaddr_in6 Socket::getLocalAddr(int sockfd)
{
    struct sockaddr_in6 localaddr;
    ::memset(&localaddr, 0, sizeof(struct sockaddr_in6));
    socklen_t socklen = static_cast<socklen_t>(sizeof(localaddr));
    int ret = ::getsockname(sockfd, (struct sockaddr*)&localaddr, &socklen);
    if(ret < 0)
    {
        LOG_ERROR << "getsockname()";
    }
    return localaddr;
}

struct sockaddr_in6 Socket::getPeerAddr(int sockfd)
{
    struct sockaddr_in6 peeraddr;
    ::memset(&peeraddr, 0, sizeof(struct sockaddr_in6));
    socklen_t socklen = static_cast<socklen_t>(sizeof(peeraddr));
    int ret = ::getpeername(sockfd, (struct sockaddr*)&peeraddr, &socklen);
    if(ret < 0)
    {
        LOG_ERROR << "getpeername()";
    }
    return peeraddr;
}

bool Socket::selfConnection(int sockfd)
{
    struct sockaddr_in6 peerAddr;
    struct sockaddr_in6 localAddr;

    peerAddr = getPeerAddr(sockfd);
    localAddr = getLocalAddr(sockfd);

    if(peerAddr.sin6_family == AF_INET)
    {
        const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&peerAddr);
        const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&localAddr);
        return laddr4->sin_port == raddr4->sin_port && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
    }else if(peerAddr.sin6_family == AF_INET6)
    {
        //用 memcmp 比较地址: memcpy 返回目标指针永不为 0, 原写法恒为 false
        return localAddr.sin6_port == peerAddr.sin6_port
            && ::memcmp(&peerAddr.sin6_addr, &localAddr.sin6_addr, sizeof(localAddr.sin6_addr)) == 0;
    }else{
        LOG_ERROR << "unknow SOCK_FAMILY";
        return false;
    }
}

bool Socket::bindaddress(const InetAddress localaddr)
{
    if(!isvalid())
    {
        return false;
    }
    const struct sockaddr* addr = localaddr.getSockAddr();
    socklen_t addrlen = static_cast<socklen_t>(addr->sa_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
    return ::bind(*sockfd_, addr, addrlen) == 0;
}

bool Socket::listen(int backlog)
{
    if(!isvalid())
    {
        return false;
    }
    return ::listen(*sockfd_, backlog) == 0;
}

//peerAddr 用于存储被接受连接的对端地址
std::optional<Socket> Socket::accept(InetAddress& peeraddr, SetNonBlocking nonblock, SetCloseOnExec closeonexec)
{
    if(!isvalid())
    {
        return std::nullopt;
    }
    //确保peeraddr有足够的空间存储地址
    struct sockaddr_in6 addr;
    std::memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    int connfd = ::accept(*sockfd_, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    if(connfd < 0)
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            LOG_INFO << "Socket::accept - no connections are present to be accepted right now.";
            return std::nullopt;
        }
        LOG_ERROR << "Socket::accept - accept failed";
        return std::nullopt;
    }
    if(nonblock == SetNonBlocking::ENABLE)
    {
        setNonblock(connfd);
    }
    if(closeonexec == SetCloseOnExec::ENABLE)
    {
        setCloseOnExec(connfd);
    }

    peeraddr = InetAddress(addr);
    return Socket(connfd);
}

int Socket::connect(int sockfd, const InetAddress& serverAddr)
{
    return ::connect(sockfd, serverAddr.getSockAddr(),
                    serverAddr.family() == AF_INET ? sizeof(struct sockaddr_in) :
                    sizeof(struct sockaddr_in6));
}

void Socket::shutdownWrite()
{
    if(isvalid())
    {
        ::shutdown(*sockfd_, SHUT_WR);
    }
}

void Socket::setNonblock(const int fd) noexcept
{
    if(fd >= 0)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        ::fcntl(fd, F_SETFL, flags);
    }
}

void Socket::setCloseOnExec(const int fd) noexcept
{
    if(fd >= 0)
    {
        int flags = ::fcntl(fd, F_GETFD, 0);
        flags |= FD_CLOEXEC;
        ::fcntl(fd, F_SETFD, flags);
    }
}

void Socket::setTcpNodelay(TcpNodelay opt) noexcept
{
    if(isvalid())
    {
        int optval = (opt == TcpNodelay::ENABLE) ? 1 : 0;
        ::setsockopt(*sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    }
}

void Socket::setReuseAddr(ReuseAddr opt) noexcept
{
    if(isvalid())
    {
        int optval = (opt == ReuseAddr::ENABLE) ? 1 : 0;
        ::setsockopt(*sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    }
}

void Socket::setReusePort(ReusePort opt) noexcept
{
    if(isvalid())
    {
        int optval = (opt == ReusePort::ENABLE) ? 1 : 0;
        ::setsockopt(*sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    }
}

// 禁用/启用 TCP keepalive 机制
void Socket::setKeepAlive(KeepAlive opt) noexcept
{
    if(isvalid())
    {
        int optval = (opt == KeepAlive::ENABLE) ? 1 : 0;
        ::setsockopt(*sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    }
}



} // end namespace mymoduo::net


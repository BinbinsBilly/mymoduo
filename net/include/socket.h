/**
 * @file socket.h
 * @author sbilly
 * @brief 封装Socket类, 提供Socket的创建、配置和基本操作功能
 *        配合InetAddress类使用, 封装度较高, 便于网络编程
 *        核心操作: 创建Socket create(); 绑定地址 bindaddress(); 监听 listen(); 接受连接 accept();
 *        相关地址的操作都用InetAddress类封装
 */

#pragma once

#include "InetAddress.hpp"

#include <optional>
#include <variant>
#include <string>
#include <chrono>
#include <memory>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>


struct tcp_info;

namespace mymoduo
{
namespace net
{

enum class TcpNodelay{ DISABLE, ENABLE };
enum class ReuseAddr { DISABLE, ENABLE };
enum class ReusePort { DISABLE, ENABLE };
enum class KeepAlive { DISABLE, ENABLE };
enum class SetNonBlocking { DISABLE, ENABLE };
enum class SetCloseOnExec { DISABLE, ENABLE };

class Socket
{
public:
    // 静态工厂方法，创建Socket对象
    /**
     * @brief 创建一个Socket对象
     * @param family 协议族，如AF_INET
     * @param type 套接字类型，如SOCK_STREAM
     * @param protocol 协议，如IPPROTO_TCP
     */
    static std::optional<Socket> create(int family, int type, int protocol);
    Socket(int sockfd);
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    ~Socket();

    int fd() const;
    bool getTcpInfo(tcp_info* info) const;
    std::optional<std::string> getTcpInfoString() const;
    
    static struct sockaddr_in6 getLocalAddr(int sockfd);
    static struct sockaddr_in6 getPeerAddr(int sockfd);

    static std::variant<struct sockaddr_in, struct sockaddr_in6> getLocalAddrVariant(int sockfd)
    {
        struct sockaddr_in6 addr6 = getLocalAddr(sockfd);
        if(addr6.sin6_family == AF_INET6)
        {
            return addr6;
        }else{
            return *reinterpret_cast<struct sockaddr_in*>(&addr6);
        }
    }
    static std::variant<struct sockaddr_in, struct sockaddr_in6> getPeerAddrVariant(int sockfd)
    {
        struct sockaddr_in6 addr6 = getPeerAddr(sockfd);
        if(addr6.sin6_family == AF_INET6)
        {
            return addr6;
        }else{
            return *reinterpret_cast<struct sockaddr_in*>(&addr6);
        }
    }

    int getSocketError() const;
    static int getSocketError(int fd); 
    static bool selfConnection(int fd);

    bool bindaddress(const InetAddress localaddr);
    bool listen(int backlog = SOMAXCONN);

    std::optional<Socket> accept(InetAddress& peeraddr, SetNonBlocking nonblock = SetNonBlocking::DISABLE,
                                SetCloseOnExec closeonexec = SetCloseOnExec::DISABLE);

    int connect(int sockfd, const InetAddress& serverAddr);

    void shutdownWrite();

    void setTcpNodelay(TcpNodelay opt) noexcept;
    void setReuseAddr(ReuseAddr opt) noexcept;
    void setReusePort(ReusePort opt) noexcept;
    void setKeepAlive(KeepAlive opt) noexcept;

    void setNonblock(const int fd) noexcept;
    void setCloseOnExec(const int fd) noexcept;

    //Rep表示类型，即用于存储时间值的数值类型
    template<typename Rep, typename Period>
    void setRecvTimeout(const std::chrono::duration<Rep, Period>& timeout);

    template<typename Rep, typename Period>
    void setSendTimeout(const std::chrono::duration<Rep, Period>& timeout);

    void close() noexcept
    {
        if(isvalid())
        {
            ::close(*sockfd_);
            *sockfd_ = -1;
        }
    }

    bool isvalid() const
    {
        return sockfd_ && *sockfd_ >= 0;
    }

private:
    friend class TcpConn;


    struct sockfdDeleter
    {
        void operator()(int* fd) noexcept
        {
            if(fd && *fd >= 0)
            {
                ::close(*fd);
            }
            delete fd;
        }
    };

    std::unique_ptr<int, sockfdDeleter> sockfd_{nullptr};
};  // class Socket

template<typename Rep, typename Period>
void Socket::setRecvTimeout(const std::chrono::duration<Rep, Period>& timeout)
{
    if(isvalid())
    {
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);

        timeval tv;
        tv.tv_sec = seconds.count();
        tv.tv_usec = microseconds.count();

        ::setsockopt(*sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
}
template<typename Rep, typename Period>
void Socket::setSendTimeout(const std::chrono::duration<Rep, Period>& timeout)
{
    if(isvalid())
    {
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);

        timeval tv;
        tv.tv_sec = seconds.count();
        tv.tv_usec = microseconds.count();

        ::setsockopt(*sockfd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
}

}   // namespace net
}   // namespace mymoduo

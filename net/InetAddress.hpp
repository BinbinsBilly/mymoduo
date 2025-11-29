/**
 * @file InetAddress.hpp
 * @author sbilly
 * @brief 封装IP地址和端口的InetAddress类, 提供地址的创建、解析和转换功能
 *        支持IPv4和IPv6地址 供Socket类使用
 */

#pragma once

#include <Logging.h>
#include <cassert>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <memory>

const static int kIPv4AddressSize = sizeof(struct sockaddr_in);
const static int kIPv6AddressSize = sizeof(struct sockaddr_in6);

namespace mymoduo
{
namespace net
{

class InetAddress
{
public:
    InetAddress(uint16_t port = 0, bool loopbackOnly = false, bool ipv6 = false)
        : ipv6_(ipv6)
    {
        std::memset(&addr_, 0, sizeof(addr_));
        if (!ipv6_)
        {
            addr_.addr4.sin_family = AF_INET;
            addr_.addr4.sin_port = htons(port);
            addr_.addr4.sin_addr.s_addr = loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
        }
        else
        {
            addr_.addr6.sin6_family = AF_INET6;
            addr_.addr6.sin6_port = htons(port);
            addr_.addr6.sin6_addr = loopbackOnly ? in6addr_loopback : in6addr_any;
        }
    }

    InetAddress(const std::string &ip, uint16_t port, bool ipv6 = false)
        : ipv6_(ipv6)
    {
        std::memset(&addr_, 0, sizeof(addr_));
        if (!ipv6_)
        {
            addr_.addr4.sin_family = AF_INET;
            addr_.addr4.sin_port = htons(port);
            ::inet_pton(AF_INET, ip.c_str(), &addr_.addr4.sin_addr);
        }
        else
        {
            addr_.addr6.sin6_family = AF_INET6;
            addr_.addr6.sin6_port = htons(port);
            ::inet_pton(AF_INET6, ip.c_str(), &addr_.addr6.sin6_addr);
        }
    }

    explicit InetAddress(const struct sockaddr_in& addr)
        : ipv6_(false)
    {
        addr_.addr4 = addr;
    }

    explicit InetAddress(const struct sockaddr_in6& addr)
    {
        if(addr.sin6_family == AF_INET6)
        {
            ipv6_ = true;
            addr_.addr6 = addr;
        }
        else
        {
            ipv6_ = false;
            addr_.addr4 = *reinterpret_cast<const struct sockaddr_in*>(&addr);
        }
    }

    sa_family_t family() const {
         if(ipv6_) return AF_INET6;
         return AF_INET;
        }

    std::string toIp(char *buf, size_t size) const
    {
        if (!ipv6_)
        {
            assert(size >= INET_ADDRSTRLEN);
            ::inet_ntop(AF_INET, const_cast<in_addr*>(&addr_.addr4.sin_addr), buf, size);
        }
        else
        {
            assert(size >= INET6_ADDRSTRLEN);
            ::inet_ntop(AF_INET6, const_cast<in6_addr*>(&addr_.addr6.sin6_addr), buf, size);
        }
        return std::string(buf);
    }

    uint16_t toPort() const
    {
        return !ipv6_ ? ntohs(addr_.addr4.sin_port) : ntohs(addr_.addr6.sin6_port);
    }

    const struct sockaddr* getSockAddr() const
    {
        return ipv6_ ? reinterpret_cast<const struct sockaddr*>(&(addr_.addr6))
                        : reinterpret_cast<const struct sockaddr*>(&(addr_.addr4));
    }

    std::string toIpPort() const
    {
        char buf[64] = "";
        size_t size = sizeof(buf);
        if(!ipv6_)
        {
            assert(size >= INET_ADDRSTRLEN);
            toIp(buf, size);
            size_t end = strlen(buf); //获取ip字符串长度 
            //从end开始写port
            assert(size - end > 6); //端口最大长度为5 + ':'
            snprintf(buf + end, size -end, ":%u", toPort());
        }else{
            assert(size > INET6_ADDRSTRLEN);
            buf[0] = '[';
            toIp(buf + 1, size - 1);
            size_t end = strlen(buf);
            assert(size - end > 7); //端口最大长度为5 + ']:' 共7
            snprintf(buf + end, size - end, "]:%u", toPort());
        }
        return std::string(buf);
    }

    // 解析域名 (family = AF_UNSPEC / AF_INET / AF_INET6)
    static bool resolve(const std::string& hostname, InetAddress* out, sa_family_t family = AF_UNSPEC)
    {
        assert(out != nullptr);
        struct addrinfo hints{};
        struct addrinfo* result = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;

        int ret = ::getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
        if (ret != 0 || result == nullptr)
        {
            LOG_ERROR << "InetAddress::resolve hostname=" << hostname << " failed " << ::gai_strerror(ret);
            if (result) ::freeaddrinfo(result);
            return false;
        }
        std::unique_ptr<struct addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);

        bool success = false;
        for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next)
        {
            if (rp->ai_family == AF_INET)
            {
                out->ipv6_ = false;
                out->addr_.addr4 = *reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
                success = true;
                break;
            }
            else if (rp->ai_family == AF_INET6)
            {
                out->ipv6_ = true;
                out->addr_.addr6 = *reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
                success = true;
                break;
            }
        }

        if (!success)
        {
            LOG_ERROR << "InetAddress::resolve - No address matched for hostname=" << hostname;
        }
        return success;
    }

private:
    union
    {
        struct sockaddr_in  addr4;
        struct sockaddr_in6 addr6;
    } addr_;

    bool ipv6_;
};// end InetAddress

} // end net
} // namespace mymoduo


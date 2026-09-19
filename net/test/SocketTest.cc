#include "socket.h"
#include "InetAddress.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <future>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using mymoduo::net::Socket;
using mymoduo::net::InetAddress;
using mymoduo::net::TcpNodelay;
using mymoduo::net::ReuseAddr;
using mymoduo::net::ReusePort;
using mymoduo::net::KeepAlive;
using mymoduo::net::SetNonBlocking;
using mymoduo::net::SetCloseOnExec;

static void testCreate() {
    auto opt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(opt.has_value());
    assert(opt->fd() >= 0);
    std::cout << "Socket create test passed fd=" << opt->fd() << "\n";
}

static void testOptions() {
    auto opt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(opt.has_value());
    opt->setTcpNodelay(TcpNodelay::ENABLE);
    opt->setReuseAddr(ReuseAddr::ENABLE);
    opt->setReusePort(ReusePort::ENABLE);
    opt->setKeepAlive(KeepAlive::DISABLE);
    // 没有直接读取选项的方法，只验证不会崩溃
    std::cout << "Socket options set without crash." << '\n';
}

static void testBindListenAccept() {
    auto serverOpt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(serverOpt.has_value());
    auto &server = *serverOpt;
    server.setReuseAddr(ReuseAddr::ENABLE);
    // 绑定到 loopback 随机端口(0)
    bool bound = server.bindaddress(InetAddress(0, true));
    assert(bound);
    bool listened = server.listen(16);
    assert(listened);

    // 查询实际端口
    struct sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    int r = ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&sin), &len);
    assert(r == 0);
    uint16_t port = ntohs(sin.sin_port);
    assert(port != 0);

    std::thread client([port]{
        int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(cfd >= 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        int cr = ::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        assert(cr == 0);
        // 保持连接直到 accept 完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ::close(cfd);
    });

    InetAddress peer; // 接收对端地址
    auto acceptedOpt = server.accept(peer, SetNonBlocking::ENABLE, SetCloseOnExec::ENABLE);
    assert(acceptedOpt.has_value());
    Socket accepted = std::move(*acceptedOpt);
    assert(accepted.fd() >= 0);
    // 验证非阻塞标志
    int flags = ::fcntl(accepted.fd(), F_GETFL, 0);
    assert(flags & O_NONBLOCK);
    int fdflags = ::fcntl(accepted.fd(), F_GETFD, 0);
    assert(fdflags & FD_CLOEXEC);
    char buf[INET_ADDRSTRLEN];
    peer.toIp(buf, sizeof(buf));
    std::cout << "Accepted connection from " << peer.toIpPort() << '\n';
    client.join();
}

static void testMoveSemantics() {
    int originalFd = -1;
    // 上面行不合适, 简化: 直接创建然后 move
    auto opt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(opt.has_value());
    originalFd = opt->fd();
    Socket target = std::move(*opt); // move 构造
    assert(target.fd() == originalFd);
    // 测试析构关闭: 使用局部作用域
    int testFd = -1;
    {
        auto tmp = Socket::create(AF_INET, SOCK_STREAM, 0);
        assert(tmp.has_value());
        testFd = tmp->fd();
    } // tmp 析构应关闭 testFd
    int ret = ::fcntl(testFd, F_GETFD, 0);
    assert(ret == -1 && errno == EBADF);
    std::cout << "Move & RAII close semantics passed." << '\n';
}

static void testTcpInfoString() {
    auto opt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(opt.has_value());
    auto infoStr = opt->getTcpInfoString();
    // 未建立连接时 tcp_info 仍可获取, 如果失败返回 nullopt, 可以接受
    if (infoStr.has_value()) {
        assert(infoStr->find("unacked=") != std::string::npos);
        std::cout << "TCP info: " << *infoStr << '\n';
    } else {
        std::cout << "TCP info not available (expected on some systems)." << '\n';
    }
}

static void testMovedFromFd() {
    // moved-from Socket 的 sockfd_ 为 nullptr, fd() 应返回 -1 而不是解引用空指针
    auto opt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(opt.has_value());
    Socket a = std::move(*opt);
    Socket b = std::move(a); // a 变为 moved-from
    assert(b.fd() >= 0);     // fd 所有权已转移给 b
    assert(a.fd() == -1);    // moved-from Socket 的 fd() 返回 -1
    std::cout << "Moved-from Socket fd() returns -1." << '\n';
}

static void testSelfConnection() {
    // 用一对真实的 TCP 连接(非自连接)验证 selfConnection 返回 false
    // 注意: 未连接 fd 的 getpeername 会失败返回全 0 地址, 可能误判, 故必须用真实连接
    auto serverOpt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(serverOpt.has_value());
    auto &server = *serverOpt;
    server.setReuseAddr(ReuseAddr::ENABLE);
    bool bound = server.bindaddress(InetAddress(0, true));
    assert(bound);
    bool listened = server.listen(16);
    assert(listened);

    // 查询实际端口
    struct sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    int r = ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&sin), &len);
    assert(r == 0);
    uint16_t port = ntohs(sin.sin_port);

    std::thread client([port]{
        int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(cfd >= 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        int cr = ::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        assert(cr == 0);
        // 保持连接直到 selfConnection 检查完成
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ::close(cfd);
    });

    InetAddress peer;
    auto acceptedOpt = server.accept(peer);
    assert(acceptedOpt.has_value());
    assert(acceptedOpt->fd() >= 0);
    // 普通连接(非自连接)应返回 false
    assert(!Socket::selfConnection(acceptedOpt->fd()));
    std::cout << "selfConnection returns false for a normal connection." << '\n';
    client.join();
}

// 回归: accept 返回的连接 socket 必须为非阻塞 + cloexec
// (Acceptor::handleRead 现以 ENABLE 参数调用 accept, 此处验证 Socket::accept 同一路径)
static void test_accept_nonblock_cloexec() {
    auto serverOpt = Socket::create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(serverOpt.has_value());
    auto &server = *serverOpt;
    server.setNonblock(server.fd());
    server.setCloseOnExec(server.fd());
    // 不常用端口, 避免与其他用例冲突
    InetAddress addr(static_cast<uint16_t>(24567), true, false);
    bool bound = server.bindaddress(addr);
    assert(bound);
    bool listened = server.listen(16);
    assert(listened);

    // 客户端用阻塞 socket ::connect(监听 fd 非阻塞不影响客户端);
    // connect 返回 0 即连接已进入 backlog, 经 promise 同步后再在非阻塞监听 fd 上 accept
    std::promise<void> connected;
    auto connectedFuture = connected.get_future();
    std::thread client([&connected]{
        int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(cfd >= 0);
        struct sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(24567);
        ::inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
        int cr = ::connect(cfd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
        assert(cr == 0);
        connected.set_value();
        // 保持连接直到 accept 完成
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ::close(cfd);
    });

    connectedFuture.wait();
    InetAddress peer;
    auto connOpt = server.accept(peer, SetNonBlocking::ENABLE, SetCloseOnExec::ENABLE);
    assert(connOpt.has_value());
    assert(connOpt->fd() >= 0);
    int flags = ::fcntl(connOpt->fd(), F_GETFL, 0);
    assert(flags != -1);
    assert(flags & O_NONBLOCK);
    int fdflags = ::fcntl(connOpt->fd(), F_GETFD, 0);
    assert(fdflags != -1);
    assert(fdflags & FD_CLOEXEC);
    std::cout << "[OK] accepted socket is nonblocking + cloexec (fd=" << connOpt->fd() << ")" << '\n';
    client.join();
}

int main() {
    testCreate();
    testOptions();
    testBindListenAccept();
    testMoveSemantics();
    testTcpInfoString();
    testMovedFromFd();
    testSelfConnection();
    test_accept_nonblock_cloexec();
    std::cout << "All Socket tests passed." << std::endl;
    return 0;
}

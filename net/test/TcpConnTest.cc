#include "TcpConn.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "InetAddress.hpp"
#include "socket.h"
#include "Buffer.h"
#include "Logging.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace mymoduo::net;

// Helper: create a TCP listener on 127.0.0.1:0 and connect a client.
// Returns {accepted_fd, client_fd, localAddr, peerAddr}
struct TcpPair {
    Socket accepted;      // 已经建立的服务端连接 Socket
    int client_fd{-1};    // 客户端 fd
    InetAddress localAddr; // server local addr
    InetAddress peerAddr;  // server peer addr
    TcpPair(Socket&& s, int cfd, const InetAddress& local, const InetAddress& peer)
        : accepted(std::move(s)), client_fd(cfd), localAddr(local), peerAddr(peer) {}
    bool valid() const { return client_fd >= 0 && accepted.fd() >= 0; }
};
static TcpPair makeTcpPair()
{
    auto serverOpt = Socket::create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(serverOpt.has_value());
    Socket server = std::move(serverOpt.value());
    server.setReuseAddr(ReuseAddr::ENABLE);
    server.setReusePort(ReusePort::ENABLE);

    InetAddress bindAddr(0, true, false); // 127.0.0.1:any
    bool ok = server.bindaddress(bindAddr);
    assert(ok);
    ok = server.listen(128);
    assert(ok);

    // 查询端口
    sockaddr_in sa{}; 
    socklen_t len = sizeof(sa);
    int r = ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&sa), &len);
    assert(r == 0);
    uint16_t port = ntohs(sa.sin_port);

    // 客户端连接
    int cfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    assert(cfd >= 0);
    sockaddr_in cli{}; cli.sin_family = AF_INET; cli.sin_port = htons(port); ::inet_pton(AF_INET, "127.0.0.1", &cli.sin_addr);
    r = ::connect(cfd, reinterpret_cast<sockaddr*>(&cli), sizeof(cli));
    assert(r == 0);

    // 接受连接
    InetAddress peer;
    auto accOpt = server.accept(peer, SetNonBlocking::DISABLE, SetCloseOnExec::ENABLE);
    assert(accOpt.has_value());
    Socket accepted = std::move(accOpt.value());

    // 本地地址
    sockaddr_in localSa{}; len = sizeof(localSa);
    r = ::getsockname(accepted.fd(), reinterpret_cast<sockaddr*>(&localSa), &len); assert(r == 0);
    InetAddress localAddr(localSa);

    return TcpPair(std::move(accepted), cfd, localAddr, peer);
}

static void test_connection_established()
{
    EventLoopThread loopThread([](mymoduo::net::EventLoop*){}, "tcpconn-test-conn");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();

    TcpPair tp = makeTcpPair();

    std::atomic<bool> connected{false};

    auto conn = std::make_shared<TcpConn>(loop, "conn1", std::move(tp.accepted), tp.localAddr, tp.peerAddr);
    conn->setConnectionCb([&](const TcpConnPtr& c){ connected = c->connected(); });

    loop->runInLoop([conn]{ conn->connectEstablished(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(connected.load());

    // cleanup
    loop->runInLoop([conn]{ conn->connectDestroyed(); });
    ::close(tp.client_fd);
}

static void test_receive_message()
{
    EventLoopThread loopThread([](mymoduo::net::EventLoop*){}, "tcpconn-test-recv");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();

    TcpPair tp = makeTcpPair();

    std::atomic<size_t> received{0};
    std::string payload = "hello-mymoduo";

    auto conn = std::make_shared<TcpConn>(loop, "conn2", std::move(tp.accepted), tp.localAddr, tp.peerAddr);
    conn->setMessageCb([&](const TcpConnPtr&, Buffer* buf, mymoduo::base::TimeStamp){
        received = buf->readableBytes();
        std::string s(buf->peek(), buf->readableBytes());
        assert(s == payload);
        buf->retrieveAll();
    });

    loop->runInLoop([conn]{ conn->connectEstablished(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Write from client to server
    ssize_t n = ::write(tp.client_fd, payload.data(), payload.size());
    assert(n == (ssize_t)payload.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(received.load() == payload.size());

    // cleanup
    loop->runInLoop([conn]{ conn->connectDestroyed(); });
    ::close(tp.client_fd);
}

static void test_shutdown()
{
    EventLoopThread loopThread([](mymoduo::net::EventLoop*){}, "tcpconn-test-shutdown");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();

    TcpPair tp = makeTcpPair();
    auto conn = std::make_shared<TcpConn>(loop, "conn3", std::move(tp.accepted), tp.localAddr, tp.peerAddr);

    std::atomic<bool> closed{false};
    conn->setHandleCloseCb([&](const TcpConnPtr&){ closed = true; });

    loop->runInLoop([conn]{ conn->connectEstablished(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Close client side to trigger close on server
    ::shutdown(tp.client_fd, SHUT_RDWR);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(closed.load());

    loop->runInLoop([conn]{ conn->connectDestroyed(); });
    ::close(tp.client_fd);
}

int main()
{
    mymoduo::Logger::setLogLevel(mymoduo::Logger::DEBUG);
    // Keep logging minimal
    LOG_INFO << "TcpConn tests start";
    test_connection_established();
    test_receive_message();
    test_shutdown();
    LOG_INFO << "TcpConn tests passed";
    return 0;
}

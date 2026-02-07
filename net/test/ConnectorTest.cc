// ConnectorTest.cc - tests for Connector: success connect, retry, stop, restart
#include "Connector.h"
#include "EventLoop.h"
#include "InetAddress.hpp"
#include "socket.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace mymoduo::net;
using namespace mymoduo;

//创建并开启监听
static int createListeningSocket(uint16_t &portOut) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral 系统自动分配临时端口
    int r = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(r == 0);
    r = ::listen(fd, 16);
    assert(r == 0);
    sockaddr_in bound{}; 
    socklen_t len = sizeof(bound);
    r = ::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len);
    assert(r == 0);
    portOut = ntohs(bound.sin_port);
    return fd;
}

//保留端口 暂不开启监听  for retries and restart test
static int createReservedSocket(uint16_t &portOut) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral
    int r = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(r == 0);
    sockaddr_in bound{}; socklen_t len = sizeof(bound);
    r = ::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len);
    assert(r == 0);
    portOut = ntohs(bound.sin_port);
    // not listening yet; this reserves the port
    return fd;
}

// Test 1: successful connection to a listening loopback server
static void testSuccessfulConnect() {
    std::cout << "[testSuccessfulConnect]" << std::endl;
    uint16_t port{8888};
    int serverFd = createListeningSocket(port);
    std::atomic<int> connectedSock{-1};

    mymoduo::net::EventLoop loop; // construct and run in same thread
    InetAddress serverAddr(port, true); // loopback
    // Connector should be managed by shared_ptr to use shared_from_this()
    std::shared_ptr<Connector> connector = std::make_shared<Connector>(&loop, serverAddr);
    connector->setConnetionCb([&](int sockfd){
        connectedSock.store(sockfd);
        loop.quit();
    });
    connector->start();
    loop.runAfter(1.0, [&](){ // safety timeout
        if(connectedSock.load() < 0) { loop.quit(); }
    });
    loop.loop();
    assert(connectedSock.load() >= 0);
    ::close(serverFd);
    ::close(connectedSock.load());
    std::cout << "  passed" << std::endl;
}

// Test 2: connection refused triggers retry (no server). Expect no callback within window.
static void testConnectionRefusedRetry() {
    std::cout << "[testConnectionRefusedRetry]" << std::endl;
    // Choose an ephemeral port by opening and closing a socket so it's very likely unused.
    uint16_t port{0};
    int tmpFd = createListeningSocket(port);
    ::close(tmpFd); // now port should have no listener

    std::atomic<int> cbCount{0};
    mymoduo::net::EventLoop loop;
    InetAddress addr(port, true);
    // Use small initial retry delay to exercise retry logic quickly.
    std::shared_ptr<Connector> connector = std::make_shared<Connector>(&loop, addr, 100); // 100ms initial retry
    connector->setConnetionCb([&](int sockfd){
        cbCount.fetch_add(1);
        ::close(sockfd);
        loop.quit();
    });
    connector->start();
    // Run loop for a short time (< first few retries) then quit.
    loop.runAfter(0.5, [&](){ loop.quit(); });
    loop.loop();
    assert(cbCount.load() == 0); // should not have connected
    std::cout << "  passed" << std::endl;
}

// Test 3: stop before connection completes; expect no callback.
static void testStopBeforeConnect() {
    std::cout << "[testStopBeforeConnect]" << std::endl;
    uint16_t port{0};
    int tmpFd = createListeningSocket(port);
    ::close(tmpFd); // ensure no listener
    std::atomic<int> cbCount{0};
    mymoduo::net::EventLoop loop;
    InetAddress addr(port, true);
    std::shared_ptr<Connector> connector = std::make_shared<Connector>(&loop, addr, 100);
    connector->setConnetionCb([&](int sockfd){
        cbCount.fetch_add(1);
        ::close(sockfd);
        loop.quit();
    });
    connector->start();
    connector->stop(); // immediately stop
    loop.runAfter(0.4, [&](){ loop.quit(); });
    loop.loop();
    assert(cbCount.load() == 0);
    std::cout << "  passed" << std::endl;
}

// Test 4: restart after initial failures; create server later so restart succeeds.
static void testRestart() {
    std::cout << "[testRestart]" << std::endl;
    // Reserve a port without listening; connector will initially fail.
    uint16_t port{0};
    int reservedFd = createReservedSocket(port);

    std::atomic<int> connectedSock{-1};
    int serverFd{-1};
    mymoduo::net::EventLoop loop;
    InetAddress addr(port, true);
    std::shared_ptr<Connector> connector = std::make_shared<Connector>(&loop, addr, 50); // fast retry
    connector->setConnetionCb([&](int sockfd){
        connectedSock.store(sockfd);
        loop.quit();
    });
    connector->start();
    // After some retries, start a server and then restart connector.
    loop.runAfter(0.12, [&](){
        // Turn the reserved socket into a listening socket.
        int r = ::listen(reservedFd, 16);
        assert(r == 0);
        serverFd = reservedFd;
    });

    loop.runAfter(0.14, [&](){
        connector->restart();
    });

    loop.runAfter(0.6, [&](){ // safety timeout
        loop.quit();
    });
    loop.loop();

    assert(connectedSock.load() >= 0); // restart should have connected
    ::close(serverFd);
    ::close(connectedSock.load());
    std::cout << "  passed" << std::endl;
}

int main() {
    Logger::setLogLevel(Logger::LogLevel::DEBUG);
    std::cout << "ConnectorTest started" << std::endl;
    testSuccessfulConnect();
    testConnectionRefusedRetry();
    testStopBeforeConnect();
    testRestart();
    std::cout << "ConnectorTest ALL passed" << std::endl;
    return 0;
}

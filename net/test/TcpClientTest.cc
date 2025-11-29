// TcpClientTest.cc - tests for TcpClient: connect/echo, send/receive, disconnect, server close, reconnect
#include "TcpConn.h"
#include "TcpClient.h"
#include "EventLoop.h"
#include "InetAddress.hpp"
#include "EventLoopThread.h"

#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace mymoduo::net;
using namespace mymoduo;

static int createListeningSocket(uint16_t &portOut) {
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
    r = ::listen(fd, 16);
    assert(r == 0);
    sockaddr_in bound{}; socklen_t len = sizeof(bound);
    r = ::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len);
    assert(r == 0);
    portOut = ntohs(bound.sin_port);
    return fd;
}

// simple acceptor thread: accepts one connection and either echo or close depending on mode
static void runServerOnce(int listenFd, std::atomic<bool>& stopFlag, bool echoMode, std::string toSendOnConnect="") {
    // accept single connection then exit when stopFlag
    while(!stopFlag.load()) {
        sockaddr_in peer{}; socklen_t len = sizeof(peer);
        int conn = ::accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
        if(conn < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if(!toSendOnConnect.empty()) {
            ::send(conn, toSendOnConnect.data(), toSendOnConnect.size(), 0);
        }
        if(echoMode) {
            char buf[1024];
            while(true) {
                ssize_t n = ::recv(conn, buf, sizeof(buf), 0);
                if(n <= 0) break;
                ssize_t w = ::send(conn, buf, n, 0);
                if(w <= 0) break;
            }
            ::close(conn);
        } else {
            // immediate close to simulate server drop
            ::close(conn);
        }
        break;
    }
}

// Test 1: basic connect + echo send/receive
static void testEchoSendReceive() {
    std::cout << "[testEchoSendReceive]" << std::endl;
    uint16_t port{0};
    int listenFd = createListeningSocket(port);
    std::atomic<bool> stopFlag{false};
    std::thread server([&]{ runServerOnce(listenFd, stopFlag, true); });


    //在线程中创建EventLoop
    EventLoopThread loopThread(nullptr, "TcpClientTestLoopThread");
    mymoduo::net::EventLoop* loop = loopThread.startLoop(); //启动事件循环

    std::cout << "EventLoop started in thread " << loop->threadId() << std::endl;
    std::cout << "main thread " << CurrentThread::threadId() << std::endl;
    InetAddress addr(port, true);

    //tcpClient 的 tcpconnection 如何析构的
    TcpClient client(loop, addr, "tcptest");

    std::atomic<int> connected{0};
    std::atomic<int> messages{0};

    LOG_DEBUG << "setting Connection callbacks";
    client.setConnetionCb([&](const TcpConnPtr& conn){
        if(conn->connected()) {
            connected.store(1);
            conn->send(std::string("hello"));
        }
    });
    LOG_DEBUG << "setting Message callbacks";
    std::promise<int> prom;
    std::future<int> fut = prom.get_future();
    client.setMessageCb([&](const TcpConnPtr& conn, Buffer* buf, base::TimeStamp recvTime){
        (void)recvTime;
        std::string s = buf->retrieveAllAsString();
        if(s == "hello") messages.fetch_add(1);
        prom.set_value(0);
        conn->shutdown();
    });
    
    LOG_DEBUG << "starting TcpClient";
    client.start();
    loop->runAfter(1.0, [&]{ loop->quit(); });
  
    //如果是在当前线程运行loop的话
    //loop->loop();  //阻塞在此
    //这里会先执行loop.quit() 然后再执行client.stop(), 故资源不能清理完毕
    fut.get(); // wait for message received
    client.stop();

    assert(connected.load() == 1);
    assert(messages.load() >= 1);

    stopFlag.store(true);
    ::close(listenFd);
    if(server.joinable()) { server.join(); } 
    std::cout << "  passed" << std::endl;
}

// Test 2: server closes immediately after accept -> client should see connection then close
static void testServerCloses() {
    std::cout << "[testServerCloses]" << std::endl;
    uint16_t port{0};
    int listenFd = createListeningSocket(port);
    std::atomic<bool> stopFlag{false};
    std::thread server([&]{ runServerOnce(listenFd, stopFlag, false); });

    mymoduo::net::EventLoop loop;
    InetAddress addr(port, true);
    TcpClient client(&loop, addr, "tcptest");

    std::atomic<int> connected{0};
    std::atomic<int> closed{0};

    client.setConnetionCb([&](const TcpConnPtr& conn){
        if(conn->connected()) {
            connected.store(1);
        } else {
            closed.fetch_add(1);
            loop.quit();
        }
    });
    client.start();
    loop.runAfter(1.0, [&]{ loop.quit(); });
    loop.loop();
    // server closed immediately; client should have seen connected then disconnected
    client.stop();
    assert(connected.load() == 1);

    stopFlag.store(true);
    ::close(listenFd);
    if(server.joinable()) server.join();
    std::cout << "  passed" << std::endl;
}

// Test 3: stop before connect -> no connection made
static void testStopBeforeConnect() {
    std::cout << "[testStopBeforeConnect-TcpClient]" << std::endl;
    uint16_t port{0};
    int tmp = createListeningSocket(port);
    ::close(tmp); // ensure no listener

    mymoduo::net::EventLoop loop;
    std::cout << "EventLoop started" << std::endl;
    InetAddress addr(port, true);

    std::cout<< " creating TcpClient" << std::endl;
    TcpClient client(&loop, addr, "tcptest");
    std::cout<< "successfully create TcpClient" << std::endl;

    std::atomic<int> connected{0};
    std::cout<< "setting Connection callbacks" << std::endl;
    client.setConnetionCb([&](const TcpConnPtr& conn){ if(conn->connected()) connected.store(1); });
    
    std::cout<< "starting TcpClient" << std::endl;
    client.start();
    
    std::cout<< "TcpClient close" << std::endl;
    client.stop();
    std::cout<< "================================" << std::endl;
    std::cout<< "TcpClient stopped" << std::endl;
    std::cout<< "================================" << std::endl;
    

    loop.runAfter(10, [&]{ loop.quit(); });
    std::cout << "EventLoop loop" << std::endl;
    loop.loop();
    // ensure client cleanup
    std::cout<< "================================" << std::endl;
    LOG_DEBUG << "EventLoop already quit";

    client.stop();
    std::cout<< "================================" << std::endl;
    assert(connected.load() == 0);
    std::cout << "  passed" << std::endl;
}

// Test 4: reconnect after server appears
static void testReconnect() {
    std::cout << "[testReconnect]" << std::endl;
    // Reserve a port by creating a bound socket, then later listen on it
    uint16_t port{0};
    int reserveFd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(reserveFd >= 0);
    int on = 1;
    ::setsockopt(reserveFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addrBind{}; 
    addrBind.sin_family = AF_INET; 
    addrBind.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
    addrBind.sin_port = 0;
    int r = ::bind(reserveFd, reinterpret_cast<sockaddr*>(&addrBind), sizeof(addrBind));
    assert(r == 0);
    sockaddr_in bound{}; socklen_t len = sizeof(bound); 
    r = ::getsockname(reserveFd, reinterpret_cast<sockaddr*>(&bound), &len); 
    assert(r == 0);
    port = ntohs(bound.sin_port);
    mymoduo::net::EventLoop loop;
    InetAddress addr(port, true);
    TcpClient client(&loop, addr, "tcptest");

    std::atomic<int> connected{0};
    client.setConnetionCb([&](const TcpConnPtr& conn){ if(conn->connected()) { connected.store(1); conn->shutdown(); loop.quit(); }});

    client.start();
    // start listening after some delay so initial connect attempts fail
    loop.runAfter(0.12, [&]{ int l = ::listen(reserveFd, 16); assert(l == 0); /* server will accept */ });
    loop.runAfter(0.14, [&]{ client.enableRetry(); });
    loop.runAfter(2.0, [&]{ loop.quit(); });
    loop.loop();
    // ensure cleanup
    client.stop();
    assert(connected.load() == 1);
    ::close(reserveFd);
    std::cout << "  passed" << std::endl;
}

int main() {
    mymoduo::Logger::setLogLevel(mymoduo::Logger::LogLevel::ERROR);
    std::cout << "TcpClientTest started" << std::endl;

    testReconnect(); 
    testStopBeforeConnect();
    testEchoSendReceive();
    testServerCloses();
    std::cout << "TcpClientTest ALL passed" << std::endl;
    return 0;
}

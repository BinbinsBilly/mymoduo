#include "HeartBeat.h"
#include "EventLoop.h"
#include "TcpServer.h"
#include "TcpClient.h"
#include "socket.h"
#include <atomic>
#include <cassert>
#include <iostream>

using namespace mymoduo::net;

static uint16_t pickEphemeralPort()
{
    using mymoduo::net::Socket;
    auto tmpOpt = Socket::create(AF_INET, SOCK_STREAM, 0);
    assert(tmpOpt.has_value());
    Socket tmp = std::move(*tmpOpt);
    bool ok = tmp.bindaddress(mymoduo::net::InetAddress(0, true));
    assert(ok);
    struct sockaddr_in6 addr6 = Socket::getLocalAddr(tmp.fd());
    uint16_t port = ntohs(reinterpret_cast<struct sockaddr_in*>(&addr6)->sin_port);
    return port;
}

static void testUpdateTriggersRemoveCallback() {
    std::cout << "[testUpdateTriggersRemoveCallback]" << std::endl;
    std::atomic<int> removed{0};

    {
        mymoduo::net::EventLoop loop;
        // enable_shared_from_this要求shared管理: start()中需用shared_from_this安全捕获
        auto hb = std::make_shared<HeartBeat>(&loop, 4, 1.0);
        hb->start();
        hb->setRemoveConnectionCb([&](const TcpConnPtr&){
            removed.fetch_add(1);
            loop.quit();
        });

        uint16_t port = pickEphemeralPort();
        InetAddress serverAddr(port, true);
        TcpServer server(&loop, serverAddr, std::string("hb_server"), 4, 1.0);

        server.setConnectionCb([&hb](const TcpConnPtr& conn){
            if(conn)
            {
                conn->setHeartBeatUpdateCb([&hb](const TcpConnPtr& c){ hb->update(c); });
                conn->setHeartBeatRemoveCb([&hb](const TcpConnPtr& c){ hb->remove(c); });
            }
        });

        server.start();

        InetAddress cliAddr("127.0.0.1", port);
        TcpClient client(&loop, cliAddr, std::string("hb_client"));
        client.start();

        loop.runAfter(2.5, [&]{ loop.quit(); });
        loop.loop();
    }

    assert(removed.load() == 1);
}

// static void testRemoveSuppressesCallback() {
//     std::cout << "[testRemoveSuppressesCallback]" << std::endl;
//     std::atomic<int> removed{0};

//     {
//         mymoduo::net::EventLoop loop;
//         auto hb = std::make_shared<HeartBeat>(&loop, 4, 1.0);
//         hb->start();
//         hb->setRemoveConnectionCb([&](const TcpConnPtr&){ removed.fetch_add(1); });

//         uint16_t port = pickEphemeralPort();
//         InetAddress serverAddr(port, true);
//         TcpServer server(&loop, serverAddr, std::string("hb_server2"), 4, 1.0);

//         TcpConnPtr storedConn{nullptr};
//         server.setConnectionCb([&](const TcpConnPtr& conn){
//             if(conn)
//             {
//                 storedConn = conn;
//                 conn->setHeartBeatUpdateCb([&hb](const TcpConnPtr& c){ hb->update(c); });
//                 conn->setHeartBeatRemoveCb([&hb](const TcpConnPtr& c){ hb->remove(c); });
//             }
//         });

//         server.start();

//         InetAddress cliAddr("127.0.0.1", port);
//         TcpClient client(&loop, cliAddr, std::string("hb_client2"));
//         client.start();

//         // remove the connection shortly after creation
//         loop.runAfter(0.05, [&hb, &storedConn]{
//             if(storedConn)
//             {
//                 hb->remove(storedConn);
//             }
//         });

//         loop.runAfter(1.5, [&]{ loop.quit(); });
//         loop.loop();
//     }

//     assert(removed.load() == 0);
// }

//回归用例: HeartBeat销毁后tick定时器必须已被取消
//loop继续运行覆盖原tick周期, 不再触发对已析构对象的访问(旧实现定时器捕获裸this, 会每秒UAF一次)
static void testDestroyCancelsTickTimer() {
    std::cout << "[testDestroyCancelsTickTimer]" << std::endl;

    mymoduo::net::EventLoop loop;
    {
        auto hb = std::make_shared<HeartBeat>(&loop, 4, 1.0);
        hb->start();
        hb.reset(); //销毁HeartBeat: ~HeartBeat中cancel掉tick定时器
    }
    //等待2.5s, 覆盖原每秒一次的tick周期: 若定时器未取消, 每秒tick将访问已析构对象导致崩溃
    loop.runAfter(2.5, [&]{ loop.quit(); });
    loop.loop();
}

int main() {
    mymoduo::Logger::setLogLevel(mymoduo::Logger::LogLevel::ERROR);
    testUpdateTriggersRemoveCallback();
    testDestroyCancelsTickTimer();
    // testRemoveSuppressesCallback();
    std::cout << "HeartBeat tests passed." << std::endl;
    return 0;
}

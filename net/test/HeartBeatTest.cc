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
        HeartBeat hb(&loop, 4, 1.0);
        hb.setRemoveConnectionCb([&](const TcpConnPtr&){
            removed.fetch_add(1);
            loop.quit();
        });

        uint16_t port = pickEphemeralPort();
        InetAddress serverAddr(port, true);
        TcpServer server(&loop, serverAddr, std::string("hb_server"), 4, 1.0);

        server.setConnectionCb([&hb](const TcpConnPtr& conn){
            if(conn)
            {
                conn->setHeartBeatUpdateCb([&hb](const TcpConnPtr& c){ hb.update(c); });
                conn->setHeartBeatRemoveCb([&hb](const TcpConnPtr& c){ hb.remove(c); });
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
//         HeartBeat hb(&loop, 4, 1.0);
//         hb.setRemoveConnectionCb([&](const TcpConnPtr&){ removed.fetch_add(1); });

//         uint16_t port = pickEphemeralPort();
//         InetAddress serverAddr(port, true);
//         TcpServer server(&loop, serverAddr, std::string("hb_server2"), 4, 1.0);

//         TcpConnPtr storedConn{nullptr};
//         server.setConnectionCb([&](const TcpConnPtr& conn){
//             if(conn)
//             {
//                 storedConn = conn;
//                 conn->setHeartBeatUpdateCb([&hb](const TcpConnPtr& c){ hb.update(c); });
//                 conn->setHeartBeatRemoveCb([&hb](const TcpConnPtr& c){ hb.remove(c); });
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
//                 hb.remove(storedConn);
//             }
//         });

//         loop.runAfter(1.5, [&]{ loop.quit(); });
//         loop.loop();
//     }

//     assert(removed.load() == 0);
// }

int main() {
    mymoduo::Logger::setLogLevel(mymoduo::Logger::LogLevel::ERROR);
    testUpdateTriggersRemoveCallback();
    // testRemoveSuppressesCallback();
    std::cout << "HeartBeat tests passed." << std::endl;
    return 0;
}

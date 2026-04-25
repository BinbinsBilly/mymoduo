#include "TcpServer.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "InetAddress.hpp"
#include "TcpConn.h"
#include "Buffer.h"
#include "Logging.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace mymoduo::net;

struct Args {
    uint16_t port = 12346;
    size_t msgSize = 64;
    int concurrency = 1;
    size_t seconds = 5;
    int threads = 4;
};

static Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s(argv[i]);
        auto next = [&]{ return (i+1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (s == "--port") a.port = static_cast<uint16_t>(std::stoi(next()));
        else if (s == "--msg") a.msgSize = std::stoul(next());
        else if (s == "--conc") a.concurrency = std::stoi(next());
        else if (s == "--sec") a.seconds = std::stoul(next());
        else if (s == "--threads") a.threads = std::stoi(next());
    }
    return a;
}

int main(int argc, char** argv)
{
    //设置为DEBUG日志级别 
    mymoduo::Logger::setLogLevel(mymoduo::Logger::LogLevel::FATAL);
    //解析命令行参数
    Args args = parse(argc, argv);
    LOG_DEBUG << "pingpong_bench start port=" << args.port << " msg=" << args.msgSize
             << " conc=" << args.concurrency << " sec=" << args.seconds << " threads=" << args.threads;

    // 创建独立的事件循环线程，避免主线程被 loop() 阻塞导致后续客户端线程无法启动
    EventLoopThread loopThread(nullptr, "pingpong-server-loop");
    // EventLoop 创建时
    // 1.TimerQueue Channel 注册到监听树
    // 2.EventLoop 的 wakeup Channel 注册到监听树
    //EventLoop是在子线程中创建的
    mymoduo::net::EventLoop* loop = loopThread.startLoop();
    InetAddress addr(args.port, true, false);
    TcpServer server(loop, addr, "pingpong-bench", 0, 0);

    server.setThreadNum(args.threads);

    LOG_DEBUG << "setting message callback";
    server.setMessageCb([&](const TcpConnPtr& c, Buffer* buf, mymoduo::base::TimeStamp){
        if (buf->readableBytes() > 0) {
            c->send(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        }
    });

    LOG_DEBUG << "starting server";
    // acceptor Channel 注册到监听树
    server.start(); // 将 listen 操作投递到 loop 线程

    LOG_DEBUG << "setting up client threads";
    std::atomic<size_t> ops{0};
    std::string payload(args.msgSize, 'x');

    // client threads
    std::vector<std::thread> ths;
    for (int i = 0; i < args.concurrency; ++i) {
        ths.emplace_back([&, i]{
            int cfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
            sockaddr_in sa{}; 
            sa.sin_family = AF_INET; 
            sa.sin_port = htons(args.port);
            ::inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

            int ret = ::connect(cfd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
            if(ret == -1)
            {
                LOG_ERROR << "client connect error";
                return;
            }else{
                LOG_DEBUG << "client connected";
            }

            // server side will accept and create TcpConn; we just drive traffic via write() here for simplicity
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(args.seconds);
            while (std::chrono::steady_clock::now() < end) 
            {
                int nwrote = ::write(cfd, payload.data(), payload.size());
                if(nwrote <= 0)
                {
                    LOG_ERROR << "client write error";
                }else{
                    LOG_DEBUG << "client wrote " << nwrote << " bytes";
                }
                char buf[4096];// 
                int nread = ::read(cfd, buf, sizeof(buf));
                if(nread <= 0)
                {
                    LOG_ERROR << "client read error";
                }else{
                    LOG_DEBUG << "client read " << nread << " bytes";
                }
                ++ops;
            }
            LOG_INFO << "client thread " << i << " done";
            ::shutdown(cfd, SHUT_RDWR);
        });
    }

    // wait for clients to finish
    for (auto& t : ths) t.join();

    // 客户端完成后通知事件循环退出
    loop->quit();

    double secs = args.seconds;
    double opsPerSec = ops.load() / secs;
    
    std::cout<< "PingPong ops/s=" << opsPerSec << std::endl;

    //FIXME: 因为不是在主线程中创建的TcpServer, 所以这里TcpSercer
    //析构时会断言失败, 但不影响测试
    return 0;
}

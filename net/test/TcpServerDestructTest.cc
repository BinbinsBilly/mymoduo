// TcpServer 跨线程析构回归测试
// 复刻 example/pingpong_bench.cc 的退出模式:
// EventLoopThread 与 TcpServer 均在主线程栈上创建, EventLoop 运行于子线程,
// main 返回时按栈逆序析构(server 先、loopThread 后),
// 验证显式启用心跳(slots=4, timeout=2)时析构路径无断言崩溃、干净退出(退出码 0)。

#include "TcpServer.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "InetAddress.hpp"
#include "TcpConn.h"
#include "Buffer.h"
#include "Logging.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <thread>

using namespace mymoduo::net;

int main()
{
    //降低日志级别, 减少输出
    mymoduo::Logger::setLogLevel(mymoduo::Logger::LogLevel::FATAL);

    //选择不易冲突的端口
    const uint16_t port = 23456;

    //1. 栈上创建事件循环线程, EventLoop 在子线程中创建
    //注意: Poller.h 在全局作用域前向声明了 EventLoop, 需用全限定名避免歧义
    EventLoopThread loopThread(nullptr, "destruct-test-loop");
    mymoduo::net::EventLoop* loop = loopThread.startLoop();

    //2. 栈上创建 TcpServer: slots=4, timeout=2 显式启用心跳, 覆盖 HeartBeat 生命周期路径
    TcpServer server(loop, InetAddress(port, true, false), "destruct-test", 4, 2);
    server.setThreadNum(2);
    //收到数据直接取走(最简的回显等价处理)
    server.setMessageCb([](const TcpConnPtr&, Buffer* buf, mymoduo::base::TimeStamp){
        buf->retrieveAll();
    });
    server.start();

    //3. 主线程用原生 socket 连接 server, 写入一字节数据
    //注意: start() 将 listen 投递到 loop 线程异步执行, 连接需要有限次重试等待 listen 生效
    int cfd = -1;
    for (int retry = 0; retry < 50; ++retry)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
        if (fd < 0)
        {
            LOG_ERROR << "create client socket failed";
            return 1;
        }
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0)
        {
            cfd = fd;
            break;
        }
        //连接失败说明 listen 尚未生效, 关闭本次 socket 稍后重试
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (cfd < 0)
    {
        LOG_ERROR << "client connect failed";
        return 1;
    }
    const char byte = 'x';
    if (::write(cfd, &byte, 1) != 1)
    {
        LOG_ERROR << "client write failed";
    }
    //等待服务器处理数据
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    //关闭连接, 触发服务端对端关闭路径
    ::shutdown(cfd, SHUT_RDWR);
    ::close(cfd);
    //等待服务器处理断连
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    //4. 通知事件循环退出
    loop->quit();

    //5. main 返回 0: server 与 loopThread 按栈逆序析构(server 先、loopThread 后)
    return 0;
}

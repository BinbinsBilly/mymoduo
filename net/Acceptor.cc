#include "Acceptor.h"

namespace mymoduo
{
namespace net
{
    Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool /*reusePort*/)
        :loop_(loop)
        ,acceptSocket_(Socket::create(listenAddr.family(), SOCK_STREAM, IPPROTO_TCP).value())
        ,acceptChannel_(loop, acceptSocket_.fd())
        ,idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
        ,listenning_(false)
    {
        acceptSocket_.setNonblock(acceptSocket_.fd());
        acceptSocket_.setCloseOnExec(acceptSocket_.fd());
        acceptSocket_.bindaddress(listenAddr);
        acceptChannel_.setReadEventCb([this](base::TimeStamp /*timestamp*/) { this->handleRead(); });
    }

    Acceptor::~Acceptor()
    {
        acceptChannel_.disableAll();  //Channel ->> 从树上摘除
        acceptChannel_.remove();      //Channel ->> 从列表中删除
        if(idleFd_ >= 0)
        {
            ::close(idleFd_);
        }
    }

    void Acceptor::listen()
    {
        acceptChannel_.enableRead(); // Channel ->> 监听树
        bool ret = acceptSocket_.listen();      // socketfd ->> 等待连接
        LOG_DEBUG << "Acceptor::listen() - return " << ret;
        listenning_ = true;
    }

    void Acceptor::handleRead()
    {
        InetAddress peerAddr;
        // 显式启用非阻塞 + cloexec: 阻塞 socket 在发送缓冲满时会卡死整个 ioLoop
        std::optional<Socket> connSocket = acceptSocket_.accept(peerAddr, SetNonBlocking::ENABLE, SetCloseOnExec::ENABLE);
        if(connSocket)
        {
            if(cb_)
            {
                cb_(std::move(connSocket.value()), peerAddr);
            }
            else
            {
                //自动析构Socket
            }
        }
        else if(errno == EMFILE || errno == ENFILE)
        {
            // fd 耗尽: LT epoll 会持续报告监听 fd 可读造成忙循环,
            // 用 idleFd 泄洪: 释放一个槽位 accept 后立即关闭, 让对端得到及时反馈
            LOG_ERROR << "Acceptor::handleRead - accept failed with " << strerror(errno) << ", draining via idleFd";
            ::close(idleFd_);
            idleFd_ = ::accept(acceptSocket_.fd(), nullptr, nullptr);
            if(idleFd_ >= 0)
            {
                ::shutdown(idleFd_, SHUT_RDWR);
                ::close(idleFd_);
            }
            idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        }
    }
} // namespace net
} // namespace mymoduo
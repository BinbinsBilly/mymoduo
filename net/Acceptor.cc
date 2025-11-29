#include "Acceptor.h"

namespace mymoduo
{
namespace net
{
    Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool /*reusePort*/)
        :loop_(loop)
        ,acceptSocket_(Socket::create(listenAddr.family(), SOCK_STREAM, IPPROTO_TCP).value())
        ,acceptChannel_(loop, acceptSocket_.fd())
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
        std::optional<Socket> connSocket = acceptSocket_.accept(peerAddr);
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
    }
} // namespace net
} // namespace mymoduo
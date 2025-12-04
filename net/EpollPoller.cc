#include "EpollPoller.h"


namespace mymoduo
{
namespace net
{

    EpollPoller::EpollPoller(EventLoop* loop)
        :Poller(loop),
         epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
         events_(1024)
    {
       if(epollfd_ < 0)
       {
            LOG_ERROR << "epoll_create \n";
       } 
    }

    EpollPoller::~EpollPoller()
    {
        ::close(epollfd_);
    }

    base::TimeStamp EpollPoller::Poll(int timeoutms, ChannelList& activeChannels)
    {
        int eventNum = epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutms);
        base::TimeStamp now = base::TimeStamp::now();
        if(eventNum > 0)
        {
            fillActiveChannel(eventNum, activeChannels);
            if(eventNum == static_cast<int>(events_.size()))
            {
                events_.resize(events_.size() * 2);
            }
        }
        else if(eventNum == 0)
        {
            LOG_INFO << "Poll: nothing happen\n";
        }
        else
        {
            if(errno != EINTR)
            {
                LOG_ERROR << "Poll: epoll_wait\n";
                return now;
            }
        }
        return now;
    }

    void EpollPoller::fillActiveChannel(int eventNum, ChannelList& activeChannels)
    {
        for(int i = 0; i < eventNum; ++i)
        {
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->setrevents(static_cast<Event>(events_[i].events));

            activeChannels.push_back(channel);
        }
    }

    void EpollPoller::removeChannel(Channel& channel)
    {
        //assert
        int fd = channel.fd();

        channels_.erase(fd);    //从channels中删除
        if(channel.status() == static_cast<int>(ChannelStatus::New)
          || channel.status() == static_cast<int>(ChannelStatus::Deleted))
        {

        channel.setstatus(ChannelStatus::New);  // 修改channel状态
            // LOG_DEBUG << "EpollPoller::removeChannel - channel not added, no need to remove";
            return; //未添加或者已经被删除不需要删除
        }
        update(EpollOp::Delete, channel);   // 从监听中摘除
        channel.setstatus(ChannelStatus::New);  // 修改channel状态
    }

    void EpollPoller::updateChannel(Channel& channel)
    {
        int fd = channel.fd();
        int status = channel.status();
        //新的 和 之前被设置为deleted的都可以加入
        if(status == static_cast<int>(ChannelStatus::New) || status == static_cast<int>(ChannelStatus::Deleted))
        {
            // LOG_DEBUG << "add new Channel to EpollPoller\n";
            if(status == static_cast<int>(ChannelStatus::New))
            {
                //未加入要添加到channels
                channels_[fd] = &channel;
            }
                //已经在channel中的不重复添加, 只修改status
                channel.setstatus(ChannelStatus::Added);
                update(EpollOp::Add, channel);
        }
        //存在的 需要判断是否有事可做
        else
        {
            if(channel.isNoneEvent())
            {
                update(EpollOp::Delete, channel);
                channel.setstatus(ChannelStatus::Deleted);
            }
            else
            {
                update(EpollOp::Modify, channel);
            }
        }
    }

    void EpollPoller::update(EpollOp operation, Channel& channel)
    {
        struct epoll_event event;
        ::memset(&event, 0, sizeof(struct epoll_event));
        event.events = channel.events();
        event.data.ptr = &channel;

        if(::epoll_ctl(epollfd_, static_cast<int>(operation), channel.fd(), &event) < 0)
        {
            LOG_ERROR << " ERROR: " << strerror(errno) << " in ::epoll_ctl(), fd=" << channel.fd()
                      << " operation=" << static_cast<int>(operation)
                      << " events=" << channel.events() << "\n";
        } 
        // LOG_DEBUG << "EpollPoller::update() - fd=" << channel.fd()
        //           << " op=" << static_cast<int>(operation)
        //           << " event=" << channel.events();
    }
}
}
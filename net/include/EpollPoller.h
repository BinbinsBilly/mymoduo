/**
 * @file EpollPoller.h
 * @author sbilly 
 * @brief 事件分发器: 调用一次epoll_wait监听事件 并由分发给相应的channel处理
 *        核心操作: Poll(调用epoll_wait); fillActiveChannel(存储在一次监听事件中活跃的channel); updateChannel(更新channel在监听中的状态); removeChannel(删除channel) 
 *        辅助操作: update(更新channel在监听中的状态, 添加/修改/删除)
 * @version 0.1
 * @date 2025-11-23
 */


#pragma once 

#include "noncopyable.h"
#include "TimeStamp.h"
#include "Poller.h"
#include "Logging.h"
// Forward declare EventLoop instead of including to reduce cycles
namespace mymoduo { namespace net { class EventLoop; } }

#include <vector>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

namespace mymoduo
{
namespace net
{
    enum class Event : uint32_t;
    enum class EpollOp : int
    {
        Add = EPOLL_CTL_ADD,
        Delete = EPOLL_CTL_DEL,
        Modify = EPOLL_CTL_MOD
    };
    enum class ChannelStatus //用来判断channel是否在channels_中
    {
        New = -1,
        Added = 1,
        Deleted = 2          //代表被从监听树中摘除 非remove
    };

    class EpollPoller : public Poller
    {
    public:
        EpollPoller(EventLoop* loop);
        ~EpollPoller();

        //根据channel的状态  加入或保留在监听树或从中摘除 但仍在channel_中
        void updateChannel(Channel& channel) override;
        //从通道中删除 (同时要从监听中摘除)
        void removeChannel(Channel& channel) override;
        //epoll_wait调用 事件监听
        base::TimeStamp Poll(int timeoutms, ChannelList& activeChannels) override;
        
        // 辅助函数 更新channel在监听中的状态
        void update(EpollOp operation, Channel& channel);

        // 获取一次事件监听中的活跃channel,并设置revent
        void fillActiveChannel(int eventNum, ChannelList& activeChannels);
        
    private:
        using EventList = std::vector<struct epoll_event>;
        int epollfd_;
        EventList events_;
    }; //end EpollPoller
} // end net
} // end mymoduo
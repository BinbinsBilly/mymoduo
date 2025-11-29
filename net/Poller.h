/**
 * @file Poller.h
 * @author sbilly (
 * @brief EventPoller 抽象类
 */

#pragma once

#include "../base/noncopyable.h"
#include "../base/TimeStamp.h"
#include "Channel.h"

#include <vector>
#include <unordered_map>

// forward declare EventLoop to avoid circular include
namespace mymoduo { namespace net { class EventLoop; } }

class EventLoop;
namespace mymoduo
{
namespace net
{
    class Poller : noncopyable
    {
    public:

        using ChannelList = std::vector<Channel*>;  

        ~Poller() = default;

        virtual base::TimeStamp Poll(int timeoutms, ChannelList& activeChannels) = 0;

        virtual void updateChannel(Channel& channel) = 0;
        virtual void removeChannel(Channel& channel) = 0;

        virtual bool hasChannel(Channel& channel) const;
        static Poller* newDefaultPoller(EventLoop* loop);
        //!!! 
        void assertInLoopThread() const
        {
            //remain to implement
        }

    protected:
        Poller(EventLoop* loop)
            :ownerLoop_(loop)
        {}

        using ChannelMap = std::unordered_map<int, Channel*>; //fd -> channel
        ChannelMap channels_;
    private:
        EventLoop* ownerLoop_;
        
    }; //end Poller
} // end net
} // end mymoduo

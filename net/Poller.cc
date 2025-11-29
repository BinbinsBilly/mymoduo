#include "Poller.h"
#include "EpollPoller.h"

namespace mymoduo{
namespace net{

        bool Poller::hasChannel(Channel& channel) const 
        {
            auto it = channels_.find(channel.fd());
            //第二个是空判断
            return (it != channels_.end() && it->second == &channel);
        }
        
        Poller* Poller::newDefaultPoller(EventLoop* loop)
        {
            return new EpollPoller(loop);
        }
        
}
}
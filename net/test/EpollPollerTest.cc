// EpollPollerTest.cc - 拆分多测试函数验证 Channel 与 Poller 交互
#include "EpollPoller.h"
#include "Channel.h"
#include "Poller.h"
#include "TimeStamp.h"
#include "Logging.h"
#include "EventLoop.h"

#include <unistd.h>
#include <cassert>
#include <atomic>
#include <sys/eventfd.h>
#include <iostream>

using namespace mymoduo::net;
using namespace mymoduo::base;

static int createEventFd() {
    int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(efd >= 0);
    return efd;
}

// 执行一次写入并poll触发所有活动Channel
static void pollOnce(mymoduo::net::EventLoop& loop, uint64_t writeValue, int efd) {
    ::write(efd, &writeValue, sizeof(writeValue));
    Poller::ChannelList active;
    mymoduo::base::TimeStamp now = loop.poller()->Poll(50, active);
    for(auto* c : active) c->handleEvent(now);
}

static void testSingleRead() {
    std::cout << "[testSingleRead]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    std::atomic<int> count{0};
    Channel ch(&loop, efd);
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ count.fetch_add(1); });
    ch.enableRead();
    pollOnce(loop, 1, efd);
    assert(count.load() == 1);
    ch.disableAll(); ch.remove(); ::close(efd);
}

static void testDisableAllNoTrigger() {
    std::cout << "[testDisableAllNoTrigger]" << std::endl;
    mymoduo::net::EventLoop loop; int efd = createEventFd(); std::atomic<int> count{0};
    Channel ch(&loop, efd);
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ count.fetch_add(1); });
    ch.enableRead(); pollOnce(loop, 1, efd); 
    assert(count.load()==1);
    ch.disableAll(); pollOnce(loop, 1, efd); // 不应再触发
    assert(count.load()==1);
    ch.remove(); ::close(efd);
}

static void testReEnable() {
    std::cout << "[testReEnable]" << std::endl;
    mymoduo::net::EventLoop loop; int efd = createEventFd(); std::atomic<int> count{0};
    Channel ch(&loop, efd);
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ count.fetch_add(1); });
    ch.enableRead(); pollOnce(loop, 1, efd); 
    ch.disableAll(); pollOnce(loop,1,efd); 
    assert(count.load()==1);
    ch.enableRead(); pollOnce(loop, 1, efd); 
    assert(count.load()==2);
    ch.disableAll(); ch.remove(); ::close(efd);
}

static void testRemoveStopsEvents() {
    std::cout << "[testRemoveStopsEvents]" << std::endl;
    mymoduo::net::EventLoop loop; int efd = createEventFd(); 
    std::atomic<int> count{0};
    Channel ch(&loop, efd);
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ count.fetch_add(1); });
    ch.enableRead(); pollOnce(loop, 1, efd); 
    assert(count.load()==1);
    ch.disableAll(); ch.remove(); pollOnce(loop, 1, efd); // 已移除不应触发
    assert(count.load()==1);
    ::close(efd);
}

static void testBatchWrites() {
    std::cout << "[testBatchWrites]" << std::endl;
    mymoduo::net::EventLoop loop; int efd = createEventFd(); 
    std::atomic<int> count{0};
    Channel ch(&loop, efd);
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ count.fetch_add(1); });
    ch.enableRead();
    uint64_t one{1};
    // 连续写三次，在一次Poll内只触发一次（eventfd语义）
    ::write(efd, &one, sizeof(one));
    ::write(efd, &one, sizeof(one));
    ::write(efd, &one, sizeof(one));
    Poller::ChannelList active; 
    mymoduo::base::TimeStamp now = loop.poller()->Poll(50, active); for(auto* c:active) c->handleEvent(now);
    assert(count.load()==1);
    ch.disableAll(); ch.remove(); ::close(efd);
}

int main() {
    std::cout << "EpollPollerTest started" << std::endl;
    testSingleRead();
    testDisableAllNoTrigger();
    testReEnable();
    testRemoveStopsEvents();
    testBatchWrites();
    std::cout << "EpollPollerTest ALL passed" << std::endl;
    return 0;
}

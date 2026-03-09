// ChannelTest.cc
// 拆分多个独立测试函数：初始状态、读写事件使能与关闭、disableAll、remove、tie 与回调触发。

#include "Channel.h"
#include "EventLoop.h"
#include <cassert>
#include <sys/eventfd.h>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <atomic>

using namespace mymoduo::net;

static int createEventFd() {
    int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(efd >= 0);
    return efd;
}

static void testInitialState() {
    std::cout << "[testInitialState]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    Channel ch(&loop, efd);
    assert(ch.isNoneEvent());
    assert(!ch.isReading());
    assert(!ch.isWriting());
    ::close(efd);
}

static void testEnableDisableReadWrite() {
    std::cout << "[testEnableDisableReadWrite]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    Channel ch(&loop, efd);
    ch.enableRead();
    assert(ch.isReading());
    assert(!ch.isWriting());
    ch.enableWrite();
    assert(ch.isReading());
    assert(ch.isWriting());
    ch.disableWrite();
    assert(ch.isReading());
    assert(!ch.isWriting());
    ch.disableRead();
    assert(!ch.isReading());
    assert(!ch.isWriting());
    assert(ch.isNoneEvent());
    ::close(efd);
}

static void testDisableAll() {
    std::cout << "[testDisableAll]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    Channel ch(&loop, efd);
    ch.enableRead();
    ch.enableWrite();
    assert(ch.isReading() && ch.isWriting());
    ch.disableAll();
    assert(ch.isNoneEvent());
    ::close(efd);
}

static void testRemoveChannel() {
    std::cout << "[testRemoveChannel]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    Channel ch(&loop, efd);
    ch.enableRead();
    ch.disableAll();
    assert(ch.isNoneEvent());
    ch.remove();
    ::close(efd);
}

// 测试 tie 语义：被 tie 的对象销毁后应跳过回调，避免访问已析构对象。
static void testTieBehavior() {
    std::cout << "[testTieBehavior]" << std::endl;
    mymoduo::net::EventLoop loop;
    int efd = createEventFd();
    Channel ch(&loop, efd);
    std::atomic<int> readCount{0};
    ch.setReadEventCb([&](mymoduo::base::TimeStamp){ readCount.fetch_add(1); });
    {
        auto obj = std::make_shared<int>(42);
        ch.tie(obj);
        ch.enableRead();
    } // obj 超出作用域被销毁
    // 手动模拟一次 eventfd 可读并触发 handleEvent
    uint64_t one = 1; 
    ::write(efd, &one, sizeof(one));
    ch.setrevents(static_cast<int>(Event::Read));
    ch.handleEvent(mymoduo::base::TimeStamp::now());
    assert(readCount.load() == 0);
    ch.disableAll();
    ch.remove();
    ::close(efd);
}

int main() {
    std::cout << "ChannelTest started" << std::endl;
    testInitialState();
    testEnableDisableReadWrite();
    testDisableAll();
    testRemoveChannel();
    testTieBehavior();
    std::cout << "ChannelTest ALL passed" << std::endl;
    return 0;
}

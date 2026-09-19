# Harden Netlib Round2 Spec

## Why
第一轮生命周期修复后复审，仍发现 4 类 P0 问题（UAF 竞态、Channel/fd 泄漏路径、未启动线程析构崩溃、阻塞式 accepted socket 导致 ioLoop 卡死）及若干正确性缺陷，需在云端验证后提交。

## What Changes
- **P0-1** `HeartBeat::update/remove` 跨线程 queueInLoop 捕获裸 `this` → 改为 `shared_from_this()`（消除 TcpServer 析构后迟到 functor 的 UAF）
- **P0-2** `Connector::~Connector` 的 `delete rawChannel` functor 在 loop quit 后被丢弃 → Channel+fd 泄漏；改用 `shared_ptr<Channel>` 捕获（与 `removeAndResetChannel` 一致，functor 析构即释放）
- **P0-3** `EventLoopThread` 未 `startLoop` 即析构 → `thread_.join()` 对未启动线程断言崩溃；增加 `started()` 守卫
- **P0-4** `Socket::accept` 默认 `SetNonBlocking::DISABLE`，Acceptor 用默认值 → **accepted socket 为阻塞模式**，发送缓冲满时 `write()` 阻塞整个 ioLoop；Acceptor 显式传 `ENABLE`（nonblock + cloexec）
- **P1-5** `Acceptor` 无 EMFILE 处理 → fd 耗尽时 LT epoll 忙循环（100% CPU）；引入 muduo idleFd 方案
- **P1-6** `Socket::setRecvTimeout` 误用 `SO_SNDTIMEO` → 改 `SO_RCVTIMEO`
- **P1-7** `TcpServer::newConnection` getsockname 仅用 `sockaddr_in` → IPv6 服务器本地地址错误/截断；改用 `sockaddr_in6`（InetAddress 已支持）
- **P1-8** `~TcpClient` 主线程直接 `setConnetionCb({})` 与 loop 线程调用 cb 存在数据竞争；改经 `runInLoop` 在 loop 线程清空
- **P2-9** `EpollPoller::updateChannel` 对 `Deleted` 状态重新 Add 时不回填 `channels_` 映射 → `hasChannel` 失真
- **P2-10** `CHECK_NOT_NULL` LOG_FATAL 后仍返回 nullptr → 后续空指针解引用；FATAL 时 abort
- **P2-11** 清理：`TcpConn::handleHighWaterMark` 冗余捕获 `this`；`sockfdDeleter` 判断顺序 `*fd >= 0 && fd` 颠倒

## Impact
- Affected specs: fix-netlib-lifecycle-bugs（第一轮，已合并，本轮为增量）
- Affected code: `net/HeartBeat.cc`、`net/Connector.cc`、`net/EventLoopThread.cc`、`net/Acceptor.{h,cc}`、`net/socket.cpp`、`net/include/socket.h`、`net/TcpServer.cc`、`net/TcpClient.cc`、`net/EpollPoller.cc`、`net/TcpConn.cc`

## ADDED Requirements
### Requirement: 阻塞安全
Accepted 连接 socket SHALL 为非阻塞 + cloexec 模式，任何发送路径 SHALL NOT 阻塞 ioLoop 线程。

#### Scenario: 慢客户端背压
- **WHEN** 对端不读取数据导致发送缓冲满
- **THEN** `write()` 返回 EAGAIN，数据进入 outputBuffer_ 等待可写事件，ioLoop 继续处理其它事件

### Requirement: fd 耗尽降级
Acceptor SHALL 在 EMFILE/ENFILE 时通过 idleFd 机制泄洪，SHALL NOT 陷入 epoll 忙循环。

#### Scenario: EMFILE
- **WHEN** 进程 fd 达到 rlimit
- **THEN** accept 经 idleFd 释放一个槽位后继续，不出现 100% CPU 自旋

## MODIFIED Requirements
### Requirement: HeartBeat 线程安全
`HeartBeat::update/remove` 的跨线程转发 functor SHALL 持有 `shared_ptr`（或 weak_ptr lock），SHALL NOT 捕获裸 `this`。

### Requirement: Connector 析构不泄漏
`~Connector` 的 Channel 清理 functor SHALL 通过 `shared_ptr<Channel>` 持有所有权，确保 functor 被丢弃（loop quit）时随析构自动释放，不依赖 functor 执行。

### Requirement: EventLoopThread 析构安全
未启动（`startLoop` 未调用）的 EventLoopThread SHALL 可安全析构，不触发断言或 terminate。

### Requirement: IPv6 服务器正确性
`TcpServer::newConnection` SHALL 使用 `sockaddr_in6` 获取本地地址，IPv4/IPv6 监听均返回正确 localAddr。

### Requirement: 超时设置正确性
`Socket::setRecvTimeout` SHALL 设置 `SO_RCVTIMEO`；`setSendTimeout` 保持 `SO_SNDTIMEO`。

### Requirement: TcpClient 析构无线程竞争
`~TcpClient` 对 connector 回调的清空 SHALL 在 loop 线程执行（runInLoop 转发）。

### Requirement: Poller 映射一致
`EpollPoller::updateChannel` 在 Add（含 Deleted→Add）时 SHALL 同步维护 `channels_` 映射。

### Requirement: CHECK_NOT_NULL 语义
空指针命中 SHALL 记录 FATAL 日志后 abort，SHALL NOT 返回 nullptr 继续执行。

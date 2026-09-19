# mymoduo 网络库生命周期与断连 Bug 修复 Spec

## Why

本仓库是 C++ 改写的 muduo 网络库（base + net 两层）。经全面代码审读，确认存在多类真实缺陷：**对端关闭时连接被双重关闭**、**服务器无法跨线程优雅析构**（`example/pingpong_bench.cc` 中作者自己留有 FIXME：`TcpServer` 析构断言失败）、**EventLoop 在错误线程析构导致断言崩溃与 TLS 损坏**、**心跳时间轮定时器永不取消导致 UAF**、**客户端析构在 loop 停止后死锁**、以及若干 socket 层正确性 bug（空指针解引用、IPv6 自连接判断恒假）。这些问题直接影响库的可用性与退出安全。

## What Changes

全部为 bug 修复，不引入新功能、不改变公开 API 语义（除返回值类型修正）：

### A. 连接断开正确性
- `Channel::handleEventWithGuard`：改为 muduo 语义 —— 仅当 `POLLHUP` 且**不可读**时才调用 close 回调，消除 `handleClose` 双重执行（当前对端每断开一次，`connectionCb_/handleCloseCb_/heartBeatRemoveCb_` 被调用两次）。

### B. 优雅退出
- `EventLoopThread::threadFunc`：`loop()` 返回后在**loop 线程内**销毁 `EventLoop`（当前 `loop_` 成员 unique_ptr 在 `~EventLoopThread` 所在线程析构 → `~EventLoop` 跨线程触发 `Channel::remove` 断言崩溃，且 `t_LoopInThisThread = nullptr` 清错线程的 TLS）。
- `EventLoop` 成员声明顺序调整：`pendingFunctors_` 移到 `Poller_/timerQueue_` 之后声明，使其**先于** Poller 析构 —— 被 quit 丢弃的 functor（可能持有 Acceptor/Channel/TcpConn）析构时 Poller 仍存活，避免 UAF。
- `TcpServer::~TcpServer`：移除 `assertInLoopThread`；`acceptor_` 所有权移入投递给 baseLoop 的 functor，保证 `~Acceptor`（及其 channel 摘除）总在 loop 线程执行；成员声明顺序调整使 `heartBeat_` 晚于 `threadPool_` 析构。
- `TcpConn::~TcpConn`：若 `connectDestroyed` 从未执行（channel 仍在），在 loop 线程内做兜底 disableAll+remove，保证任意退出路径 fd 关闭、channel 摘除。
- `example/pingpong_bench.cc`：移除 FIXME 注释，验证修复后干净退出（exit code 0）。

### C. 死锁
- `Connector::stopAndWait` 与 `~TcpClient` 中的同步等待（`future::wait`）：改为**有界等待**（`wait_for`），loop 已停止时记录警告并继续析构，而不是永久阻塞。

### D. 内存泄漏 / 生命周期
- `HeartBeat`：保存 `runEvery` 返回的 sequence 并在析构时 `cancel`（当前 tick 定时器永不取消，loop 存活时每秒 UAF 一次）；`step` 计算改为 `ceil` 且下限 1（当前 `timeout < 1s` 时 step=0，连接被立即判超时）；`TcpServer` 中 `heartBeat_` 改为 `shared_ptr`，传给 `TcpConn` 的心跳回调捕获 `weak_ptr`，消除 TcpServer 析构后 stale functor 对裸 `this` 的解引用。
- `TimerQueue::cancelInLoop`：取消"正在执行的 repeat 定时器"时（该 timer 已被摘出索引），加入 `cancelList_`，阻止其在 `updateTimerList` 中被重启（当前自取消静默失效）。
- `TimerQueue` 内部存储 `unique_ptr<Timer>` 改为 `shared_ptr<Timer>`：`addTimer` 投递的 functor 被丢弃时不再泄漏裸 `new Timer`。
- `Socket::fd()`：`return *sockfd_ ? *sockfd_ : -1` 解引用可能为 nullptr 的指针（moved-from Socket 调用即崩溃），且 fd==0 被误判无效；改为 `sockfd_ && *sockfd_ >= 0` 判断。
- `Socket::selfConnection` IPv6 分支：`::memcpy(...) == 0` 恒为假（比较的是返回的指针），改为 `::memcmp(...) == 0`。

### E. 小修正
- `TcpConn` 构造：删除对**已连接 socket** 的 `setReuseAddr/setReusePort/bindaddress`（bind 对已连接 socket 必然 EINVAL 被静默忽略，属无效系统调用），仅保留 `setKeepAlive`。
- `Buffer::readT/peekT`：声明与定义的返回类型为 `void` 但函数体内 `return result;`，一旦实例化即编译错误；修正为返回 `T`。
- `Connector::handleWrite`：非 `kConnecting` 状态分支不再把状态改写为 `kDisconnected`（避免污染经 error 事件成功建立连接后的状态），仅记录日志。

### F. 验证与提交
- 补充回归测试（Channel 双事件、TimerQueue 自取消、TcpServer 跨线程析构等）。
- 本地全量验证：构建零警告、ctest 全绿、`pingpong_bench` 干净退出、ASan 构建抽查无报错。
- 新增 GitHub Actions workflow（ubuntu + cmake 构建 + ctest），push 当前分支 `trae/agent-Iv1k76` 到 origin，用 `gh` 监控云端 CI 通过后完成提交。

## Impact

- Affected code:
  - [net/Channel.cc](file:///workspace/net/Channel.cc)
  - [net/EventLoop.cc](file:///workspace/net/EventLoop.cc) 与 [net/include/EventLoop.h](file:///workspace/net/include/EventLoop.h)（成员顺序）
  - [net/EventLoopThread.cc](file:///workspace/net/EventLoopThread.cc)
  - [net/TcpServer.cc](file:///workspace/net/TcpServer.cc) 与 [net/include/TcpServer.h](file:///workspace/net/include/TcpServer.h)
  - [net/HeartBeat.cc](file:///workspace/net/HeartBeat.cc) 与 [net/include/HeartBeat.h](file:///workspace/net/include/HeartBeat.h)
  - [net/TimerQueue.cc](file:///workspace/net/TimerQueue.cc) 与 [net/include/TimerQueue.h](file:///workspace/net/include/TimerQueue.h)
  - [net/TcpConn.cc](file:///workspace/net/TcpConn.cc)
  - [net/Connector.cc](file:///workspace/net/Connector.cc)、[net/TcpClient.cc](file:///workspace/net/TcpClient.cc)
  - [net/socket.cpp](file:///workspace/net/socket.cpp)
  - [net/Buffer.cc](file:///workspace/net/Buffer.cc) 与 [net/include/Buffer.h](file:///workspace/net/include/Buffer.h)
  - [example/pingpong_bench.cc](file:///workspace/example/pingpong_bench.cc)
  - net/test/ 新增回归测试；新增 .github/workflows/ci.yml
- 无 **BREAKING** 变更：`Buffer::readT/peekT` 此前不可用（实例化即编译失败），修正返回类型属于纠错而非破坏。

## ADDED Requirements

### Requirement: 对端关闭事件单次分发
系统在收到 `POLLHUP` 且同时可读（`POLLIN|POLLPRI`）的事件时，SHALL 只触发读回调（由 `read()==0` 走 `handleClose`），不得同时触发 close 回调；仅在 `POLLHUP` 且不可读时直接触发 close 回调。

#### Scenario: 对端正常关闭（有/无未读数据）
- **WHEN** 对端调用 `close()/shutdown(WR)`，epoll 返回 `EPOLLIN|EPOLLHUP`
- **THEN** `TcpConn::handleClose` 恰好执行一次；`connectionCb_`、`handleCloseCb_`、`heartBeatRemoveCb_` 各恰好被调用一次；连接从 `TcpServer::connectionMap_` 正常移除

### Requirement: EventLoop 生命周期与线程亲和
`EventLoop` SHALL 在其所属 loop 线程内析构；`EventLoopThread` 析构时 SHALL 先 quit 并 join 线程，且不跨线程触碰 `EventLoop` 及其 `t_LoopInThisThread` TLS。

#### Scenario: EventLoopThread 正常启停
- **WHEN** `EventLoopThread` startLoop 后调用其析构（任意线程）
- **THEN** loop 线程内完成 `~EventLoop`（channel 摘除断言全部通过），主线程 TLS 中已注册的其它 EventLoop 不被误清除，进程无断言崩溃

### Requirement: TcpServer 跨线程优雅析构
`TcpServer` SHALL 支持在其 baseLoop 运行于其它线程时被析构：acceptor 的 channel 摘除 SHALL 在 baseLoop 线程执行；所有存量连接 SHALL 被投递 `connectDestroyed`（执行与否取决于 ioLoop 是否尚在运行，未执行时由 `~TcpConn` 兜底清理）。

#### Scenario: benchmark 退出路径
- **WHEN** 按当前 `pingpong_bench` 模式：EventLoopThread 承载 baseLoop、`loop->quit()` 后在主线程析构 `TcpServer` 与 `EventLoopThread`
- **THEN** 全程无断言失败、无崩溃，进程 exit code 为 0

### Requirement: 心跳时间轮安全析构
`HeartBeat` 析构时 SHALL 取消其 tick 定时器；心跳回调 SHALL 通过 `weak_ptr` 访问心跳对象，宿主（TcpServer）析构后残留的回调 SHALL 安全变为 no-op；超时槽位计算 SHALL 对 `timeout < 1s` 钳制为 1 且对非整数秒向上取整。

#### Scenario: TcpServer（启用心跳）析构后 baseLoop 仍在运行
- **WHEN** 启用心跳的 TcpServer 析构、tick 定时器下一个到期点到达
- **THEN** 不发生 use-after-free；残留的 update/remove 回调 no-op

### Requirement: 定时器自取消生效
通过 `EventLoop::cancel(sequence)` 取消一个**正在执行回调的 repeat 定时器**时，该定时器 SHALL 不再被重启。

#### Scenario: repeat 定时器回调内取消自身
- **WHEN** `runEvery` 的回调中调用 `cancel(自身 sequence)`
- **THEN** 该定时器只执行这一次，之后不再触发

### Requirement: 客户端析构不死锁
`TcpClient` 析构与 `Connector::stopAndWait` SHALL 使用有界等待；当所属 loop 已停止导致等待超时时，SHALL 记录警告并继续完成析构（不泄漏 `TcpConn` 引用，不永久阻塞）。

#### Scenario: loop 停止后析构 TcpClient
- **WHEN** loop 已 quit/线程已退出后析构 `TcpClient`
- **THEN** 析构在有限时间内完成，无死锁

### Requirement: Socket 层正确性
`Socket::fd()` SHALL 对 moved-from 对象安全返回 -1 且接受 fd==0 为合法描述符；IPv6 自连接检测 SHALL 使用 `memcmp` 比较（原 `memcpy(...) == 0` 恒假）。

### Requirement: 云端 CI 验证
仓库 SHALL 提供 GitHub Actions workflow（Linux + CMake 构建 + ctest），所有修复推送后 SHALL 在 GitHub Actions 上全绿。

#### Scenario: push 后 CI 通过
- **WHEN** 修复分支推送到 origin
- **THEN** `gh run watch` 确认 workflow 构建与全部测试通过

## MODIFIED Requirements

（无 —— 本变更全部为缺陷修复，不修改既有规格。）

## REMOVED Requirements

（无。）

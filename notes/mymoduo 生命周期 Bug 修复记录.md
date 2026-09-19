---
title: mymoduo 网络库生命周期 Bug 修复记录
date: 2026-09-20
tags:
  - mymoduo
  - network-library
  - bugfix
  - cpp
aliases:
  - 生命周期修复记录
branch: trae/agent-Iv1k76
status: merged
---

# mymoduo 网络库生命周期 Bug 修复记录

> [!abstract] 概述
> 本次修改对 cpp 改写的 moduo 网络库进行了全面的生命周期审查与修复，覆盖 **9 类 bug**：双重关闭、悬空访问（UAF）、内存泄漏、死锁、无法优雅退出等问题。所有修复经过本地三配置（Debug / Release / ASan+UBSan）验证与 GitHub Actions 云端 CI 双矩阵验证，全部通过。

## 修复清单

### 1. 连接断开 — Channel 双重关闭

> [!bug] 问题
> 对端关闭时 epoll 同时返回 `POLLHUP | POLLIN`，`Channel::handleEventWithGuard` 会先触发读回调（read()==0 → handleClose），又触发 close 回调，导致连接被关闭两次。

**修复**（`net/Channel.cc`）：close 回调条件改为 `(revent_ & POLLHUP) && !(revent_ & (POLLIN | POLLPRI))` —— 仅 HUP 且不可读时走 close 路径。

```cpp
// 仅当 HUP 且不可读时才走 close 路径，可读时由 read()==0 触发 handleClose，避免双重关闭
if((revent_ & POLLHUP) && !(revent_ & (POLLIN | POLLPRI)))
{
    if(cCb_) { cCb_(); }
}
```

### 2. 优雅退出 — EventLoop 线程亲和

> [!bug] 问题
> `EventLoop` 在 `EventLoopThread` 析构线程（而非 loop 线程）被销毁，触发 `assert(isInLoopThread())` 失败。

**修复**（`net/EventLoopThread.cc`）：`threadFunc()` 中 `loop()->loop()` 返回后在**自身线程内** `loop_.reset()`，用 `std::counting_semaphore<1>` 同步析构时序：

```cpp
void EventLoopThread::threadFunc()
{
    loop_ = std::make_unique<EventLoop>();
    loopPromise_.set_value(loop_.get());
    if(cb_) { cb_(loop_.get()); }
    loop_->loop(); // 事件循环 一去不返
    destructReleaseSem_.release(); // 通知析构线程可以继续
    loop_.reset(); // 在 loop 线程内销毁
}
```

同时调整 `EventLoop.h` 成员声明顺序，保证 `pendingFunctors_` 先于 `Poller_`、`timerQueue_`、`wakeupChannel_` 析构——被丢弃的 functor 在 loop 线程内安全销毁。

### 3. 优雅退出 — TcpServer 跨线程析构

> [!bug] 问题
> `~TcpServer` 含 `assertInLoopThread`，跨线程析构直接断言失败；acceptor/heartBeat 生命周期与 loop 线程不匹配。

**修复**（`net/TcpServer.{h,cc}`）：
- 删除析构断言；`acceptor_` move 进 `runInLoop` functor，由 loop 线程销毁
- `heartBeat_` 改为 `std::shared_ptr<HeartBeat>`，声明移至 `threadPool_` 之前（析构晚于线程池 join）
- 心跳回调捕获 `std::weak_ptr<HeartBeat>`，TcpServer 析构后残留回调 no-op

### 4. 内存泄漏 — HeartBeat 析构后 tick 定时器 UAF

> [!bug] 问题
> `HeartBeat` 析构时未取消 tick 定时器，loop 仍在运行则回调访问已析构对象。

**修复**（`net/HeartBeat.cc`）：保存 `tickTimerSeq_ = loop_->runEvery(...)`，析构时 `loop_->cancel(tickTimerSeq_)`；step 计算改为 `std::max(1, (int)std::ceil(timeout_))` 处理小数/超时钳制。

### 5. 内存泄漏 + 逻辑 — TimerQueue 自取消与泄漏

> [!bug] 问题
> ① repeat 定时器在回调中取消自身：到期处理时已被移出 `sequenceIndex_`，`cancelInLoop` 查不到 → 仍被重启。② `addTimer` functor 被丢弃时 `new Timer` 泄漏。

**修复**（`net/TimerQueue.cc`）：
```cpp
if(mapIt == sequenceIndex_.end())
{
    if(callingExpired_)
    {
        cancelList_.insert(sequence); // 延迟取消，阻止其重启
    }
    return;
}
```
内部存储改为 `std::shared_ptr<Timer>`，消除泄漏。

### 6. 崩溃 — Socket 层

> [!bug] 问题
> ① `Socket::fd()` 对 moved-from Socket 解引用空指针。② IPv6 自连接检测用 `memcpy(...)==0`（恒假）。

**修复**（`net/socket.cpp`）：
```cpp
return (sockfd_ && *sockfd_ >= 0) ? *sockfd_ : -1;   // fd()
memcmp(&peerAddr.sin6_addr, &localAddr.sin6_addr, sizeof(...)) == 0  // IPv6
```

### 7. 生命周期 — TcpConn

**修复**（`net/TcpConn.cc`）：
- 构造删除对已连接 socket 无效的 `setReuseAddr/setReusePort/bindaddress`（保留 `setKeepAlive`）
- `~TcpConn` 兜底：`connectDestroyed` 未执行时（channel 仍存在）在 loop 线程内 `disableAll()+remove()`

### 8. 死锁 — TcpClient / Connector

> [!warning] 问题
> loop 已停止时析构 `TcpClient` / `Connector::stopAndWait` 使用 `future::wait()` 无限期阻塞 → 死锁。

**修复**：改为 `wait_for(std::chrono::seconds(2))` 有界等待，超时记录 LOG_WARN；`handleWrite` 非 `kConnecting` 分支移除 `setState(kDisconnected)` 状态污染。

### 9. 悬空访问 — base/Thread（ASan 复核新发现）

> [!danger] 问题
> `Thread::start()` 线程 lambda 捕获 `this` 并访问 `name_`、`func_`。Thread 对象 detach 析构后线程仍在执行 `func_()` → **stack-use-after-scope**（ASan 报告）。

**修复**（`base/Thread.cc`）：functor 与线程名 `std::move` 进线程 lambda 自身所有权：

```cpp
ThreadFunc func = std::move(func_);
std::string name = name_.empty() ? std::string("mymoduoThread") : name_;
thread_ = std::thread([&threadIdPromise, func = std::move(func), name = std::move(name)]() mutable
{ ... });
```

### 其他小修正

- `Buffer::readT/peekT` 返回类型 `void` → `T`，模板定义移入头文件（修复链接错误）
- `pingpong_bench` FIXME 注释移除，验证干净退出

## 验证结果

> [!success] 全部通过
> - **本地**：Debug / Release / ASan+UBSan 三配置 ctest 均 **22/22 通过**，`-Wall -Wextra` 零新增警告
> - **基准**：`pingpong_bench --port 22222 --msg 64 --conc 8 --sec 3 --threads 4` 干净退出（exit 0，约 14 万 ops/s）
> - **云端 CI**：GitHub Actions 双矩阵（Release + ASan+UBSan）全绿，运行 ID `35458019834`、`35458096096`

## 提交信息

| 提交 | 内容 |
|---|---|
| `43417db` | 9 类生命周期 bug 修复 + 回归测试 + CI workflow（29 文件，+572/-114） |
| `91d772f` | ci: actions/checkout 升级 v5，消除 Node.js 20 弃用警告 |

分支 `trae/agent-Iv1k76` → 已合并进 `main`。

## 新增回归测试

- `net/test/TcpServerDestructTest.cc` — TcpServer 跨线程析构（含心跳）
- ChannelTest：`POLLHUP\|POLLIN` 不触发 closeCb / 仅 `POLLHUP` 恰好触发 1 次
- TimerQueueIntegrationTest：repeat 回调内取消自身只执行一次
- SocketTest：moved-from fd 与 selfConnection 用例
- HeartBeatTest：析构后定时器不再 tick

## 相关链接

- 修复详情见仓库 `net/`、`base/` 源码
- CI 配置：`.github/workflows/ci.yml`
- 仓库：[BinbinsBilly/mymoduo](https://github.com/BinbinsBilly/mymoduo)

^fix-record-2026-09

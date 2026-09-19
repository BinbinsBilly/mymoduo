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
> 对 cpp 改写的 moduo 网络库进行两轮全面审查与修复：
> - **第一轮**（`43417db`）：覆盖 **9 类 bug**——双重关闭、悬空访问（UAF）、内存泄漏、死锁、无法优雅退出等。
> - **第二轮**（`3c3af64`）：在第一轮基础上继续排查**跨线程 UAF、资源泄漏、栈越界、阻塞 I/O、数据竞争**等，共修复 10 项，其中 `Buffer::readFd` 栈越界是 ASan 大消息压测中新发现的 P0。
>
> 两轮修复均经过本地三配置（Debug / Release / ASan+UBSan）验证与 GitHub Actions 云端 CI 双矩阵验证，全部通过，已合并进 `main`。

## 第一轮修复清单

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

## 第一轮验证结果

> [!success] 全部通过
> - **本地**：Debug / Release / ASan+UBSan 三配置 ctest 均 **22/22 通过**，`-Wall -Wextra` 零新增警告
> - **基准**：`pingpong_bench --port 22222 --msg 64 --conc 8 --sec 3 --threads 4` 干净退出（exit 0，约 14 万 ops/s）
> - **云端 CI**：GitHub Actions 双矩阵（Release + ASan+UBSan）全绿，运行 ID `35458019834`、`35458096096`

## 第一轮提交信息

| 提交 | 内容 |
|---|---|
| `43417db` | 9 类生命周期 bug 修复 + 回归测试 + CI workflow（29 文件，+572/-114） |
| `91d772f` | ci: actions/checkout 升级 v5，消除 Node.js 20 弃用警告 |

分支 `trae/agent-Iv1k76` → 已合并进 `main`。

## 第一轮新增回归测试

- `net/test/TcpServerDestructTest.cc` — TcpServer 跨线程析构（含心跳）
- ChannelTest：`POLLHUP\|POLLIN` 不触发 closeCb / 仅 `POLLHUP` 恰好触发 1 次
- TimerQueueIntegrationTest：repeat 回调内取消自身只执行一次
- SocketTest：moved-from fd 与 selfConnection 用例
- HeartBeatTest：析构后定时器不再 tick

## 第二轮加固（内存安全 + 正确性）

> [!abstract] 第二轮目标
> 在第一轮基础上，继续排查**跨线程悬空访问（UAF）、资源泄漏、栈越界、阻塞 I/O、数据竞争**等隐患，共修复 10 项；其中 `Buffer::readFd` 栈越界是在 ASan 大消息压测中**新发现**的 P0。所有修复经三配置本地验证 + 云端 CI 双矩阵全绿，已合并进 `main`（`3c3af64`）。

### R2-1. HeartBeat 跨线程 UAF —— functor 捕获裸 `this`

> [!bug] 问题
> `HeartBeat::update`/`remove` 在非 loop 线程被调用时，`queueInLoop([this, conn]() { this->update(conn); })`。`TcpServer` 析构会移交 `HeartBeat` 所有权，若 HeartBeat 在 functor 执行前析构，迟到的 functor 解引用悬垂 `this` → **use-after-free**。

**修复**（`net/HeartBeat.cc`）：捕获 `shared_from_this()` 延长对象生命周期，保证 functor 执行时对象存活：

```cpp
loop_->queueInLoop([self = shared_from_this(), conn]() {
    self->update(conn);
});
```

> [!example] 跨线程生命周期时序
> ```mermaid
> sequenceDiagram
>     participant Caller as 非loop线程
>     participant Queue as EventLoop::pendingFunctors
>     participant Loop as loop线程
>     participant HB as HeartBeat
>
>     Note over Caller,HB: 修复前：functor 持裸 this
>     Caller->>Queue: queueInLoop([this, conn])
>     Caller->>HB: 析构(this 失效)
>     Loop->>Queue: 取出 functor
>     Loop->>HB: this->update(conn) ❌ UAF
>
>     Note over Caller,HB: 修复后：functor 持 shared_ptr
>     Caller->>Queue: queueInLoop([self, conn])
>     Caller->>HB: reset(最后一个外部引用)
>     Note right of HB: 引用计数: functor 仍持有 self → 对象存活
>     Loop->>Queue: 取出 functor
>     Loop->>HB: self->update(conn) ✅ 安全
>     Loop->>HB: functor 析构 → 对象释放
> ```

### R2-2. Connector 析构泄漏 —— loop quit 时 functor 被丢弃

> [!bug] 问题
> `~Connector` 把 `Channel*` 按值捕获进清理 functor 再 `delete`。若 `EventLoop` 已退出，functor 随 `pendingFunctors_` 整体丢弃，`Channel` 与其持有的 fd **永远泄漏**。

**修复**（`net/Connector.cc`）：用 `std::shared_ptr<Channel> guard(rawChannel)` 按值捕获；functor 执行或被丢弃时，`guard` 析构自动释放 Channel：

```cpp
std::shared_ptr<Channel> guard(rawChannel);
loop_->runInLoop([guard, sockfd]() {
    Channel* ch = guard.get();
    if (!ch->isNoneEvent()) ch->disableAll();
    ch->remove();
    ::close(sockfd);
});
```

### R2-3. EventLoopThread 未启动析构崩溃

> [!bug] 问题
> 构造后未调用 `startLoop()` 直接析构，`thread_.join()` 对未启动线程触发 `std::terminate`。

**修复**（`net/EventLoopThread.cc`）：析构开头加 `started()` 守卫：

```cpp
EventLoopThread::~EventLoopThread() {
    if (!thread_.started()) { return; }   // 未启动直接跳过
    // ... 既有 quit + release + join ...
}
```

### R2-4. Acceptor accepted socket 阻塞 + fd 耗尽

> [!bug] 问题
> ① `accept` 返回的连接 socket 未设非阻塞，send 缓冲满时 `write` 阻塞 → ioLoop 线程饿死。② 进程 fd 耗尽（EMFILE/ENFILE）时 `accept` 持续失败，epoll 边沿触发下陷入**忙循环**。

**修复**（`net/Acceptor.cc` + `net/include/Acceptor.h`）：
- accept 显式 `SetNonBlocking::ENABLE, SetCloseOnExec::ENABLE`
- 维护 `idleFd_`（`/dev/null`）：fd 耗尽时关闭 idleFd 腾出槽位 → accept 一次拿到对端 fd → `shutdown`+`close` 让对端感知 → 重新 open `/dev/null` 恢复泄洪能力

### R2-5. ⚠️ Buffer::readFd 栈越界（ASan 大消息压测新发现 P0）

> [!danger] 问题
> 当 `readv` 返回 `n > writable` 时，原代码：
> ```cpp
> writerIndex_ += writeableBytes();          // 先移动写指针
> append(extrabuf, n - writeableBytes());    // writeableBytes() 已为 0 → append(extrabuf, n)
> ```
> `extrabuf` 是 **65536 字节的栈数组**，`n` 最大可达 `writable + 65536`，`append(extrabuf, n)` 从栈上**越界读取**，既造成栈越界访问又导致数据重复/损坏。该 bug 在大消息（1MB）ASan 压测中触发。

**修复**（`net/Buffer.cc`）：在修改 `writerIndex_` 之前**先保存 writable**，后续全部使用该常量：

```cpp
const size_t writable = writeableBytes();   // 快照
vec[0].iov_len = writable;
...
} else {
    writerIndex_ += writable;
    append(extrabuf, n - writable);          // 用快照值，正确
}
```

> [!example] readv 双 iov 布局与越界示意
> ```mermaid
> graph LR
>     subgraph vec[readv iovec[2]]
>         v0[vec0: 主缓冲区<br/>writable 字节]
>         v1[vec1: extrabuf 栈数组<br/>65536 字节]
>     end
>     FD[(fd)] -->|readv n 字节| v0
>     v0 -->|写满 writable| v1
>
>     subgraph bug[修复前 ❌]
>         B1[writerIndex_ += writable<br/>writeableBytes()==0]
>         B2[append extrabuf n-0 == n<br/>n > 65536 → 栈越界读]
>         B1 --> B2
>     end
>
>     subgraph fix[修复后 ✅]
>         F1[const writable = writeableBytes<br/>快照不变]
>         F2[append extrabuf n-writable<br/>≤ 65536]
>         F1 --> F2
>     end
> ```

### R2-6. Socket 层正确性

> [!bug] 问题
> ① `setRecvTimeout` 误用 `SO_SNDTIMEO`（应为接收超时）。② `sockfdDeleter` 判空顺序错误：`if(*fd >= 0 && fd)` 在 `fd==nullptr` 时先解引用。

**修复**（`net/include/socket.h`）：
```cpp
::setsockopt(*sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));   // ①
if (fd && *fd >= 0) { ::close(*fd); }                               // ②
```

### R2-7. TcpServer —— IPv6 本地地址 + 空指针 abort

**修复**（`net/TcpServer.cc`）：
- `newConnection` 取本端地址改用 `sockaddr_in6`，兼容 IPv6 监听
- `CHECK_NOT_NULL` 在空指针时 `LOG_FATAL` 后 `::abort()`（与 muduo 语义一致，不再返回 nullptr 导致后续 NPE）

### R2-8. TcpClient 析构数据竞争

> [!bug] 问题
> `~TcpClient` 主线程直接 `connector_->setConnetionCb({})`，与 loop 线程 `handleWrite` 读取该回调存在数据竞争。

**修复**（`net/TcpClient.cc`）：清空回调投递到 loop 线程执行：

```cpp
loop_->runInLoop([this]() {
    connector_->setConnetionCb(Connector::ConnectorCb{});
});
```

### R2-9. EpollPoller Deleted→Add 映射失真

> [!bug] 问题
> `removeChannel` 把 `Deleted` 状态的 fd 从 `channels_` 移除；之后重新 `Add` 时，原代码仅 `New` 状态回填映射，`Deleted→Add` 不回填 → `hasChannel` 返回 false，状态不一致。

**修复**（`net/EpollPoller.cc`）：`New` 与 `Deleted→Add` 统一回填 `channels_[fd] = &channel;`。

### R2-10. TcpConn handleHighWaterMark 冗余捕获

**修复**（`net/TcpConn.cc`）：functor 移除冗余 `this` 捕获，仅保留 `guardThis`（`shared_from_this()`），执行时复查 `highWaterMarkCb_` 存在性。

## 第二轮验证结果

> [!success] 全部通过
> - **本地**：Debug / Release / ASan+UBSan 三配置 ctest **22/22 通过**，`-Wall -Wextra` 零新增警告
> - **大消息背压**：`pingpong_bench --msg 1048576 --conc 4 --sec 5 --threads 4`（ASan）exit 0、无 sanitizer 报错，验证非阻塞 accept + readFd 修复
> - **冒烟吞吐**：`pingpong_bench --msg 64` Release 约 23.3 万 ops/s，无回归
> - **云端 CI**：分支 run `35460208506`（Release 54s ✓ + ASan+UBSan 58s ✓）；合并 main 后 run `35460278516` 全绿
>
> 注：ASan bench 曾出现一次 exit 137（SIGKILL），经查为容器 4GiB memcg 的 OOM kill（anon-rss 达上限），非代码内存安全缺陷。

## 第二轮提交信息

| 提交 | 内容 |
|---|---|
| `3c3af64` | round 2 hardening：跨线程 UAF、Channel/fd 泄漏、readFd 栈越界、accept 非阻塞+idleFd、SO_RCVTIMEO 等 10 项 + 4 组回归测试（18 文件，+379/-43） |
| `76739ce` | docs: tick round-2 spec checklist |

分支 `trae/agent-Iv1k76` → 已合并进 `main`。

## 第二轮新增回归测试

- `BufferTest::testReadFdExtrabufOverflow` —— 100KB 数据走双 iov 路径，逐字节校验无越界/无重复
- `HeartBeatTest::test_update_after_destruct_no_uaf` —— 跨线程 update 排队后立即析构无崩溃
- `EventLoopThreadTest::test_destruct_without_start` —— 未 startLoop 直接析构安全
- `SocketTest::test_accept_nonblock_cloexec` —— accept 返回 fd 含 `O_NONBLOCK | FD_CLOEXEC`

## 相关链接

- 修复详情见仓库 `net/`、`base/` 源码
- CI 配置：`.github/workflows/ci.yml`
- 仓库：[BinbinsBilly/mymoduo](https://github.com/BinbinsBilly/mymoduo)

^fix-record-2026-09

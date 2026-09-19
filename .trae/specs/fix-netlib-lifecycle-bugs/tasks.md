# Tasks

> 执行原则：每个任务独立可验证；T1–T7 按文件划分互不冲突，可并行；T8/T9 依赖前序完成。
> 统一约束：C++20、`-Wall -Wextra` 零新增警告；不改变公开 API 语义；遵循各文件既有代码风格。

- [x] Task 1: 修复 Channel 事件分发双重关闭（net/Channel.cc）
  - [x] 1.1 `handleEventWithGuard`：close 回调条件改为 `(revent_ & POLLHUP) && !(revent_ & (POLLIN | POLLPRI))`，移除 `POLLRDHUP` 参与 close 条件的分支（epoll 未注册 RDHUP，属死代码；保留 read 分支中 `POLLRDHUP` 无害但不触发）
  - [x] 1.2 在 net/test/ChannelTest.cc 增加回归用例：`POLLHUP|POLLIN` 时 closeCb 计数为 0、rCb 触发；仅 `POLLHUP` 时 closeCb 计数为 1

- [x] Task 2: 修复 EventLoop 线程亲和与析构顺序（net/EventLoopThread.cc、net/include/EventLoop.h）
  - [x] 2.1 `threadFunc`：`loop_->loop()` 返回后在 loop 线程内 `loop_.reset()`（EventLoop 在自身线程析构）；`~EventLoopThread` 保持 quit+join，join 后不再触碰 `loop_`
  - [x] 2.2 `EventLoop.h`：调整成员声明顺序，使 `pendingFunctors_` 在 `Poller_`、`timerQueue_`、`wakeupChannel_` **之后**声明（析构先于它们），并添加注释说明原因
  - [x] 2.3 确认 EventLoopThreadTest 既有用例通过；如有断言依赖旧析构时序则同步修正测试

- [x] Task 3: 修复 TcpServer/HeartBeat 生命周期（net/TcpServer.{h,cc}、net/HeartBeat.{h,cc}）
  - [x] 3.1 `~TcpServer`：删除 `assertInLoopThread`；将 `acceptor_` move 进投递给 `loop_` 的 functor（`runInLoop`，loop 线程内执行 `~Acceptor`；被丢弃则在 `~EventLoop` 的 pendingFunctors 析构中于 loop 线程销毁，依赖 Task 2.2 的顺序保证）
  - [x] 3.2 `TcpServer.h`：`heartBeat_` 改为 `std::shared_ptr<HeartBeat>`，且声明位置移到 `threadPool_` 之前（析构晚于线程池 join）
  - [x] 3.3 `newConnection` 中心跳回调捕获 `std::weak_ptr<HeartBeat>`，lock 失败即 no-op
  - [x] 3.4 `HeartBeat`：保存 `tickTimerSeq_ = loop_->runEvery(...)`；析构时 `loop_->cancel(tickTimerSeq_)`（cancel 内部已 runInLoop，跨线程安全）；tick 定时器捕获 weak_from_this，析构后 no-op
  - [x] 3.5 `HeartBeat::update` 的 `step` 改为 `std::max(1, (int)std::ceil(timeout_))`
  - [x] 3.6 HeartBeatTest 既有用例通过；新增"析构后定时器不再 tick"回归用例（借助 runAfter 观察若干秒无崩溃）

- [x] Task 4: 修复 TimerQueue 自取消与泄漏（net/TimerQueue.{h,cc}）
  - [x] 4.1 `cancelInLoop`：`sequenceIndex_` 未命中且 `callingExpired_` 为真时，将 sequence 插入 `cancelList_`（正在执行的 repeat 定时器取消后不再被 `updateTimerList` 重启）
  - [x] 4.2 内部存储改为 `shared_ptr<Timer>`（TimerList/SequenceIndex/expired 向量及比较器、`addTimer` 的 functor 捕获），消除 functor 被丢弃时 `new Timer` 泄漏
  - [x] 4.3 TimerQueueIntegrationTest/TimerTest 通过；新增"repeat 回调内取消自身只执行一次"回归用例

- [x] Task 5: 修复 Socket 层与 TcpConn 构造（net/socket.cpp、net/include/socket.h、net/TcpConn.cc）
  - [x] 5.1 `Socket::fd()`：`return (sockfd_ && *sockfd_ >= 0) ? *sockfd_ : -1;`（isvalid() 同步修正）
  - [x] 5.2 `Socket::selfConnection` IPv6 分支：`memcpy(...)==0` 改为 `memcmp(&peerAddr.sin6_addr, &localAddr.sin6_addr, sizeof(...)) == 0`
  - [x] 5.3 `TcpConn` 构造：删除 `setReuseAddr/setReusePort/bindaddress` 三行（对已连接 socket 无效），保留 `setKeepAlive`
  - [x] 5.4 `~TcpConn`：`connectDestroyed` 未执行时（`connChannel_` 仍存在）在 loop 线程内兜底 `disableAll()+remove()`；非 loop 线程仅记录警告
  - [x] 5.5 SocketTest/TcpConnTest 通过（新增 moved-from fd 与 selfConnection 用例）

- [x] Task 6: 修复 TcpClient/Connector 死锁与状态污染（net/TcpClient.cc、net/Connector.cc）
  - [x] 6.1 `Connector::stopAndWait`：`f.wait()` 改为 `f.wait_for(std::chrono::seconds(2))`（promise 用 shared_ptr），超时记录 LOG_WARN 后返回
  - [x] 6.2 `~TcpClient`：conn 的同步销毁同样改为有界等待；超时后继续析构（functor 捕获 shared_ptr，安全）
  - [x] 6.3 `Connector::handleWrite` 非 `kConnecting` 分支：移除 `setState(kDisconnected)`，仅记录日志
  - [x] 6.4 TcpClientTest/ConnectorTest/TcpClientRetryStressTest 通过

- [x] Task 7: 修复 Buffer 模板签名（net/include/Buffer.h、net/Buffer.cc）
  - [x] 7.1 `readT/peekT` 返回类型由 `void` 修正为 `T`（enable_if 保持），实例化编译通过
  - [x] 7.2 BufferTest 通过并补充 readT/peekT 用例

- [x] Task 8: 端到端回归与本地全量验证
  - [x] 8.1 新增 net/test/TcpServerDestructTest.cc（注册进 CMakeLists）：EventLoopThread + TcpServer（含心跳）+ 建立连接 + quit + 主线程析构，断言无崩溃、退出码 0
  - [x] 8.2 全量 `cmake --build` 零错误、无新增警告；`ctest` 全绿（22/22）
  - [x] 8.3 `pingpong_bench --port 22222 --msg 64 --conc 8 --sec 3 --threads 4` 干净退出（exit 0），移除 bench 中 FIXME 注释
  - [x] 8.4 ASan 构建跑 ctest 与 bench 抽查（`-fsanitize=address,undefined`），无报错

- [x] Task 8b: 修复 base/Thread 析构后线程悬空访问（ASan 复核时发现，阻塞 Task 8.4）
  - [x] 8b.1 `Thread::start()`：functor 与线程名 `std::move` 进线程 lambda 自身所有权，不再捕获 `this`；Thread 对象 detach 析构后线程执行 `func_()` 不再悬空（ASan: stack-use-after-scope）
  - [x] 8b.2 `thread_tests` ASan 下通过；全量 ctest（常规 + ASan）22/22 复验通过

- [x] Task 9: 云端 CI 与提交
  - [x] 9.1 新增 .github/workflows/ci.yml：ubuntu-latest，cmake 配置构建 + ctest（Release 与 ASan 两个矩阵项或分步）
  - [x] 9.2 本地全量验证通过后 git commit（分支 trae/agent-Iv1k76），push 到 origin
  - [x] 9.3 用 `gh run watch`/`gh pr checks` 监控 GitHub Actions 全绿；失败则按日志修复后重新提交推送，直至通过（两次运行均全绿；actions/checkout 升级 v5 消除 Node.js 20 弃用警告）

# Task Dependencies
- Task 1–7 相互独立，可并行
- Task 8 依赖 Task 1–7 全部完成
- Task 9 依赖 Task 8

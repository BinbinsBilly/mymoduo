# Tasks

> 执行原则：每项独立可验证；T1–T6 按文件划分互不冲突可并行；T7 依赖前序；T8/T9 依赖全部完成。
> 统一约束：C++20、`-Wall -Wextra` 零新增警告；不改变公开 API 语义（P0-4 仅改 Acceptor 调用点，不改 accept 默认参数以免影响潜在调用方）。

- [x] Task 1: 修复 HeartBeat 跨线程 UAF（net/HeartBeat.cc）
  - [x] 1.1 `update`/`remove` 的 queueInLoop functor 由 `[this, conn]` 改为 `[self = shared_from_this(), conn]`（类已继承 enable_shared_from_this）
  - [x] 1.2 HeartBeatTest 既有用例通过；新增"跨线程 update 排队后立即析构 HeartBeat 无崩溃"回归用例

- [x] Task 2: 修复 Connector 析构泄漏（net/Connector.cc）
  - [x] 2.1 `~Connector` 三条清理路径中 `delete rawChannel` 改为 `std::shared_ptr<Channel> guard(rawChannel)` 按值捕获进 functor（isInLoopThread 路径与跨线程路径统一），functor 被丢弃时随析构释放
  - [x] 2.2 `loop_` 为空的兜底分支直接释放保持不变（guard 析构时释放，无 functor 参与）
  - [x] 2.3 ConnectorTest/TcpClientTest/TcpClientRetryStressTest 通过

- [x] Task 3: 修复 EventLoopThread 未启动析构崩溃（net/EventLoopThread.cc）
  - [x] 3.1 `~EventLoopThread`：`thread_.join()` 前增加 `if (thread_.started())` 守卫（early return 同时覆盖 sem release，未启动时不会执行到）
  - [x] 3.2 EventLoopThreadTest 新增"构造后不 startLoop 直接析构"回归用例
  - [x] 3.3 EventLoopThreadPool 失败回退路径（threads_.clear()）复验：启动成功的线程正常 quit+join

- [x] Task 4: 修复 accepted socket 阻塞模式（net/Acceptor.cc）
  - [x] 4.1 `Acceptor::handleRead` 的 `accept(peerAddr)` 改为 `accept(peerAddr, SetNonBlocking::ENABLE, SetCloseOnExec::ENABLE)`
  - [x] 4.2 SocketTest 新增"accept 返回的 fd 为 O_NONBLOCK + FD_CLOEXEC"断言用例
  - [x] 4.3 pingpong_bench 复验吞吐无回归（Release 冒烟 233K ops/s @ msg=64）

- [x] Task 5: 修复 Socket/Acceptor 正确性（net/socket.cpp、net/include/socket.h、net/Acceptor.{h,cc}、net/TcpServer.cc、net/TcpClient.cc）
  - [x] 5.1 `setRecvTimeout`：`SO_SNDTIMEO` → `SO_RCVTIMEO`
  - [x] 5.2 `Acceptor` 引入 idleFd（构造 `::open("/dev/null", O_RDONLY | O_CLOEXEC)`，析构 close）；accept 失败时 Acceptor 侧 `errno` 复查 EMFILE/ENFILE，执行 `idleFd` 泄洪（::close(idleFd); ::accept...; ::shutdown...; 重新 open /dev/null）
  - [x] 5.3 `TcpServer::newConnection`：`sockaddr_in local` → `sockaddr_in6 local`，`InetAddress localaddr(local)` 走既有 sockaddr_in6 构造
  - [x] 5.4 `~TcpClient`：`connector_->setConnetionCb(Connector::ConnectorCb{})` 移入 `loop_->runInLoop(...)`（在 stopAndWait 之前投递）；TcpClientTest 通过

- [x] Task 6: 修复 Poller 映射与 TcpServer 空指针（net/EpollPoller.cc、net/TcpServer.cc、net/TcpConn.cc、net/include/socket.h）
  - [x] 6.1 `EpollPoller::updateChannel`：Add 分支（New 或 Deleted）统一 `channels_[fd] = &channel;`
  - [x] 6.2 `CHECK_NOT_NULL`：FATAL 日志后 `::abort()`（与 muduo 语义一致）
  - [x] 6.3 `TcpConn::handleHighWaterMark` functor 移除冗余 `this` 捕获（保留 guardThis，并在执行时复查回调存在）
  - [x] 6.4 `sockfdDeleter`：`if(*fd >= 0 && fd)` → `if(fd && *fd >= 0)`
  - [x] 6.5 相关既有测试全部通过（EpollPollerTest/ChannelTest/TcpConnTest）

- [x] Task 7: 回归测试补充与本地全量验证
  - [x] 7.1 新增/更新回归测试：HeartBeat 析构竞态（1.2）、EventLoopThread 未启动析构（3.2）、accept 非阻塞+cloexec（4.2）
  - [x] 7.2 全量 `cmake --build`（Debug + Release）零错误、无新增警告；`ctest` 全绿（22/22）
  - [x] 7.3 ASan+UBSan 构建 ctest 全绿（22/22）+ pingpong_bench 干净退出（exit 0）
  - [x] 7.4 针对 P0-4 补充压力场景：pingpong 加大 --msg 1MB 验证慢消费下无卡死（ASan 下 exit 0、无报错）
  - [x] 7.5 追加（验证中发现的新 P0）：Buffer::readFd 栈越界——append 长度误用已变更的 `writeableBytes()`，从 65536 字节栈上 extrabuf 越界读取；已修复（先保存 `writable`）并补充 `testReadFdExtrabufOverflow` 回归测试

- [x] Task 8: 云端 CI 与提交
  - [x] 8.1 提交到分支 trae/agent-Iv1k76，push 到 origin
  - [x] 8.2 `gh run watch` 监控 GitHub Actions 全绿（run 35460208506：Release 54s ✓ + ASan+UBSan 58s ✓）
  - [x] 8.3 全绿后合并进 main（fast-forward 3c3af64，main CI run 35460278516 亦全绿）

# Task Dependencies
- Task 1–6 相互独立，可并行
- Task 7 依赖 Task 1–6
- Task 8 依赖 Task 7

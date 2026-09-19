# Checklist

## P0 修复
- [x] HeartBeat::update/remove 跨线程 functor 持有 shared_ptr，无裸 this 捕获
- [x] HeartBeat 析构后迟到的 update/remove functor 为 no-op（回归测试覆盖）
- [x] ~Connector 清理 functor 用 shared_ptr<Channel> 按值捕获，loop quit 丢弃时随析构释放（无 Channel/fd 泄漏）
- [x] 未 startLoop 的 EventLoopThread 可安全析构（回归测试覆盖）
- [x] Acceptor accept 的连接 socket 为 O_NONBLOCK | FD_CLOEXEC（回归测试覆盖）
- [x] 大消息背压场景（--msg 1MB）ioLoop 不卡死，bench 正常完成退出
- [x] Buffer::readFd 先保存 writable 再修改 writerIndex_，extrabuf 无栈越界/数据重复（回归测试覆盖）

## P1 修复
- [x] Acceptor EMFILE/ENFILE 经 idleFd 泄洪，无 epoll 忙循环
- [x] setRecvTimeout 设置 SO_RCVTIMEO（不再误用 SO_SNDTIMEO）
- [x] TcpServer::newConnection 使用 sockaddr_in6，IPv6 监听 localAddr 正确
- [x] ~TcpClient 清空 connector 回调在 loop 线程执行，无数据竞争

## P2 修复
- [x] EpollPoller Add（含 Deleted→Add）回填 channels_ 映射，hasChannel 一致
- [x] CHECK_NOT_NULL 空指针时 FATAL + abort，不返回 nullptr
- [x] handleHighWaterMark functor 无冗余 this 捕获
- [x] sockfdDeleter 判断顺序修正（先判空再解引用）

## 验证
- [x] 全量构建零错误、无新增警告（-Wall -Wextra，Debug + Release）
- [x] ctest 全部通过（含新增回归测试，22/22 × Debug/Release/ASan+UBSan）
- [x] ASan+UBSan 构建下 ctest 与 pingpong_bench（--msg 1MB）无报错
- [x] pingpong_bench 吞吐无回归（Release msg=64 冒烟 233K ops/s）
- [x] 修复已 commit 并 push 到 origin
- [x] GitHub Actions 云端 CI 全绿（gh 验证，run 35460208506）
- [x] 合并进 main 且 main CI 全绿（run 35460278516）

# Checklist

## 连接断开正确性
- [x] Channel 在 `POLLHUP|POLLIN` 时只触发读回调，close 回调计数为 0（回归测试覆盖）
- [x] Channel 在仅 `POLLHUP` 时 close 回调恰好触发 1 次
- [x] 对端关闭时 `TcpConn::handleClose` 只执行一次，`connectionCb_/handleCloseCb_/heartBeatRemoveCb_` 各一次

## 优雅退出
- [x] EventLoop 在其自身 loop 线程内析构（threadFunc 中 loop() 返回后销毁）
- [x] EventLoopThread 析构后主线程 TLS 中其它 EventLoop 注册不被误清除
- [x] EventLoop 成员析构顺序：pendingFunctors_ 先于 Poller_/timerQueue_ 析构
- [x] TcpServer 可在跨线程析构，无 assertInLoopThread 断言
- [x] acceptor 的 channel 摘除总在 baseLoop 线程执行（functor 持有所有权）
- [x] ~TcpConn 对未执行 connectDestroyed 的连接做兜底 channel 清理（仅 loop 线程）
- [x] pingpong_bench 全流程 exit code 0，FIXME 注释已移除

## 死锁
- [x] loop 停止后析构 TcpClient 不再永久阻塞（有界等待 + 警告日志）
- [x] Connector::stopAndWait 同样有界等待

## 内存泄漏 / 生命周期
- [x] HeartBeat 析构时取消 tick 定时器
- [x] 心跳回调通过 weak_ptr 访问，TcpServer 析构后残留回调 no-op
- [x] HeartBeat 超时槽位：timeout<1 钳制为 1，非整数秒向上取整
- [x] 正在执行的 repeat 定时器自取消后不再重启（回归测试覆盖）
- [x] TimerQueue 被丢弃的 addTimer functor 不泄漏 Timer 对象（shared_ptr 存储）
- [x] Socket::fd() 对 moved-from Socket 返回 -1，fd==0 合法
- [x] Socket::selfConnection IPv6 使用 memcmp 比较
- [x] Thread 对象 detach 析构后线程执行 functor 不悬空（functor/name 移入线程所有权，ASan 复验通过）

## 小修正
- [x] TcpConn 构造不再对已连接 socket 调用 setReuseAddr/setReusePort/bindaddress
- [x] Buffer::readT/peekT 返回 T，实例化可编译（测试覆盖）
- [x] Connector::handleWrite 非 kConnecting 分支不再污染状态

## 验证
- [x] 全量构建零错误、无新增警告（-Wall -Wextra）
- [x] ctest 全部通过（含新增回归测试，常规/Release/ASan 均 22/22）
- [x] ASan+UBSan 构建下 ctest 与 pingpong_bench 无报错
- [x] .github/workflows/ci.yml 存在且语法有效
- [x] 修复已 commit 并 push 到 origin（分支 trae/agent-Iv1k76）
- [x] GitHub Actions 云端 CI 全绿（gh 验证：Release ✓ / ASan+UBSan ✓，run 35458019834 与 35458096096）

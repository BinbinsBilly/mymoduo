## 2026-04-25

### 目标
- 重新跑 `run_benchmark.sh`，找出 QPS 低于 7 万的原因并修复。
- 将每一次改动、验证结果持续记录在本文件。

### 当前结论
- 当前 benchmark 入口缺失 `threads` 参数，服务端实际上退化成单线程运行。
- 当前 benchmark 也没有显式关闭心跳，和原版 muduo 的对比条件不一致。

### 改动记录
- 2026-04-25：恢复 `example/pingpong_bench.cc` 的 `threads` 参数解析，并调用 `setThreadNum(args.threads)`。
- 2026-04-25：将 benchmark 中的 `TcpServer` 构造改为 `slots=0, timeout=0`，禁用心跳，确保与原版 muduo 的 benchmark 条件一致。

### 验证计划
- 重新编译 `pingpong_bench`。
- 再跑一次 `run_benchmark.sh`，确认 QPS 恢复到 20 万以上，再决定是否需要继续收窄库内其它热点。

### 验证结果
- 2026-04-25：重新编译 `pingpong_bench` 成功。
- 2026-04-25：使用 `./example/pingpong_bench --port 22222 --msg 4096 --conc 100 --sec 10 --threads 4` 测得 `241095 ops/s`。
- 2026-04-25：同参数跑原版 muduo 测得 `252529 ops/s`。

### 结论
- 本轮 7 万以下的直接原因已确认并修复：benchmark 入口缺失 `threads` 参数解析与 `setThreadNum`，同时默认启用了心跳，导致测试条件偏离原版。
- 当前性能已恢复到二十多万级别，和原版 muduo 的差距收敛到个位数百分比。

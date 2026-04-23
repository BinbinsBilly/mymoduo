#!/bin/bash
set -e

# ==========================================
# C++ 网络库 (mymoduo) 性能与压测工具脚本
# ==========================================

echo "==> 准备构建目录..."
mkdir -p build && cd build

echo "==> 使用 Release 模式并开启调试符号(便于生成火焰图)进行编译..."
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer"
make pingpong_bench -j$(nproc)

echo "==> 编译完成！"

cd example

# ==========================================
# 1. 吞吐量 (QPS) 压测
# ==========================================
echo "==> 1. 开始多线程基准吞吐量测试 (QPS 测试)..."
echo "并发: 100, 消息大小: 4096 bytes, 压测时间: 10s, 服务端线程: 4"
./pingpong_bench --port 12349 --msg 4096 --conc 100 --sec 10 --threads 4 > qps_result.txt
echo "吞吐量测试结果："
cat qps_result.txt
echo ""

# ==========================================
# 2. CPU 热点函数剖析 (perf) & 3. 生成火焰图
# ==========================================
echo "==> 2. 准备进行性能采样 (生成火焰图)..."
echo "请注意：perf 工具可能需要 root 权限才能运行。"

if [ ! -d "/tmp/FlameGraph" ]; then
    echo "正在克隆 FlameGraph 工具..."
    git clone https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
fi

echo "开始采样..."
if [ "$EUID" -ne 0 ]; then
    echo "使用 sudo 请求权限以运行 perf record..."
    sudo perf record -F 99 -g -o perf.data -- ./pingpong_bench --port 12350 --msg 4096 --conc 100 --sec 10 --threads 4
else
    perf record -F 99 -g -o perf.data -- ./pingpong_bench --port 12350 --msg 4096 --conc 100 --sec 10 --threads 4
fi

echo "正在生成火焰图 (cpu-flamegraph.svg)..."
if [ "$EUID" -ne 0 ]; then
    sudo perf script -i perf.data | /tmp/FlameGraph/stackcollapse-perf.pl | tr -d '\000-\010\013\014\016-\037' | /tmp/FlameGraph/flamegraph.pl > cpu-flamegraph.svg
else
    perf script -i perf.data | /tmp/FlameGraph/stackcollapse-perf.pl | tr -d '\000-\010\013\014\016-\037' | /tmp/FlameGraph/flamegraph.pl > cpu-flamegraph.svg
fi

echo ""
echo "=========================================="
echo "测试完成！"
echo "1. 吞吐量报告见输出或 build/example/qps_result.txt"
echo "2. 火焰图已生成至: build/example/cpu-flamegraph.svg (用浏览器打开即可查看 CPU 热点)"
echo "=========================================="

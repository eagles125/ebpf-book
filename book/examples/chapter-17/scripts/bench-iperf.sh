#!/usr/bin/env bash
# 吞吐压测：依赖 setup-veth.sh 拓扑与 iperf3。运行级待验证（编写环境无 root）。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 用法：sudo bash bench-iperf.sh [时长秒]
set -euo pipefail
DURATION=${1:-30}
# 时长校验：仅接受正整数
case "$DURATION" in
    ''|*[!0-9]*|0) echo "用法: $0 [时长秒]（需为正整数）"; exit 1 ;;
esac
ip netns exec ns-server iperf3 -s >/dev/null 2>&1 &   # 后台服务端（不用 -D，前台后台化以便记录 PID）
IPERF_PID=$!
trap 'kill "$IPERF_PID" 2>/dev/null || true' EXIT     # 退出时只清理本脚本启动的服务端
ip netns exec ns-client iperf3 -c 10.0.2.2 -P 8 -t "$DURATION"
# 指标口径：SUM 行 sender/receiver 吞吐（Gbits/sec）与重传数；对照 fwctl stats 的 pass/drop

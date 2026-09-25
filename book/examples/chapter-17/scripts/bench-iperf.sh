#!/usr/bin/env bash
# 吞吐压测：依赖 setup-veth.sh 拓扑与 iperf3。运行级待验证（编写环境无 root）。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 用法：sudo bash bench-iperf.sh [时长秒]
set -euo pipefail
DURATION=${1:-30}
ip netns exec ns-server iperf3 -s -D          # 后台服务端
trap 'pkill iperf3 2>/dev/null || true' EXIT
ip netns exec ns-client iperf3 -c 10.0.2.2 -P 8 -t "$DURATION"
# 指标口径：SUM 行 sender/receiver 吞吐（Gbits/sec）与重传数；对照 fwctl stats 的 pass/drop

#!/usr/bin/env bash
# pktgen 打流压测 pps/丢包率：必须 root；仅作用于 veth（默认 veth-fw-c）。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 运行级待验证：pktgen 依赖 /proc/net/pktgen，无法在编写环境验证，以脚本+预期输出交付。
# 用法：sudo bash bench-pktgen.sh [dev] [时长秒]
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "需要 root（sudo）运行"; exit 1; }   # 写 /proc/net/pktgen 需 root
DEV=${1:-veth-fw-c}; SECS=${2:-10}
PG=/proc/net/pktgen
# 依赖检查：count 的微秒换算需要 bc，缺失时报错并给出安装提示
command -v bc >/dev/null 2>&1 || { echo "缺少依赖：bc 未安装，请先安装（如 apt install bc 或 dnf install bc）后重试"; exit 1; }
modprobe pktgen 2>/dev/null || true
[ -d "$PG" ] || { echo "内核未启用 pktgen"; exit 1; }
echo "rem_device_all"                              > "$PG/kpktgend_0"
echo "add_device ${DEV}@0"                         > "$PG/kpktgend_0"
D="$PG/${DEV}@0"
echo "count $(echo "$SECS"*1000000 | bc)"          > "$D"   # 发包个数：count 按包数计（非微秒），按 1 Mpps 假设折算 SECS 秒
echo "delay 0"                                     > "$D"
echo "pkt_size 64"                                 > "$D"
echo "dst_mac 00:00:00:00:00:02"                   > "$D"   # 按拓扑实际对端 MAC 修改
echo "dst 10.0.0.2"                                > "$D"
echo "start"                                       > "$PG/pgctrl"
grep -E "Result|pps" "$D"
# 指标口径：pps（发送速率）、acked/dropped。
# 方向注意：默认对 veth-fw-c 打流走 TX 路径（veth-fw-c 发送→ns-client），
# 不经过 veth-fw-c 的 XDP（RX）与 veth-fw-s 的 TC egress，与挂载点不交叉；
# 要对照 fwctl stats 的 drop_acl，需在 ns-client 内对 veth-c 打流
# （使流量经 veth-fw-c 的 RX 路径进入防火墙，并相应调整 dst/dst_mac）。

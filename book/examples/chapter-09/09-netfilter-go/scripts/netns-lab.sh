#!/usr/bin/env bash
# 第 9 章示例 09-netfilter-go 的隔离网络实验环境。
#
# 网络路径示例必须在隔离拓扑中验证：一条错误的 drop 规则可能断开
# 当前 SSH 连接。本脚本用 network namespace + veth pair 构造
# “宿主机 <-> nfguard 命名空间”的最小拓扑，实验全部在 netns 内进行。
#
# 拓扑：
#   宿主机 veth0 (10.99.0.1) <----> nfguard 内 veth1 (10.99.0.2)
#
# 用法：
#   sudo bash scripts/netns-lab.sh up     # 创建拓扑
#   sudo ip netns exec nfguard bash       # 进入命名空间
#   sudo bash scripts/netns-lab.sh down   # 清理

set -euo pipefail

NS="nfguard"
HOST_IF="veth0"
NS_IF="veth1"
HOST_IP="10.99.0.1"
NS_IP="10.99.0.2"

up() {
    # 清理可能残留的旧环境（删除 netns 会连带移除两端 veth）
    ip netns del "$NS" 2>/dev/null || true

    ip netns add "$NS"
    ip link add "$HOST_IF" type veth peer name "$NS_IF"
    ip link set "$NS_IF" netns "$NS"

    ip addr add "$HOST_IP/24" dev "$HOST_IF"
    ip link set "$HOST_IF" up

    ip -n "$NS" addr add "$NS_IP/24" dev "$NS_IF"
    ip -n "$NS" link set "$NS_IF" up
    ip -n "$NS" link set lo up
    ip -n "$NS" route add default via "$HOST_IP"

    echo "拓扑已就绪："
    echo "  进入命名空间： sudo ip netns exec $NS bash"
    echo "  连通性测试：   sudo ip netns exec $NS ping -c1 $HOST_IP"
    echo "  清理：         sudo bash scripts/netns-lab.sh down"
}

down() {
    ip netns del "$NS" 2>/dev/null || true
    # 兜底：netns 删除失败时手动移除宿主侧 veth
    ip link del "$HOST_IF" 2>/dev/null || true
    echo "拓扑已清理（bpffs 上的 map 需另行清理："
    echo "  sudo rm /sys/fs/bpf/nf_guard_rules /sys/fs/bpf/nf_guard_stats）"
}

case "${1:-}" in
    up)   up ;;
    down) down ;;
    *)    echo "用法：$0 up|down" >&2; exit 1 ;;
esac

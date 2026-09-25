#!/usr/bin/env bash
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 第 17 章实验拓扑：ns-client -- ns-fw -- ns-server（三命名空间，veth 组网）。
# 安全警告：本脚本只创建 veth 与 netns；XDP/TC 只允许挂到 veth-fw-c / veth-fw-s。
# 用法：sudo bash setup-veth.sh [setup|teardown]
set -euo pipefail
NS_CLIENT=ns-client; NS_FW=ns-fw; NS_SERVER=ns-server

setup() {
    [ "$(id -u)" -eq 0 ] || { echo "需要 root（sudo）运行"; exit 1; }
    # 幂等防护：残留命名空间会让下方 ip link set 中途失败并在宿主 ns 遗留游离 veth 对
    for ns in "$NS_CLIENT" "$NS_FW" "$NS_SERVER"; do
        if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
            echo "检测到残留命名空间 $ns，请先执行 teardown（sudo bash $0 teardown）"
            exit 1
        fi
    done
    ip netns add $NS_CLIENT 2>/dev/null || true
    ip netns add $NS_FW     2>/dev/null || true
    ip netns add $NS_SERVER 2>/dev/null || true
    # 左链路
    ip link add veth-c type veth peer name veth-fw-c
    ip link set veth-c netns $NS_CLIENT
    ip link set veth-fw-c netns $NS_FW
    # 右链路
    ip link add veth-s type veth peer name veth-fw-s
    ip link set veth-s netns $NS_SERVER
    ip link set veth-fw-s netns $NS_FW
    # 地址与转发
    ip -n $NS_CLIENT addr add 10.0.0.2/24 dev veth-c; ip -n $NS_CLIENT link set veth-c up
    ip -n $NS_CLIENT route replace default via 10.0.0.1
    ip -n $NS_FW addr add 10.0.0.1/24 dev veth-fw-c;  ip -n $NS_FW link set veth-fw-c up
    ip -n $NS_FW addr add 10.0.2.1/24 dev veth-fw-s;  ip -n $NS_FW link set veth-fw-s up
    # ip 无 sysctl 子对象，进 ns 执行 sysctl 是标准用法
    ip netns exec $NS_FW sysctl -w net.ipv4.ip_forward=1
    ip -n $NS_SERVER addr add 10.0.2.2/24 dev veth-s; ip -n $NS_SERVER link set veth-s up
    ip -n $NS_SERVER route replace default via 10.0.2.1
    echo "拓扑就绪。连通性：sudo ip netns exec $NS_CLIENT ping 10.0.2.2"
}

teardown() {
    ip netns del $NS_CLIENT 2>/dev/null || true
    ip netns del $NS_FW     2>/dev/null || true   # veth 对随 netns 一并销毁
    ip netns del $NS_SERVER 2>/dev/null || true
    echo "拓扑已销毁"
}

case "${1:-setup}" in
    setup) setup ;;
    teardown) teardown ;;
    *) echo "用法: $0 [setup|teardown]"; exit 1 ;;
esac

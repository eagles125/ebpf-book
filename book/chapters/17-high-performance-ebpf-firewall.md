# 第 17 章 实战：构建高性能 eBPF 防火墙

> 本章当前为“深度骨架”。后续完整章节将补齐可编译运行的 XDP/TC 数据面程序、用户态控制程序、规则下发、状态查询、NAT、校验和更新和压测记录。本骨架先固定生产级原型所需的架构和验收口径。

## 学习目标

完成本章后，读者应能够：

- 设计一个数据面与控制面分离的 eBPF 防火墙原型。
- 在 XDP 与 TC 之间做出明确选择，并理解二者性能、上下文和功能差异。
- 使用 eBPF map 实现 ACL、连接跟踪表和 NAT 状态表。
- 实现五元组匹配、状态检测、SNAT/DNAT 和校验和更新。
- 使用 Go 或 Python 编写用户态控制面，下发规则并查询状态。
- 使用 pktgen、iperf、bpftool 和网卡统计解释吞吐、新建连接速率和丢包结果。

## 本章导读

用户要求本章实现一个“麻雀虽小，五脏俱全”的 eBPF 基本防火墙，具备高吞吐、高新建连接速率、基本安全防护和 NAT 功能。这个目标不能只给出几段孤立代码。一个可信的防火墙原型至少需要清晰的数据路径、控制面接口、map 设计、连接状态机、规则更新策略、校验和处理、压测方法和失败边界。

本章首批不直接声称完整防火墙已经可用于生产。原因很简单：XDP/TC 程序需要在真实内核、真实网卡或 veth 实验拓扑、root 权限和压测环境中验证。后续完整章节会给出完整代码和压测数据；本骨架先把架构、代码资产和验证清单固定下来。

## 总体架构

```text
                    控制面：Go/Python CLI 或 daemon
┌────────────────────────────────────────────────────────────────┐
│  rule add/del/list                                             │
│  nat add/del/list                                              │
│  conntrack dump                                                │
│  stats export                                                  │
└───────────────┬───────────────────────────────┬────────────────┘
                │ bpffs pinned map              │ perf/ring/stats
                v                               ^
                    共享 map 层                 │
┌───────────────────────────────────────────────┴────────────────┐
│ acl_lpm_map / acl_hash_map                                      │
│ conntrack_hash_map                                              │
│ nat_policy_map                                                  │
│ stats_percpu_array                                              │
└───────────────┬────────────────────────────────────────────────┘
                │
                v
                    数据面：XDP 或 TC eBPF 程序
┌────────────────────────────────────────────────────────────────┐
│ parse Ethernet/IP/TCP/UDP                                      │
│ normalize five tuple                                           │
│ lookup conntrack                                               │
│ apply ACL                                                      │
│ apply SNAT/DNAT                                                │
│ update checksum                                                │
│ return PASS / DROP / REDIRECT / TX                             │
└────────────────────────────────────────────────────────────────┘
```

数据面必须只做高频路径必要工作：解析、匹配、状态更新和动作返回。控制面负责复杂逻辑：规则解析、冲突检查、超时扫描、状态展示、灰度更新和指标导出。

## XDP 与 TC 的选择分析

### XDP 优势

- 挂载在驱动收包早期，处理路径短，适合高吞吐丢包、防 DDoS、快速白名单/黑名单。
- 可返回 `XDP_DROP`、`XDP_PASS`、`XDP_TX`、`XDP_REDIRECT`。
- 配合 AF_XDP 可构建用户态零拷贝数据面。

### XDP 限制

- 上下文是 `struct xdp_md`，还没有进入完整内核协议栈。
- 做 NAT 时需要自己处理更多 L2/L3/L4 细节。
- 与 qdisc、skb metadata、部分协议栈能力集成不如 TC。

### TC 优势

- 挂载在 clsact ingress/egress，能够处理 `struct __sk_buff`。
- 更适合与 Linux 网络栈、容器 veth、隧道封装、egress NAT 和策略路由协作。
- helper 和上下文能力对改包更友好。

### TC 限制

- 路径比 XDP 更晚，极限吞吐通常不如原生 XDP。
- 已经分配 skb，高压丢包场景成本更高。

### 本章原型建议

完整章节建议采用“两阶段原型”：

1. **XDP ingress 防火墙**：实现高吞吐 ACL、基本 conntrack 和入口丢包。
2. **TC egress/ingress NAT 扩展**：实现更清晰的 SNAT/DNAT 和容器/veth 场景。

这样既能体现 XDP 的性能优势，也能现实地处理 NAT 在 egress 路径上的工程细节。

## 代码资产计划

```text
book/examples/chapter-17/firewall/
  bpf/
    fw_common.h
    fw_xdp.bpf.c
    fw_tc.bpf.c
  user/
    go.mod
    main.go
    rules.go
    maps.go
    stats.go
  scripts/
    setup-veth.sh
    attach-xdp.sh
    attach-tc.sh
    load-rules.sh
    bench-iperf.sh
    bench-pktgen.sh
  Makefile
  README.md
```

## 报文解析计划

数据面需要解析：

- Ethernet II。
- IPv4；后续可扩展 IPv6。
- TCP 与 UDP。
- 五元组：源地址、目的地址、源端口、目的端口、协议。

解析函数必须遵守 verifier 约束：每次访问包数据前检查 `data + offset <= data_end`。完整章节应解释为什么 verifier 不能接受“人看起来安全但缺少显式边界检查”的代码。

```c
struct flow_key {
    __u32 src_ip;
    __u32 dst_ip;
    __u16 src_port;
    __u16 dst_port;
    __u8 proto;
    __u8 pad[3];
};
```

## ACL 匹配设计

### 方案 A：hash 五元组匹配

- key：完整五元组或带通配编码的规则 key。
- value：动作、优先级、计数器索引。
- 优点：查找快，适合精确规则。
- 缺点：CIDR、端口范围和优先级处理不自然。

### 方案 B：LPM trie 地址匹配 + 端口/协议补充

- 使用 `BPF_MAP_TYPE_LPM_TRIE` 匹配源/目的 CIDR。
- 再结合端口范围、协议和方向。
- 优点：适合网络 ACL。
- 缺点：多维匹配需要拆分或多次查找。

### 本章原型建议

首版实现：

- 精确五元组 hash 规则，用于最小可运行版本。
- LPM trie CIDR 黑白名单，用于展示网络 ACL 的工程扩展。
- 默认策略：deny 或 allow 由控制面配置。

## 连接跟踪设计

### conntrack key

```c
struct ct_key {
    struct flow_key fwd;
};
```

需要定义反向 key：

```text
forward:  client_ip:client_port -> server_ip:server_port proto
reverse:  server_ip:server_port -> client_ip:client_port proto
```

### conntrack value

```c
enum ct_state {
    CT_NEW = 1,
    CT_ESTABLISHED = 2,
    CT_FIN_WAIT = 3,
    CT_CLOSED = 4,
};

struct ct_value {
    __u64 last_seen_ns;
    __u64 packets;
    __u64 bytes;
    __u8 state;
    __u8 flags;
    __u16 nat_port;
    __u32 nat_addr;
};
```

### 状态机计划

```text
TCP SYN
  -> CT_NEW
SYN/ACK or ACK matching reverse direction
  -> CT_ESTABLISHED
FIN/RST
  -> CT_FIN_WAIT or CT_CLOSED
timeout
  -> control plane cleanup
UDP first packet
  -> CT_ESTABLISHED-like short timeout entry
```

### 高新建连接速率优化

- 使用 LRU hash map 限制表大小，避免无限增长。
- 对高频计数使用 per-CPU stats，减少原子竞争。
- 数据面只更新时间戳和必要状态，复杂回收由控制面批量执行。
- 对 TCP 状态机做最小可用实现，不在 XDP 中复制完整 netfilter conntrack。

## NAT 设计

### SNAT

SNAT 修改源地址和源端口，常见于 egress：

```text
10.0.0.2:41234 -> 1.1.1.1:443
        |
        v
203.0.113.10:50001 -> 1.1.1.1:443
```

需要记录 NAT 映射：

```text
original tuple -> translated tuple
reverse tuple  -> original tuple
```

### DNAT

DNAT 修改目的地址和目的端口，常见于 ingress 负载均衡或端口转发：

```text
client -> 203.0.113.20:80
        |
        v
client -> 10.0.0.10:8080
```

### 校验和更新

改写 IPv4 地址、TCP/UDP 端口后必须更新：

- IPv4 header checksum。
- TCP checksum 或 UDP checksum。

完整章节需要比较：

- XDP 中手动增量校验和更新。
- TC 中使用 `bpf_l3_csum_replace`、`bpf_l4_csum_replace` 等 helper 的可能路径。

## 用户态控制面

控制面首选 Go，原因是 Go 的 `cilium/ebpf` 生态成熟，便于管理 map、attach、pin 和 CLI。Python 可作为轻量替代，但生产原型建议 Go。

### CLI 计划

```bash
sudo ./fwctl attach --iface eth0 --mode xdp
sudo ./fwctl rule add --src 10.0.0.0/24 --dst 0.0.0.0/0 --proto tcp --dport 443 --action allow
sudo ./fwctl nat snat add --src 10.0.0.0/24 --to 203.0.113.10 --port-range 50000-60999
sudo ./fwctl conntrack list
sudo ./fwctl stats
sudo ./fwctl detach --iface eth0
```

### 控制面职责

- 加载和挂载 eBPF 程序。
- pin maps 到 bpffs。
- 解析 ACL/NAT 配置。
- 原子更新规则。
- 周期性清理 conntrack 超时项。
- 导出 Prometheus 指标或打印统计。

## 性能基准测试计划

### veth 实验拓扑

```text
ns-client                         ns-fw                         ns-server
  veth-c  <-------------------->  veth-fw-c
                                    veth-fw-s <---------------> veth-s

测试路径：
client -> fw ingress XDP/TC -> fw egress TC NAT -> server
```

### iperf 吞吐测试

```bash
ip netns exec ns-server iperf3 -s
ip netns exec ns-client iperf3 -c 10.0.2.2 -P 8 -t 30
```

### pktgen 丢包/新建压力

```bash
# 后续完整章节提供 pktgen 脚本。
# 指标：pps、drop rate、CPU 使用、map 查找失败、conntrack insert 失败。
```

### 结果解读

完整章节应至少解释：

- XDP drop pps 与 TC drop pps 的差距。
- 规则数量增加对查找延迟的影响。
- conntrack map 容量不足时的失败模式。
- NAT 校验和错误如何通过 tcpdump、ethtool 统计和服务端日志定位。

## 可视化：数据路径决策树

```text
收到报文
  |
  v
解析 L2/L3/L4 成功？
  |-- 否 -> PASS 或 DROP（按策略）
  |
  v
命中已有 conntrack？
  |-- 是 -> 校验方向和状态 -> 必要时执行 NAT -> PASS
  |
  v
命中 ACL deny？
  |-- 是 -> DROP + stats
  |
  v
命中 ACL allow 或默认 allow？
  |-- 否 -> DROP + stats
  |
  v
新建 conntrack
  |
  v
需要 SNAT/DNAT？
  |-- 是 -> 改写地址/端口 + 更新校验和 + 写 NAT 状态
  |
  v
PASS / REDIRECT / TX
```

## 安全与生产边界

- map 容量必须有上限，并有超时回收策略。
- 控制面必须校验规则冲突和端口池耗尽。
- eBPF 程序应导出 drop reason 计数，避免“只知道丢包不知道原因”。
- 更新规则时避免清空 map 导致瞬时全放行或全阻断。
- 不要在 XDP 高频路径输出 perf event 作为默认行为。
- 生产部署必须配合内核参数、能力控制和审计策略，第 24 章会展开。

## 完整章节验收清单

- [ ] XDP ACL 最小版本可编译、加载、丢包和放行。
- [ ] TC NAT 扩展可编译、加载，并能完成 SNAT 或 DNAT。
- [ ] conntrack map 能记录正反向连接。
- [ ] 控制面能添加、删除、列出 ACL 和 NAT 规则。
- [ ] 校验和更新经过 tcpdump 或端到端连接验证。
- [ ] veth 实验拓扑脚本可重复创建和销毁。
- [ ] iperf 或 pktgen 压测方法可复现。
- [ ] 文中明确说明原型与生产防火墙的差距。

## 总结

一个可信的 eBPF 防火墙不是单个 XDP 函数，而是数据面、控制面、状态表、规则模型、NAT 映射、校验和更新、指标和压测共同组成的系统。本章后续完整写作会先实现 XDP 高吞吐 ACL，再通过 TC 补齐 NAT 和更贴近容器网络的处理路径，最后用 veth、iperf 和 pktgen 给出可解释的性能结果。

## 本章参考来源

- Linux kernel documentation: BPF program types, XDP, TC, helpers, maps。
- Cilium documentation: eBPF networking, kube-proxy replacement, network policy, Hubble observability。
- libbpf and libbpf-bootstrap examples。
- cilium/ebpf Go library documentation。
- LWN.net BPF and XDP articles。
- bpf mailing list discussions for verifier, helpers, XDP/TC behavior and networking changes。

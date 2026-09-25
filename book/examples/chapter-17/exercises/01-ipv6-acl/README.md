# 练习 01：IPv6 ACL 支持

第 17 章《实战：构建高性能 eBPF 防火墙》课后扩展任务 1（数据面）：以阶段 2（`../../02-parse-acl/`）为基线，把报文解析与两级 ACL 扩展到 IPv6。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在阶段 2 代码（Ethernet/IPv4/TCP/UDP 解析 + `acl_rules`/`acl_lpm` 两级 ACL）的基础上完成 IPv6 扩展：

1. 解析分支支持以太网类型 `0x86DD`：IPv6 基本头固定 40 字节（没有 `ihl` 类比），`nexthdr` 只处理 TCP（6）与 UDP（17）；遇到扩展头（非 6/17）**直接放行并计入 pass**，不要误丢。
2. 为 IPv6 新增一套与 v4 同构的 ACL 结构与映射：128 位五元组 key、规则 value、LPM key（前缀长度 0–128），以及 hash/LPM 两张新映射；**IPv4 报文查 v4 映射、IPv6 报文查 v6 映射，互不混用**。
3. loader 新增 `--rule6 <action>,<proto>,<src>,<sport>,<dst>,<dport>` 与 `--cidr6 <action>,<cidr>`；v4 参数与阶段 2 完全兼容。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] v4 行为与阶段 2 完全一致：`--rule`/`--cidr` 的下发与判定不变，IPv4 报文只查 v4 映射。
- [ ] `--cidr6 drop,2001:db8:0::/64` 下发后，来自 `2001:db8:0::2` 的 IPv6/TCP 流量被丢弃：`total` 与 `drop_acl` 同步增长，iperf3 客户端无法建立连接。
- [ ] 带 IPv6 扩展头的报文（`nexthdr` 非 6/17）不被丢弃：计入 `pass` 并 `XDP_PASS`。
- [ ] 非法输入在参数阶段被拒绝：`--cidr6` 前缀长度超 0–128、IPv6 地址非法、字段数不足等均报错退出。

## 提示

只给「会用到什么」，不给写法：

- 以太网类型 `0x86DD` 与 `ETH_P_IP` 一样是 uapi 宏，`vmlinux.h` 里没有——想想阶段 2 是怎么补 `#define` 的。
- `vmlinux.h` 提供了 `struct ipv6hdr`：留意 `nexthdr` 字段；基本头是**定长**的，想清楚 L4 头从哪里开始、还需要几次边界检查。
- TCP/UDP 头结构与 v4 分支用的是同一对（`struct tcphdr`/`struct udphdr`）。
- 对照 `flow_key`/`lpm_key` 想一想：128 位地址怎么存才能保持「原样存、原样查」的字节序契约？结构体 `pad` 为什么仍要清零？
- 两张新映射的选型与阶段 2 相同（hash + LPM trie，`BPF_F_NO_PREALLOC`）；LPM 查找时 `prefixlen` 填多少？
- 用户态地址解析用 `inet_pton(AF_INET6, ...)`——它一次写多少字节、写到结构的哪个字段上？
- 主程序需要某种方式区分「这个报文是 v4 还是 v6」再决定查哪套映射。

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`；实验拓扑由 `../../scripts/setup-veth.sh` 创建（veth 上原本只有 IPv4 地址，IPv6 演示需自行补配，见下），XDP 挂载点为拓扑中的 `veth-fw-c`。
- 安全警告：ACL 规则会直接丢包，**只挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**，否则可能立即失联。

```bash
cd book/examples/chapter-17/exercises/01-ipv6-acl
make
```

运行演示（需要 root；先建立拓扑，再给相关 veth 补 IPv6 地址）：

```bash
sudo ../../scripts/setup-veth.sh
# 给拓扑补 IPv6 地址（练习用文档前缀 2001:db8::/32，教学示例不入生产）
sudo ip -n ns-client addr add dev veth-c 2001:db8:0::2/64
sudo ip -n ns-fw     addr add dev veth-fw-c 2001:db8:0::1/64

# 终端 A：ns-fw 里起 iperf3 服务端（监听本机 veth-fw-c 的 v6 地址）
sudo ip netns exec ns-fw iperf3 -s

# 终端 B：下发 v6 网段黑名单并挂载（--rule/--cidr 等 v4 参数可同时叠加，与阶段 2 兼容）
sudo ./acl -i veth-fw-c --cidr6 drop,2001:db8:0::/64

# 终端 C：客户端跑 IPv6 TCP 压测——建连应当失败
sudo ip netns exec ns-client iperf3 -6 -c 2001:db8:0::1 -p 5201 -t 30
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

下发 `--cidr6 drop,2001:db8:0::/64` 后，客户端 `iperf3 -6 -c 2001:db8:0::1 -p 5201`：

```text
已下发规则：精确五元组 v4 0 条、v6 0 条；源地址网段 v4 0 条、v6 1 条。
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0
total=1496 pass=0 drop_acl=1496 drop_parse=0
total=3102 pass=0 drop_acl=3102 drop_parse=0
```

- 客户端（2001:db8:0::2 ∈ 2001:db8:0::/64）发出的每个 IPv6/TCP 报文都命中 v6 网段规则被丢弃：
  `total` 与 `drop_acl` 同步递增；iperf3 客户端无法建立连接。
- ICMPv6（ping、邻居发现 NDP，`nexthdr`=58）不属于 TCP/UDP，走「解析失败 → 放行」路径计入
  `pass`——压测期间 NDP 维持着邻居表项，这正是「看不懂的报文保守放行」的用意所在。样例窗口内
  无新出现的 NDP/ICMPv6 报文（邻居解析在建流初期已完成，与阶段 1 对 ARP 的口径一致），
  故三行 `pass` 均保持 0；一旦出现此类报文即计入 `pass`。
- 对照实验：换用 `--cidr drop,10.0.0.0/24`（v4 规则）时 v6 流量不受影响——证明 v4/v6 各查各的映射；
  反向同理。v4 侧 `--rule`/`--cidr` 行为与阶段 2 逐项一致。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`fw_common.h`/`acl.bpf.c`/`acl.c`），建议自行实现后再对照：

1. **结构扩展（`fw_common.h`）**：新增 `struct flow_key6`——128 位地址按 4 个 `__be32` 原样存储（网络字节序，「原样存原样查」的契约对 128 位同样成立）；`struct rule_value6` 与 v4 的 value 同构；`struct lpm_key6` 前缀长度放宽到 0–128。端口/协议/pad 的处理与 v4 完全一致，`pad` 清零保证哈希与字节级比较行为确定。既有 v4 结构一律不动。
2. **解析分流（`acl.bpf.c` 的 `parse_flow`）**：以太网层之后按 `h_proto` 分成 v4/v6 两个分支（函数签名多出 `flow_key6` 与 `is_v6` 两个出参）。v6 分支的要点：
   - 基本头固定 40 字节，没有 `ihl` 类比：`(__u8 *)ip6 + sizeof(*ip6)` 之后的指针就是 L4，L4 边界检查在新指针上重做一次（与 v4 同款「每层一查」纪律）；
   - `nexthdr` 仅识别 6（TCP）/17（UDP）；其余值（跳点头、路由头、分片头、AH 等扩展头）一律返回 0，走主程序「解析失败 → `bump(STAT_PASS)` → `XDP_PASS`」的既有路径——完整扩展头链解析超出练习范围，保守放行，绝不误丢；
   - 128 位地址用 `__builtin_memcpy` 从 `ipv6hdr` 整块拷入，不依赖 `vmlinux.h` 中 `in6_addr` 的成员命名。
3. **两张新映射与判定分支**：`acl_rules6`（hash，key `flow_key6`）与 `acl_lpm6`（LPM trie，key `lpm_key6`，`prefixlen` 0–128，仍需 `BPF_F_NO_PREALLOC`）。`acl_verdict6` 与 v4 的 `acl_verdict` 逐行同构，区别只在查 v6 映射与 LPM 查找 `prefixlen` 固定 128。报文家族由以太网类型唯一确定，v4 报文永不查 v6 映射、反之亦然——「互不混用」不是约定，而是解析分支的结构保证。
4. **loader 扩展（`acl.c`）**：新增 `--rule6`/`--cidr6` 两类参数（v4 参数与阶段 2 逐字兼容），命令行缓存数组与下发循环各配一份 v6 版本。地址解析改用 `inet_pton(AF_INET6, ...)`——一次写满 16 字节，恰好落入 `src_ip[4]`/`dst_ip[4]`；端口仍 `htons` 后存。v6 前缀长度做显式 0–128 范围检查：v4 侧「截断式取值」的缺陷（`/256` 截成 `/0`）在 v6 侧改为超界直接拒绝，避免静默错配。
5. **统计口径不变**：仍用 `enum stat_idx` 的 4 个下标（C1 契约，练习不新增下标）；`STAT_TOTAL` 同时计 v4/v6 成功解析的报文，`STAT_DROP_ACL` 计两族命中拒绝。
6. **已知边界**：只识别「基本头之后直接跟 TCP/UDP」的 IPv6 报文；带扩展头的报文（含分片）按 PASS 放行——意味着分片重组、路由头等场景下的精细策略本练习不覆盖，作为进阶方向。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- RFC 8200：Internet Protocol, Version 6 (IPv6) Specification（40 字节基本头、`Next Header` 与扩展头链的构成）。
- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，LPM trie 的 key 格式、`prefixlen` 语义与查找规则）。
- `inet_pton(3)`：`AF_INET6` 地址解析（IPv6 字面量的合法形式）。
- libbpf 文档：`bpf_map_lookup_elem`/`bpf_map_update_elem` 与 skeleton 加载模式（与阶段 2 同源）。
- 本章阶段 2 示例 `book/examples/chapter-17/02-parse-acl/`：本练习的基线代码，v4 路径与其保持一致。

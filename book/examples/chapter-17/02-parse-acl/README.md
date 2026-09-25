# 02-parse-acl：报文解析与 ACL

第 17 章《实战：构建高性能 eBPF 防火墙》渐进示例的阶段 2：从「无脑丢」进阶到「看懂再判」——逐层边界检查解析出五元组，再用 hash 精确规则 + LPM 网段规则两级 ACL 决定放行还是丢弃。

## 演示目标

- 掌握 XDP 报文解析的标准写法：Ethernet → IPv4 → TCP/UDP 逐层推进，**每访问一层头之前都做一次边界检查**，并正确处理 IPv4 可变头长（`ihl`）。
- 理解五元组（源/目的 IP、源/目的端口、协议）的归一化：key 全部按网络字节序「原样存、原样查」，内核侧与控制面侧都无需字节序转换。
- 认识两类 ACL 映射的分工与查找优先级：`acl_rules`（hash，精确五元组，最具体）→ `acl_lpm`（LPM trie，源地址网段最长前缀）→ 默认放行。
- 学会用命令行向映射下发规则：loader 解析 `--rule`/`--cidr` 参数，在 `open_and_load` 之后用 `bpf_map_update_elem` 写入映射。

## 文件说明

| 文件 | 说明 |
|---|---|
| `fw_common.h` | 阶段 2 共享头：五元组/规则结构、统计下标（字段语义、字节序、pad 一经定义不再改动，阶段 3/4 在此演化，Go 控制面按同一契约镜像） |
| `acl.bpf.c` | 内核态数据面：逐层边界检查解析出五元组 → 两级 ACL 判定 → `XDP_PASS`/`XDP_DROP`，per-CPU 数组计数 |
| `acl.c` | 用户态 loader：解析 `-i` 与全部 `--rule`/`--cidr` → `open_and_load` → 下发规则到映射 → attach XDP → 每 2 秒打印 4 项统计 |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`；实验拓扑由 `../scripts/setup-veth.sh` 创建，XDP 挂载点为拓扑中的 `veth-fw-c`；吞吐实验另需 `iperf3`（编译不依赖）。
- 安全警告：ACL 规则会直接丢包，**只挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**，否则可能立即失联。

## 编译与运行

```bash
cd book/examples/chapter-17/02-parse-acl
make

# 需要先建立实验拓扑（见 ../scripts/setup-veth.sh）：
sudo ../scripts/setup-veth.sh

# 网段黑名单：丢弃来自 10.0.0.0/24 的全部流量（--rule/--cidr 均可重复叠加）
sudo ./acl -i veth-fw-c --cidr drop,10.0.0.0/24

# 精确五元组 + 网段规则组合下发：
sudo ./acl -i veth-fw-c --rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201 --cidr drop,10.0.0.0/24
```

动手顺序（三个终端）：

```bash
# 终端 A：服务端命名空间起 iperf3 服务端
sudo ip netns exec ns-server iperf3 -s -p 5201

# 终端 B：客户端先验证基线连通（未下发规则时应能看到正常吞吐）
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -p 5201 -t 30

# 终端 C：挂载程序并下发固定端口五元组的丢弃规则，观察吞吐归零
sudo ./acl -i veth-fw-c --rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201
```

> 注意：`--rule` 是六个字段**全等**的精确匹配，而 iperf3 客户端的源端口默认随机（多数版本不支持固定）。
> 若规则始终不命中，先在防火墙命名空间用 `ss -tn` 查看真实连接的五元组，再按实下发；或直接改用
> 不依赖端口的 `--cidr` 演示丢包效果。

## 预期输出

（预期输出示例 / 运行级待验证）

下发 `--cidr drop,10.0.0.0/24` 后，在客户端跑 `iperf3 -c 10.0.2.2 -p 5201`：

```text
已下发规则：精确五元组 0 条，源地址网段 1 条。
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0
total=512 pass=0 drop_acl=512 drop_parse=0
total=1337 pass=0 drop_acl=1337 drop_parse=0
```

- 客户端（10.0.0.2 ∈ 10.0.0.0/24）发出的每个 IPv4/TCP 报文都命中网段规则被丢弃：
  `total` 与 `drop_acl` 同步递增；`pass` 与 `drop_parse` 保持 0——`pass` 只计实际放行的报文
  （解析失败的残包、ARP 等非 IPv4 报文也实际放行，计入 `pass`），与 `drop_acl` 互斥；
  `drop_parse` 阶段 2 保留未用。iperf3 客户端无法建立连接、吞吐为 0。
- 示例场景中 ARP 已在拓扑建立或基线测试时完成，压测期间没有新的非 IPv4 报文，故 `pass` 保持 0。
- 若不下发任何规则（只写 `-i`），则 `total` 与 `pass` 同步递增、`drop_acl` 恒为 0——即查找优先级
  第 3 步的默认放行，正好对照「规则命中」与「默认策略」。

## 代码讲解要点

- 查找优先级（与 `acl.bpf.c` 头注释同口径）：1) `acl_rules` 精确五元组（最具体，命中即返回其 action）；
  2) `acl_lpm` 源地址 CIDR 最长前缀匹配（黑/白名单由 value 的 action 区分，网段越具体优先级越高）；
  3) 默认 `FW_ALLOW`（教学基线；生产默认策略应可配置）。
- 字节序「存什么查什么」：报文里的 `saddr`/`daddr`/`source`/`dest` 本来就是网络字节序，五元组 key
  原样保存；loader 侧 `inet_pton`/`htons` 产出的同样是网络字节序——两侧不做任何转换即可做哈希全等
  匹配。若任何一侧转成主机序，哈希 key 就对不上了；这也是 `flow_key` 注释强调「原样存原样查」的原因。
- verifier 逐层边界检查：`data`/`data_end` 描述报文起止，verifier 把 `(eth+1) > data_end` 这类比较当
  「指针定界」证据，**每访问一层头都要重做一次，少写一次就加载失败**。`ihl` 处理是 IPv4 特有的：
  头长可变（`ihl` 以 4 字节为单位，最小 5=20B），必须先取 `ihl*4` 并验证不小于基本头长，再以
  `l4 = ip + ihl_len` 跳过可选字段，L4 的边界检查在新指针上重新做。
- 两类映射的选型：hash 是 key 全等匹配、O(1)，适合精确封禁，不适合网段/端口范围；LPM trie 按最长
  前缀匹配网段，适合源地址黑/白名单；端口范围、多字段通配两者都不适合（课后练习方向）。
- `BPF_F_NO_PREALLOC`：创建 LPM trie 必须带该标志——trie 的节点改为插入时按需分配，不为
  `max_entries` 预分配内存，网段规则这类条目数可观的映射借此节省内存、允许更大的容量上限。
- 统计口径：per-CPU 数组每 CPU 独立副本、数据面更新无锁；`STAT_TOTAL` 只计成功解析出五元组的报文，
  `STAT_PASS` 只计实际放行（与阶段 1 的 pass 语义一致），`STAT_DROP_ACL` 计 ACL 命中拒绝。
- loader 侧为什么重新定义结构体：skeleton 只导出 map/program 句柄、不导出 BPF 源码里的类型与枚举，
  而 `fw_common.h` 依赖 `vmlinux.h`（内核 BTF）不能被用户态包含——因此 `acl.c` 按契约逐字段镜像
  （阶段 5 的 Go 控制面同理镜像，改任一侧必须同步）。
- `parse_rule_arg`/`parse_cidr_arg` 的失败模式：`sscanf` 用带宽度的扫描集（`%7[^,]`/`%63[^,]`）防
  缓冲区越界；字段数不足、端口 >65535、IP 非法、CIDR 缺 `/`、前缀长度 >32 都直接拒绝；教学从简，
  proto 非 `tcp` 一律按 UDP、action 非 `drop` 一律按 allow 处理。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。先用 `../00-env/check-env.sh` 确认权限档位 |
| `attach_xdp 失败`，或挂载后远程会话失联 | 误挂到物理网卡/SSH 接口：XDP 在驱动层直接丢包。务必只挂实验拓扑的 veth 接口 |
| 编译报 `vmlinux.h file not found` | 未先 `make`（首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 生成），或内核缺少 BTF（参见附录 C） |
| 编译报 `use of undeclared identifier 'ETH_P_IP'` | `vmlinux.h` 只含类型不含宏；示例源码内已 `#define ETH_P_IP 0x0800`，删掉该行即复现 |
| 下发 `--rule` 后 `drop_acl` 不增长 | 精确匹配要求六个字段全部一致，客户端源端口随机时不命中：用 `ss -tn` 查实际五元组按实下发，或改用 `--cidr` |
| `--cidr drop,10.0.0.0/256` 丢弃了所有流量 | 前缀长度超出 0–32 时被截断（256 截成 0，`/0` 匹配所有源地址）：输入合法前缀长度 |
| 报 `规则下发失败（规则数超上限或格式错误）` | 映射容量已用尽（`acl_rules` 上限 1024、`acl_lpm` 上限 4096）；阶段 2 从简未提供删除接口，重启 loader 即恢复空表 |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，hash 与 LPM trie 的语义、
  key/value 约束与容量限制）。
- libbpf 文档：`bpf_map_lookup_elem` 对 `BPF_MAP_TYPE_LPM_TRIE` 的查找语义（lookup key 的
  `prefixlen` 决定查询前缀）、`bpf_map_update_elem` 的用户态用法。
- `include/uapi/linux/bpf.h`：`BPF_MAP_TYPE_LPM_TRIE`、`BPF_F_NO_PREALLOC`、`enum xdp_action`。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式（阶段 1 同源）。
- iperf3 用户文档：吞吐压测方法学（与第 9 章、第 17 章正文的压测口径一致）。

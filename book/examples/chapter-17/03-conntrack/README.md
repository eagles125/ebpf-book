# 03-conntrack：连接跟踪与 LRU

第 17 章《实战：构建高性能 eBPF 防火墙》渐进示例的阶段 3：从「逐包无状态判断」进阶到「有状态观测」——数据面用正反两个方向的 key 在 LRU 哈希表里登记每条连接，以最小 TCP 状态机跟踪 NEW/ESTABLISHED/FIN_WAIT/CLOSED，loader 每 2 秒打印统计并 dump 连接表。

## 演示目标

- 理解连接跟踪的基本模型：把「孤立的报文」组织成「连接」——登记（首包）→ 命中（后续同向/反向报文）→ 状态推进（FIN/RST）→ 回收（控制面超时清理）。
- 掌握正反 key 互换规则：一条连接的两个方向各构成一个五元组 key，`fill_ct_key()` 按 `reverse` 交换源/目的构造；先查正向（报文自身方向），未命中再查反向（回包方向），命中即同一连接。
- 认识 LRU 哈希映射的容量语义：`BPF_MAP_TYPE_LRU_HASH` 容量固定（`CT_MAX_ENTRIES=65536`），满时自动淘汰最久未用项——表不会无限增长，但**淘汰是静默的**；新连接登记失败计入 `ct_full`。
- 学会用 `bpf_map_get_next_key` + `bpf_map_lookup_elem` 弱遍历 dump 映射，并用与 `bpf_ktime_get_ns` 同源的单调时钟（`CLOCK_MONOTONIC`）计算表项 idle。

## 文件说明

| 文件 | 说明 |
|---|---|
| `fw_common.h` | 阶段 3 共享头（阶段 2 全文 + 连接跟踪追加）：`ct_state` 状态机、`ct_key`/`ct_value`、`CT_MAX_ENTRIES`，统计下标扩为 7 项（字段契约一经定义不再改动，Go 控制面按同一契约镜像） |
| `conntrack.bpf.c` | 内核态数据面：三参 `parse_flow`（新增 TCP 标志位出参）→ 正反 key 查表 → 状态机推进/新连接登记（LRU），恒 `XDP_PASS` 只观测，per-CPU 计数 |
| `conntrack.c` | 用户态 loader：解析 `-i` → `open_and_load` → attach XDP → 每 2 秒打印 7 项统计并 dump conntrack 表 |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`BPF_MAP_TYPE_LRU_HASH` 与 `bpf_ktime_get_ns` 在 5.10 上均已可用）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`；实验拓扑由 `../scripts/setup-veth.sh` 创建，XDP 挂载点为拓扑中的 `veth-fw-c`；建流工具用 `iperf3`（编译不依赖）。
- 安全警告：本阶段程序恒 `XDP_PASS`、只读观测，但**仍只应挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**（与阶段 1/2 口径一致）。

## 编译与运行

```bash
cd book/examples/chapter-17/03-conntrack
make

# 需要先建立实验拓扑（见 ../scripts/setup-veth.sh）：
sudo ../scripts/setup-veth.sh

# 挂载并观察（Ctrl-C 退出并自动卸载）：
sudo ./conntrack -i veth-fw-c
```

另开一个终端建一条 TCP 流（iperf3 默认控制与数据共用一条连接，dump 中就是一条表项）：

```bash
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -p 5201 -t 30
```

## 预期输出

（预期输出示例 / 运行级待验证）

iperf3 建流后 dump 出一条以首个报文方向登记的条目（`state=1`）；iperf3 客户端结束
（发送 FIN 关闭连接）后，同一条目状态推进、`idle` 持续增长：

```text
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0 ct_new=0 ct_established=0 ct_full=0

total=52 pass=52 drop_acl=0 drop_parse=0 ct_new=1 ct_established=0 ct_full=0
10.0.0.2       :41322 -> 10.0.2.2       :5201  proto=6 state=1 idle=0.1s pkts=52

total=1889 pass=1889 drop_acl=0 drop_parse=0 ct_new=1 ct_established=0 ct_full=0
10.0.0.2       :41322 -> 10.0.2.2       :5201  proto=6 state=3 idle=1.3s pkts=1889
```

- `proto=6` 是 TCP（`IPPROTO_TCP`）；`state` 即 `enum ct_state` 的取值：`1=CT_NEW`、
  `2=CT_ESTABLISHED`、`3=CT_FIN_WAIT`、`4=CT_CLOSED`。
- 建流时 SYN 首包以正向 key（client→server）登记为 `state=1`，此后每个客户端方向报文命中
  同一条目（`packets`/`bytes` 累加、`idle` 归零）；iperf3 结束后客户端发 FIN，状态推进为
  `3`。表项不会自行消失——超时回收由控制面完成，本阶段 loader 未实现清理，`idle` 会一直增长。
- **为什么示例里 `ct_established` 恒为 0（看不到 `state=2`）**：`CT_NEW → CT_ESTABLISHED`
  由「反向报文」触发，而单一挂载点只能看到报文的**接收**方向——本拓扑中服务端回包从
  `veth-fw-s` 进入 ns-fw、再从 `veth-fw-c` **发出**（发送方向），XDP 看不见它们。只有反向
  报文真的经过挂载点（例如让同一张 conntrack 表同时挂到 `veth-fw-c` 与 `veth-fw-s` 两个
  接口）才会看到 `state=2`——这正是「挂载点决定数据面可见性」的活教材，正反 key 查找逻辑
  本身已就绪，一旦反向报文可见即可触发。
- 只有一条流时 `pkts` 与 `total` 相等：每个成功解析的报文都命中该条目并累加 `packets`；
  `pass` ≥ `total` 的差值是非 IPv4/TCP/UDP 报文（解析失败按放行处理），`pass` 只计实际
  放行（与阶段 1/2 口径一致）。`drop_acl`/`drop_parse` 是阶段 2 判 ACL 的保留计数，本阶段
  只观测不判 ACL，恒为 0。
- `ct_full=0`：65536 的 LRU 容量在教学拓扑打不满。若把 `CT_MAX_ENTRIES` 临时调小（如 16）
  重建，多连接并发时即可看到新连接登记失败、`ct_full` 递增，同时旧表项被**静默淘汰**
  （dump 里直接消失）。

## 代码讲解要点

- 正反 key 互换规则：`fill_ct_key(&k, &flow, reverse)` 在 `reverse=1` 时交换源/目的 IP 与
  端口（协议不变）。一条连接的两个方向各构成一个 key，但**只登记其中一个**——先查正向
  （报文自身方向），未命中再查反向（回包方向），命中即同一连接且 `from_reply=1`。首个被
  看到的报文方向决定登记方向，dump 里的条目因此是「归一化到首包方向」的。
- 状态机（最小可用，不复刻 netfilter conntrack 全量语义）：`CT_NEW --看到反向报文-->
  CT_ESTABLISHED`；TCP 报文带 RST → `CT_CLOSED`、带 FIN → `CT_FIN_WAIT`（RST 优先于 FIN
  判断，两者直接覆盖当前状态）；UDP 无状态机，只有 `CT_NEW`。TCP 非 SYN 的「中途报文」
  查不到记录时跳过登记——避免把观测开始前就已建立的连接误登记成新连接。
- LRU 选型与静默淘汰：连接数随流量不可预知，普通 hash 表满后新表项直接插入失败、旧表项
  永不回收；`BPF_MAP_TYPE_LRU_HASH` 满时自动淘汰最久未用项，内存有上界。代价是「淘汰是
  静默的」——数据面无法感知淘汰发生，只能靠统计（`ct_full`）与控制面观测。
- `STAT_CT_FULL` 的触发路径：登记用 `bpf_map_update_elem(..., BPF_NOEXIST)`，返回非 0 即
  计满——LRU 满时的插入竞争、多 CPU 同时登记同一新连接（后到者失败）都会走到这里。策略上
  本阶段选择放行并计数（观测优先），是否 DROP 的取舍留给正文讨论。
- 写竞争近似：多 CPU 同时更新同一条目（`packets++`/`bytes +=`/状态写回）存在丢更新竞争
  （后写覆盖），教学实现接受该近似；生产可改 per-CPU 计量或 `bpf_spin_lock`。
- 超时回收归控制面：表项只带 `last_seen_ns`（`bpf_ktime_get_ns` 单调时钟，非墙钟），数据面
  不做超时删除；由控制面遍历超过阈值的表项并删除（阶段 5 的 Go 控制面实现）。时钟基准
  必须对齐：`bpf_ktime_get_ns` 基于 `CLOCK_MONOTONIC`，loader 用 `clock_gettime(CLOCK_MONOTONIC)`
  取同源时钟，两侧直接相减才是真实 idle，混用墙钟会得到巨大或负值。
- 三参 `parse_flow`：出参 `tcp_flags` 仅 TCP 有效（UDP 分支置 0，失败路径不写、调用方已
  预初始化）。TCP 标志位从位域成员（`tcp->fin`/`tcp->syn`/`tcp->rst`/`tcp->ack`）打包成
  4 bit——位域布局依赖编译器与字节序，x86_64/arm64 均为小端、与报文中标志位顺序一致，
  访问安全；大端平台需改用位运算自行解析。
- dump 是「无快照保证」的弱遍历：`bpf_map_get_next_key` 逐个取下一个 key，遍历期间表项可能
  正被 LRU 淘汰（`lookup` 失败就跳过），读到啥打印啥。
- 打印陷阱：`inet_ntoa` 返回线程内静态缓冲区，同一条 `printf` 里调用两次会互相覆盖
  （两个 IP 打成同一个），dump 用可重入的 `inet_ntop` 各自写入独立缓冲区。
- loader 侧镜像结构体：skeleton 只导出 map/program 句柄、不导出 BPF 源码里的类型与枚举，
  而 `fw_common.h` 依赖 `vmlinux.h`（内核 BTF）不能被用户态包含——`conntrack.c` 按契约
  逐字段镜像 `ct_key`/`ct_value`/统计枚举（与阶段 2 同理，改任一侧必须同步另一侧）。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。先用 `../00-env/check-env.sh` 确认权限档位 |
| `attach_xdp 失败`，或挂载后远程会话失联 | 误挂到物理网卡/SSH 接口。务必只挂实验拓扑的 veth 接口（本阶段虽只观测也保持该纪律） |
| 编译报 `vmlinux.h file not found` | 未先 `make`（首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 生成），或内核缺少 BTF（参见附录 C） |
| 编译报 `use of undeclared identifier 'ETH_P_IP'` | `vmlinux.h` 只含类型不含宏；示例源码内已 `#define ETH_P_IP 0x0800`，删掉该行即复现 |
| dump 一直为空（有流量但无表项） | TCP 只登记以 SYN 开始的连接：程序挂载晚于连接建立时，中途报文（非 SYN 且无记录）按设计跳过登记。重跑 `iperf3` 重新建流即可 |
| `state` 一直停在 1、`ct_established` 恒为 0 | 反向报文未经过挂载点：XDP 只作用于接收方向，本拓扑中服务端回包在 `veth-fw-c` 上是发送方向，看不见即不触发 `CT_ESTABLISHED`（见「预期输出」第 3 条） |
| `ct_new` 停止增长、`ct_full` 递增 | LRU 表满（65536）后新连接登记失败，同时旧表项被静默淘汰；调大容量或由控制面清理前，新连接不再登记。教学可调小 `CT_MAX_ENTRIES` 复现 |
| `idle` 数值巨大或为负 | 时间基准混用：`last_seen_ns` 来自 `bpf_ktime_get_ns`（单调时钟），用户态必须用 `CLOCK_MONOTONIC` 对齐，不能用墙钟（`time()`/`CLOCK_REALTIME`） |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，`BPF_MAP_TYPE_LRU_HASH`
  的容量与 LRU 淘汰语义、per-CPU 数组映射）。
- `include/uapi/linux/bpf.h`：`BPF_MAP_TYPE_LRU_HASH`、`BPF_NOEXIST`、`enum xdp_action`。
- libbpf 文档：`bpf_map_get_next_key`/`bpf_map_lookup_elem` 的用户态遍历组合、
  `bpf_ktime_get_ns` 的单调时钟基准、per-CPU 映射的 `bpf_map_lookup_elem` 语义。
- `inet_ntop(3)`：`inet_ntoa` 的静态缓冲区陷阱与可重入地址转换。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式（阶段 1/2 同源）。
- iperf3 用户文档：建流方法学（与阶段 2 的压测口径一致）。

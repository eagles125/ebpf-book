# 第 17 章 实战：构建高性能 eBPF 防火墙

> 本章规划的独立示例已全部落地到 [`book/examples/chapter-17/`](../examples/chapter-17/)，
> 每个阶段独立成目录、独立可编译，含 BPF 源码、用户态 loader/控制面、Makefile 与 README
> （运行说明与验证状态）；编译级验证已于 2026-10-09 使用非 Snap Go 工具链
> （`/work/ai/ebpf-book/book/.tools/go-1.27.1/bin/go`）全量通过
> （`scripts/verify-build.sh`，9/9 PASS），
> 运行级验证需特权环境，状态标注见各 README 与 1.5 节。

本章把一个“麻雀虽小，五脏俱全”的 eBPF 防火墙拆成五个渐进阶段完整实现：从最小的 XDP 丢包闭环，到报文解析与 ACL、连接跟踪、TC 上的 SNAT/DNAT，最终汇合成一套带 Go 控制面的完整系统。全部示例源码与脚本位于 `book/examples/chapter-17/`，按阶段独立成目录、独立可编译；本章正文讲解的每一段代码都与该目录下的源文件一一对应，文中标注了每个代码块的出处文件。目录总览：

```text
book/examples/chapter-17/
  README.md            目录总览、统一构建模式、验证状态说明
  00-env/              check-env.sh 环境自检（无 root 也能给出三档结论）
  01-xdp-drop/         阶段 1：最小 XDP 丢弃
  02-parse-acl/        阶段 2：报文解析 + ACL
  03-conntrack/        阶段 3：连接跟踪
  04-nat-tc/           阶段 4：TC NAT
  05-firewall/         阶段 5：完整系统（bpf/ 数据面 + user/ Go 控制面）
  scripts/             setup-veth.sh 拓扑、bench-*.sh 压测、verify-build.sh 编译级验证
  exercises/           课后扩展任务 4 项参考实现（各含独立 README）
```

## 学习目标

完成本章后，读者应能够：

- **构建**：独立完成五个阶段数据面程序（XDP 丢弃、解析 + ACL、连接跟踪、TC NAT、完整系统）与 Go 控制面 `fwctl` 的编译，理解 libbpf-bootstrap 与 cilium/ebpf 两条构建链路。
- **挂载**：把 XDP 程序挂到指定接口并理解 native/generic 差异；用 libbpf 的 `bpf_tc_*` 或 `tc filter replace ... pinned` 挂载 TC 程序；管理 map/prog/link 三类 pin 对象的生命周期。
- **下发规则**：用命令行或 `fwctl` 向精确五元组、源地址网段、SNAT/DNAT 三类规则对应的映射下发条目，理解五元组的网络字节序契约。
- **读统计**：解释 per-CPU 统计映射 8 项计数的口径（含 `pass + ct_established + drop_acl = total` 恒等式的适用范围）、dump 连接表并判断连接状态。
- **解读压测**：用 iperf3 与 pktgen 对实验拓扑压测，按“XDP 未生效、连接表满、NAT 校验和错、规则不生效”四类失败模式定位问题。

## 本章导读

**教师怎么教**：本章按五个阶段推进，每个阶段固定按“教师视角引入 → 核心代码原理讲解 → 学生动手 → 检查点自测”四段展开。阶段依赖是线性的，每一步只引入一个新概念；阶段 5 的数据面不发明新逻辑——`fw_xdp` 合并阶段 2 的解析/ACL 与阶段 3 的连接跟踪，`fw_tc` 原样迁入阶段 4 的 NAT 改写，控制面是新写的部分：

```text
01-xdp-drop ──► 02-parse-acl ──► 03-conntrack ──► 04-nat-tc ──► 05-firewall
  丢包闭环        解析 + ACL        连接跟踪         TC NAT       合并 + Go 控制面
```

课时安排可按每阶段一次课（约 2 学时）推进，阶段 5 占两次课；压测与排错（第八节）可并入阶段 4/5 的实践课。每阶段示例目录内的 README 与正文小节一一对应，可作课前预习材料。

**学生怎么学**：动手前逐项核对前置条件——Linux 内核 5.10+ 且开启 `CONFIG_DEBUG_INFO_BTF`；root 或 `CAP_BPF` + `CAP_NET_ADMIN`（TC 挂载另需 `CAP_NET_ADMIN`）；`clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`，阶段 5 另需 Go 1.22+ 与 `tc`（iproute2）；实验拓扑由 `book/examples/chapter-17/scripts/setup-veth.sh` 创建；建流工具用 iperf3。无特权环境可运行 `00-env/check-env.sh` 得到三档结论，编译级验证不需要 root。安全红线只有一条：XDP/TC 程序会直接丢包、改包，**只挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**，否则可能立即失联。

## 一、环境准备

本章实验环境按五层组织，逐层就绪后再进入阶段 1：1.1 确认内核、权限与依赖工具；1.2 装好编译工具链；1.3 搭起实验拓扑；1.4 说明挂载、卸载与规则下发；1.5 汇总预期输出与验证状态约定。

**教师视角**：五层的顺序不是并列清单，而是依赖链——先确认内核与权限（1.1），编译链路才有着落（1.2）；拓扑与运行操作（1.3/1.4）依赖前两层；效果判读（1.5）依赖前三层。类比飞行员的航前检查单：逐项核对不是仪式，而是让缺项在地面暴露——环境层不装齐，后面全卡住：BTF 缺失则 `vmlinux.h` 无从生成、编译停在第一步，iperf3 缺装则要到压测才返工，回溯成本最高。两个误区先讲清：其一，凭感觉判断权限档位——capsh 的 bounding set 不等于生效能力，1.1 的偏差就是现成反例；其二，把「编译通过」当成「实验就绪」——运行级操作另有权限与工具前提。

**学生动手**：逐项核对（运行级需 root）：

- [ ] 运行 `bash book/examples/chapter-17/00-env/check-env.sh`，记录三段结论
- [ ] 按命令清单装齐工具链，逐一核对版本输出（clang 18 / bpftool 7 / libbpf 1.3 / Go 1.22+）
- [ ] `setup-veth.sh setup` 后从 ns-client ping 通 10.0.2.2，完成拓扑验收
- [ ] 通读 1.5 验证状态约定，能复述「预期输出示例」的含义

阶段检查点：

1. `check-env.sh` 编译级全部 OK，且能说出自己所在的权限档位。
2. 拓扑验收 ping 通过，并能解释销毁为何不留游离 veth。

自测两问：

1. **账号无 root 但 bounding set 含 `cap_bpf`，能完成全部实验吗？**（提示：bounding set 不等于 permitted/effective 生效集，以 1.1 的 capsh 实测为准。）
2. **编译第一步就报 `vmlinux.h` 生成失败，先查什么？**（提示：`/sys/kernel/btf/vmlinux` 是否存在，即 `CONFIG_DEBUG_INFO_BTF` 是否开启。）

### 1.1 依赖环境

**内核版本与 BTF。** 全部示例要求 Linux 5.10+ 且开启 `CONFIG_DEBUG_INFO_BTF`——XDP bpf_link、LRU hash、TC clsact、`bpf_skb_store_bytes` 等在 5.10 上均已可用；每个示例 `make` 的第一步是 `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`，内核 BTF 缺失则类型头文件无从生成，编译无法开始。

**权限分级。** 加载 BPF 程序需要特权，三档环境的能力边界：

| 环境档位 | 编译 | 加载/挂载 | veth 实验 |
|---|---|---|---|
| root | 可 | 可 | 可（仍只挂 veth） |
| 非 root + `CAP_BPF`/`CAP_NET_ADMIN` | 可 | 可 | 可（网络类操作还需相应 CAP） |
| 非 root 且无 `CAP_BPF` | 可 | 不可 | 不可 |

不同内核版本对 `CAP_BPF`/`CAP_PERFMON`/`CAP_SYS_ADMIN` 的组合要求略有差异，教学上以 root 或上表第二档为稳妥口径。非 root 加载失败不是程序错误：`kernel.unprivileged_bpf_disabled=2` 时内核明确拒绝非特权加载，`open_and_load` 直接返回失败——这正是本仓库编写环境的档位。

**veth 与网络命名空间前置。** 1.3 的拓扑由 iproute2 的 `ip netns`/`ip link` 创建，运行级实验另需 `tc`（挂 TC filter）与 iperf3（建流、压测）；三者都只影响运行，编译不依赖。

**依赖工具清单与检查。** 清单不必背——运行 `bash book/examples/chapter-17/00-env/check-env.sh`（无 root 也可），脚本按“内核与 BTF → 工具链（编译级）→ 权限分级（加载级/实验级）”三段输出并给出结论。本机输出节选（本地验证）——注意权限一行并不属实，原因见后：

```text
== 工具链（编译级） ==
OK    clang（编译 BPF C）
OK    bpftool（BTF dump / skeleton）
OK    libbpf 头文件（libbpf-dev）
== 权限分级（加载级/实验级） ==
OK    具备 CAP_BPF：可加载程序，实验还需 CAP_NET_ADMIN/CAP_SYS_ADMIN
```

那行 OK 为何不属实：脚本以 capsh 的 **bounding set** 粗检权限，本机 bounding set 恰含 `cap_bpf`；但 bounding set 不等于生效能力——`capsh --has-p=cap_bpf` 实测 not permitted（本机非 root、`kernel.unprivileged_bpf_disabled=2`），真实档位是上表第三行：可编译、不可加载。判断自身权限要以 permitted/effective 集为准，这个偏差本身就是现成的教学点。

### 1.2 编译环境

两条构建链路、两套工具：

- **C 链路（阶段 1–4 与练习）**：clang/LLVM 18.x 以 `-target bpf` 编译 BPF C；`libbpf-dev` 提供用户态头文件（`/usr/include/bpf/bpf_helpers.h`）与链接库；bpftool 7.x 负责 BTF dump 与 skeleton 生成；gcc、make 构建用户态 loader。
- **Go 链路（阶段 5 控制面）**：Go 1.22+；依赖 `cilium/ebpf v0.16.0` 固定于 `user/go.mod`——示例按该版本 API 书写（如 `MapIterator.Next` 一次取一对、接口解析用标准库 `net.InterfaceByName`），升级依赖前先核对这两处。

Ubuntu 安装与版本自检（本机实测输出，本地验证）：

```bash
sudo apt install clang llvm gcc bpftool libbpf-dev make golang iproute2
clang --version                 # Ubuntu clang version 18.1.3 (1ubuntu1)
bpftool version                 # v7.7.0
go version                      # go1.27.1 linux/amd64
dpkg -s libbpf-dev | grep ^Version   # Version: 1:1.3.0-2build2
```

Go 依赖校验和已随仓库提交（`go.sum`），联网环境首次构建通常无需干预；若提示缺依赖，执行 `cd user && go mod tidy`——该步需访问 Go 模块代理（网络依赖），离线环境按 1.5 的约定记“编译级待验证”。

### 1.3 部署环境

实验拓扑为三命名空间两跳链路：客户端与服务端互不可达，一切流量必经 ns-fw 转发，防火墙程序挂在 ns-fw 的两个接口上：

```text
ns-client ──── 左链路 ────► ns-fw ──── 右链路 ────► ns-server
veth-c                      veth-fw-c   veth-fw-s   veth-s
10.0.0.2/24                 10.0.0.1/24 10.0.2.1/24 10.0.2.2/24
默认路由 → 10.0.0.1         ip_forward=1            默认路由 → 10.0.2.1
```

| 命名空间 | 接口 | 地址与路由 |
|---|---|---|
| ns-client | veth-c | 10.0.0.2/24，默认路由 → 10.0.0.1 |
| ns-fw | veth-fw-c、veth-fw-s | 10.0.0.1/24、10.0.2.1/24，`ip_forward=1` |
| ns-server | veth-s | 10.0.2.2/24，默认路由 → 10.0.2.1 |

搭建与销毁（脚本自带残留防护：检测到同名 netns 残留即拒绝执行，避免中途失败遗留游离 veth）：

```bash
sudo bash book/examples/chapter-17/scripts/setup-veth.sh setup     # 搭建
sudo ip netns exec ns-client ping 10.0.2.2                         # 连通性验收
sudo bash book/examples/chapter-17/scripts/setup-veth.sh teardown  # 销毁
```

清理语义：销毁只需删除三个 netns——veth 对的一端随 netns 消失时，对端即使留在宿主命名空间也会被内核一并回收，不留游离接口。

**安全警告（执行任何挂载命令前必读）**：XDP/TC 程序会直接丢包、真实改写报文，TC egress 还影响该接口发出的全部流量。只允许挂到本拓扑创建的 veth 接口（`veth-fw-c`/`veth-fw-s`），**绝不要挂到物理网卡或 SSH 正在使用的接口**——否则可能立即失联，只能从带外控制台恢复。

### 1.4 运行环境

**XDP 挂载与卸载。** 两条等价路径：阶段 1–3 的 C loader 调 libbpf 的 `bpf_program__attach_xdp` 挂到指定接口，Ctrl-C 退出时随 link 的文件描述符关闭自动解除挂载；阶段 5 的 `fwctl attach` 用 cilium/ebpf 的 `link.AttachXDP` 挂载并把 link pin 成 `/sys/fs/bpf/fw17/xdp_link`——attach 进程退出后挂载持续生效，卸载由 `fwctl detach` 加载该 pinned link 并 close 完成。

**TC 挂载与卸载。** TC 程序不能靠 skeleton 自动挂载：阶段 4 的 C loader 走 libbpf netlink API——`bpf_tc_hook_create` 创建 clsact qdisc（已存在报 `-EEXIST`，视为成功）、`bpf_tc_attach` 绑定 filter，退出前必须显式 `bpf_tc_detach`（filter 持有程序引用，不随进程退出卸载）；阶段 5 的 Go 侧把 pin 好的程序路径交给 iproute2：`tc qdisc replace dev <接口> clsact` 后 `tc filter replace ... bpf direct-action pinned <prog>`（兼容 5.10+，TCX 需 6.6+ 不采用），卸载按方向 `tc filter del dev <接口> ingress/egress`。

**fwctl 七个子命令总览**（与阶段 5 README 一致）：

| 子命令 | 作用 |
|---|---|
| `attach` | 加载两个 .o、同名映射合并为同一实例并 pin 到 `/sys/fs/bpf/fw17`，挂载 XDP 与 TC |
| `detach` | 解除全部挂载并删除 pin 目录 |
| `rule` | 精确五元组规则 add/del/list（`acl_rules`） |
| `cidr` | 源网段规则 add（`acl_lpm`，最长前缀匹配；仅新增，删除留作练习） |
| `nat` | SNAT/DNAT 通配规则 add（`nat_map`） |
| `conntrack` | 连接表 dump（list） |
| `stats` | per-CPU 统计逐 CPU 求和输出 |

**规则与 NAT 下发流程。** attach 一次，之后改规则、查状态都独立执行，不需要重新挂载：

```text
fwctl attach   （加载 → pin map/prog → 挂载 XDP/TC）
  → fwctl rule/cidr/nat add   （逐 key 原子写入 pinned map，不清空重建）
  → fwctl stats / conntrack list   （观察生效）
  → fwctl detach   （删除 pin 目录，规则随 map 消失）
```

### 1.5 运行效果

各阶段关键观测点与预期输出（均为预期输出示例，口径见本节末尾的验证状态约定）：

| 阶段/观测点 | 预期输出示例 | 判读要点 |
|---|---|---|
| 阶段 1 `xdp_drop` | `pass=0 drop=12` | `drop` 递增即 XDP 生效，ping 全部超时 |
| 阶段 2 ACL 统计 | `total` 与 `drop_acl` 同步递增 | 精确规则需用 `ss -tn` 查真实源端口；网段规则用 `--cidr` |
| 阶段 3 连接表 dump | `10.0.0.2:41322 -> 10.0.2.2:5201 … proto=6 state=1 idle=0.1s pkts=52` | SYN 首包登记 `state=1`（NEW）；反向报文不可见时到不了 2（正向 FIN/RST 仍可迁出 1），本拓扑预期 |
| 阶段 4 NAT 抓包 | `IP 10.0.2.1.41322 > 10.0.2.2.8080: Flags [S]` | 目的/源已按 DNAT/SNAT 改写；`nat` 计数按 2 递进 |
| 阶段 5 `fwctl stats` | `total=18734 pass=18734 nat=37468` | 解析成功的报文满足 `pass + ct_established + drop_acl = total` |

**tcpdump 抓包对比。** 在 ns-server 抓包验证 NAT 改写：`sudo ip netns exec ns-server tcpdump -ni veth-s tcp port 8080`。方法：先由连接表 dump 或 ingress 侧抓包记录原始五元组，再对照改动后报文——目的地址应已改为 DNAT 规则指定的新目的、源地址应已改为 SNAT 指定的新源；若 tcpdump 提示校验和错误，说明增量校验和实现有误（地址项须带 `BPF_F_PSEUDO_HDR`，UDP 校验和 0 跳过改写）。

**stats 与 conntrack 查询。** attach 之后任意时刻、任意进程可查（无需重新 attach）：

```bash
sudo ./fwctl conntrack list   # 10.0.0.2:41322 -> 10.0.1.1:8080 proto=6 state=NEW
sudo ./fwctl stats            # 8 项：total/pass/drop_acl/drop_parse/ct_new/ct_established/ct_full/nat
```

**验证状态约定。** 本章正文与各示例 README 的验证状态统一按三档标注：

| 场景 | 统一标注 |
|---|---|
| 编译验证 | 已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3 / Go 1.27.1）`make` 通过 |
| 待运行验证 | 编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例 |
| Go 依赖网络不可用 | 依赖待拉取，编译级待验证（需网络 `go mod tidy`） |

## 二、总体架构（数据面/控制面分工）

本章完成的防火墙由两个数据面程序与一个 Go 控制面组成：XDP 程序 `fw_xdp` 负责“看懂报文、查连接表、判 ACL”，TC 程序 `fw_tc_ingress`/`fw_tc_egress` 负责在转发路径上做 NAT 改写，控制面 `fwctl` 负责加载、挂载、规则下发与状态读取。三者不直接互相调用，全部协作通过五张共享映射完成——映射名与定义在两个数据面对象中逐字一致，加载时被合并为同一实例并固定（pin）到 bpffs 目录 `/sys/fs/bpf/fw17`：

```text
                控制面：fwctl（Go + cilium/ebpf v0.16.0）
     attach / detach │ rule·cidr·nat add/del/list │ conntrack list │ stats
                     │ 读写 pin 目录 /sys/fs/bpf/fw17
                     v
 ┌────────────────────────────────────────────────────────────────────┐
 │                    pin 目录 /sys/fs/bpf/fw17                        │
 │   acl_rules（hash）  acl_lpm（LPM trie）  conntrack（LRU hash）      │
 │   nat_map（hash）    stats（per-CPU 数组）      —— 5 张共享映射       │
 │   fw_xdp.prog  fw_tc_ingress.prog  fw_tc_egress.prog  xdp_link      │
 └────────────┬───────────────────────────────────┬───────────────────┘
              │ XDP 侧使用 4 张（同名同定义）       │ TC 侧使用 nat_map + stats
              v                                   v
   数据面 1：fw_xdp（XDP，veth-fw-c）      数据面 2：fw_tc（TC clsact）
   解析 → conntrack 正反查找              fw_tc_ingress @ veth-fw-c ingress（DNAT）
   → 快路径先于 ACL → 丢/放行             fw_tc_egress  @ veth-fw-s egress （SNAT）
                                          do_nat 改写 + 增量校验和
```

五张映射的选型与出现阶段一览：

| 映射 | 类型 | 容量 | 用途 | 出现阶段 |
|---|---|---|---|---|
| `stats` | per-CPU 数组 | 8 项 | 数据面无锁计数，控制面逐 CPU 求和 | 2–5 |
| `acl_rules` | hash | 1024 | 精确五元组规则（key 全等匹配） | 2、5 |
| `acl_lpm` | LPM trie | 4096 | 源地址 CIDR 最长前缀匹配 | 2、5 |
| `conntrack` | LRU hash | 65536 | 连接表：正反 key、状态机、计量 | 3、5 |
| `nat_map` | hash | 4096 | SNAT/DNAT 通配规则 | 4、5 |

实际挂载位置与实验拓扑严格对应：

| 程序 | 程序类型 | 接口与方向 | 职责 |
|---|---|---|---|
| `fw_xdp` | XDP | `veth-fw-c`（接收方向） | 解析、连接跟踪、ACL 判定 |
| `fw_tc_ingress` | TC clsact ingress | `veth-fw-c` ingress | DNAT（赶在 ns-fw 路由查找前改目的地址） |
| `fw_tc_egress` | TC clsact egress | `veth-fw-s` egress | SNAT（路由决策后、出接口前改源地址） |

**为什么 XDP 做丢弃与 ACL、TC 做 NAT。** 这是本章在两种程序类型之间取舍的结论。XDP 挂载点位于驱动收包路径的最早处，native 模式下报文尚未生成 skb，处理路径最短，适合把“丢弃与放行”这类高频判定放在那里；代价是上下文 `xdp_md` 只有裸报文指针，改包所需的一切（非线性缓冲区、校验和维护、与路由的协作）都要自理。TC 挂载点工作在 `clsact` qdisc 上、报文已是 skb，`bpf_skb_store_bytes`/`bpf_l3_csum_replace` 等辅助函数由内核代管缓冲区状态，且 ingress/egress 两个方向正好覆盖“进防火墙第一跳改目的、出防火墙最后一跳改源”的 NAT 位置需求；代价是路径比 XDP 晚、极限吞吐通常更低。因此本章把“判”放在 XDP、把“改”放在 TC，两者通过共享映射协同。

数据面只做高频路径的必要工作：解析、查表、判定、改写、计数；一切低频复杂逻辑——规则解析与冲突检查、状态展示——都归控制面。这条分工原则贯穿全章：数据面代码里只有映射查找与算术，没有策略计算与格式化输出；控制面代码里没有逐包逻辑，只有映射读写与命令分发。

**教师视角**：把架构放在阶段之前讲，是为了让学生带着地图进阶段——阶段 1–4 的每个示例都是阶段 5 全景图上的一块预演，先见森林再看树木，进入阶段 5 时合并与收口就不显突兀。类比餐厅：数据面是前场服务员，逐桌快速响应；控制面是后场经理，改菜单、排班；菜单（共享映射）是双方唯一的约定——同名映射若不一致，等于两份菜单各改各的，「共享 map 不一致事故」就来了：两个 .o 被分开独立加载时各得一套映射实例，控制面 pin 的只是其中一套，表现为统计与规则对不上（排错表已有专条）。

**学生动手**：不动代码，只对图核对：

- [ ] 对照本节架构图，说出五张共享映射分别被哪些程序读写
- [ ] 指出三个程序各自的挂载接口与方向（对照挂载位置表）
- [ ] 用一句话复述「数据面只做高频路径的必要工作」的分工边界

阶段检查点：

1. 能不看正文画出「三程序 + 五映射 + pin 目录」关系草图。
2. 能预测并解释「两次独立加载」会出什么事故。

自测两问：

1. **为什么同名映射必须经 `fwctl attach` 的合并路径加载？**（提示：两次独立加载产生两套同名映射实例，pin 的只有一套，另一套的统计与规则永远看不到。）
2. **规则冲突检查为什么不放进数据面？**（提示：数据面只做 O(1) 查表与算术，一切低频复杂逻辑归控制面——分工原则。）

## 三、阶段 1：最小 XDP 丢弃

### 教师视角引入：防火墙先要会丢包

任何防火墙的第一项能力都是丢包，而且是“在协议栈之前”丢。类比门口保安：上岗第一天先要学会拦人，之后才谈得上查证件、登记访客。本章第一个闭环因此从“无条件丢弃所有 IPv4 报文”开始——程序只有约 40 行，却完整走一遍「加载 → 验证器（verifier）校验 → 即时编译（JIT）→ 挂载 → 报文触发」的链路，后面每个阶段都在这条链路上加东西。

两个常见误区先讲清：`XDP_PASS` 不等于“我处理过了”，它只表示“交给内核协议栈继续处理”——本示例在残包与非 IPv4 报文上返回 `XDP_PASS` 并计入 `pass`，语义是“实际放行”；XDP 的执行位置随挂载模式而异——native XDP 由网卡驱动在收包路径早期执行、报文尚未进入协议栈，generic XDP 是驱动不支持时的回退实现、工作在 skb 分配之后，两种模式对程序代码透明但时机与性能不同、随内核版本与驱动支持而异，教学上记住保守结论：XDP 尽可能早，早到协议栈看到它之前。

读者最容易卡住的地方有两个：非 root 环境下 `open_and_load` 直接失败（这是 `kernel.unprivileged_bpf_disabled=2` 的预期行为，不是程序错误）；编译报 `use of undeclared identifier 'ETH_P_IP'`——BTF 只导出类型、不导出宏，uapi 头文件里的 `#define` 需要 CO-RE 程序在源码内自行补定义。

### 核心代码原理讲解：一个最小的 XDP 程序

以下为 `book/examples/chapter-17/01-xdp-drop/xdp_drop.bpf.c` 全文（与示例目录逐字一致；文件头注释即 `XDP_DROP(1)` 驱动层直接丢弃不进协议栈、`XDP_PASS(2)` 交给协议栈、`XDP_TX(4)`/`XDP_REDIRECT(6)` 本章未用的语义表）：

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 1：最小 XDP 丢弃示例。
 *
 * 闭环：XDP 挂载点（网卡驱动收包最早处）-> 动作返回码 -> loader 轮询 map 观察效果。
 * 行为：丢弃所有 IPv4 报文（XDP_DROP），放行 ARP 等（XDP_PASS），per-CPU 数组计数
 *       （pass 只计实际放行的报文，pass 与 drop 互斥）。
 *
 * 返回值语义（include/uapi/linux/bpf.h 中 enum xdp_action）：
 *   XDP_DROP(1)  在驱动层直接丢弃，不生成 skb、不进协议栈；
 *   XDP_PASS(2)  交给内核协议栈继续处理；
 *   XDP_TX(4)/XDP_REDIRECT(6) 本章暂不使用。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* ETH_P_IP 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP 0x0800

enum { STAT_PASS = 0, STAT_DROP };

/* 选 per-CPU 数组：每 CPU 独立副本，数据面更新无锁、零竞争；loader 侧再求和 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

/* 小工具：计数 +1；lookup 失败静默跳过，不影响主路径 */
static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

SEC("xdp")
int xdp_drop(struct xdp_md *ctx)
{
    /* xdp_md 的 data/data_end 描述报文起止； verifier 只认显式边界检查，
       「人看起来安全」的代码一律拒绝加载 */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* Ethernet 头固定 14 字节；必须在解引用前确认 eth+1 不越过 data_end */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        bump(STAT_PASS);          /* 残包交协议栈：计入实际放行 */
        return XDP_PASS;          /* 残包交协议栈处理 */
    }

    if (eth->h_proto == bpf_htons(ETH_P_IP)) {
        bump(STAT_DROP);
        return XDP_DROP;          /* 教学演示：IPv4 全部丢弃 */
    }
    bump(STAT_PASS);              /* 非 IPv4（ARP 等）实际放行：计入 pass */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";   /* GPL 兼容声明，必需 */
```

三个读码要点：其一，`SEC("xdp")` 把函数标记为 XDP 程序类型（libbpf 据此设 `prog_type=BPF_PROG_TYPE_XDP`），但 section 名推断不出挂载到哪块网卡，挂载必须在用户态显式完成；`struct xdp_md` 的 `data`/`data_end` 描述报文起止，`(void *)(long)ctx->data` 是把 64 位上下文字段截回指针宽度的标准惯用法。其二，边界检查是硬性约束——删掉 `(void *)(eth + 1) > data_end` 后 clang 照样编译通过，加载却被验证器拒绝：验证器只认“指针与 `data_end` 的显式比较”作为定界证据，阶段 2 把这条规则展开成整节讲解。其三，统计选 per-CPU 数组映射是性能与语义的配套：每 CPU 独立副本、数据面自增无锁零竞争，`bump()` 里 lookup 失败静默跳过，计数失败不影响丢包决策；`bpf_htons` 只转换常量不转换字段；`LICENSE` 的 GPL 兼容声明缺了它部分辅助函数无法解析。

「加载-校验-JIT-挂载-触发」全链路：

```text
用户态 loader（xdp_drop.c）
    │ xdp_drop_bpf__open_and_load()
    v
打开对象 → 验证器逐指令校验 → JIT 编译为本机指令
    │ bpf_program__attach_xdp(prog, ifindex)
    v
XDP 挂载点（veth-fw-c 驱动收包路径）
    │ 报文到达
    v
xdp_drop() 执行：IPv4 → XDP_DROP；其余 → XDP_PASS
    │ bpf_map_lookup_elem(&stats, ...)
    v
loader 每秒轮询 per-CPU 统计并求和打印
```

loader 侧 `book/examples/chapter-17/01-xdp-drop/xdp_drop.c` 的加载与挂载主路径（`main` 节选；阶段 2–4 的 loader 复用同一骨架）。per-CPU 求和由 `sum_percpu()` 完成：用户态一次 `bpf_map_lookup_elem` 读回 `nr_cpus` 份 value 的连续拼接，逐份求和才是全机计数，`buf[64]` 是教学从简的 CPU 数上限（超限放弃统计）：

```c
    /* 加载 = 打开对象 -> verifier 校验 -> JIT；失败常见原因：权限不足、verifier 拒绝 */
    struct xdp_drop_bpf *skel = xdp_drop_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach。
       注意：libbpf 1.x attach 失败返回 ERR_PTR（非 NULL），必须用 libbpf_get_error 判错，
       仅判 !link 会漏掉真实失败 */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.xdp_drop, ifindex);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        xdp_drop_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", argv[2], ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        printf("pass=%llu drop=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP));
        sleep(1);
    }

    xdp_drop_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
```

挂载成功后 loader 每秒轮询一次并打印 `pass=%llu drop=%llu`；Ctrl-C 置位 `exiting` 退出后 `xdp_drop_bpf__destroy()` close 全部 fd，程序与映射随进程退出而卸载——阶段 1 的“卸载”是自动的，到阶段 4/5 的 TC filter 与 pin 对象就必须显式管理。libbpf 1.x 的 attach 失败返回 `ERR_PTR` 而非 `NULL`，必须用 `libbpf_get_error()` 判错，只判 `!link` 会漏掉真实失败。

### 学生动手：跑通第一个闭环

命令（从 `book/examples/chapter-17/01-xdp-drop/` 目录执行；运行需要 root 与实验拓扑）：

```bash
make                              # vmlinux.h -> xdp_drop.bpf.o -> skel.h -> xdp_drop
sudo ../scripts/setup-veth.sh     # 建立三命名空间 veth 拓扑（含残留命名空间防护）
sudo ./xdp_drop -i veth-fw-c      # 挂载（Ctrl-C 退出并自动卸载）
sudo ip netns exec ns-client ping 10.0.2.2   # 另一终端触发流量
```

预期输出示例（运行级待验证——编写环境为非 root，`unprivileged_bpf_disabled=2`，未执行加载与运行；本章此后所有「预期输出」同此口径，不再重复标注）：

```text
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
pass=0 drop=3
pass=0 drop=7
pass=0 drop=12
```

怎么读：`drop` 统计被丢弃的 IPv4 报文；`pass` 只计实际放行（残包与非 IPv4 如 ARP）——示例场景中 ARP 已在拓扑建立时完成，ping 期间没有新的非 IPv4 报文，`pass` 保持 0。IPv4 在驱动层即被丢弃，`10.0.2.2` 不可达、ping 全部超时——对照点正是：报文在协议栈之前就没了，但统计映射里看得见丢了多少。

### 检查点：自测三问

1. **被 `XDP_DROP` 的报文还会被协议栈看到吗？**（提示：区分 native 与 generic XDP。）
   答案提示：native 在驱动收包早期执行、报文未生成 skb，协议栈完全看不到；generic 工作在 skb 之后，但报文同样不会向上交付。
2. **为什么 `eth + 1 > data_end` 这个检查不可省？**（提示：验证器如何“定界”。）
   答案提示：验证器只认“与 `data_end` 的显式比较”作为指针区间证据，省略后解引用无法被证明安全、加载失败（clang 不报错，错误在加载期）。
3. **per-CPU 统计映射为什么要 loader 求和？**
   答案提示：每 CPU 一份独立副本，数据面只更新本 CPU 那份；用户态一次 lookup 读回全部副本的拼接，任何单份都不代表全机值。

**独立示例**：[`book/examples/chapter-17/01-xdp-drop/`](../examples/chapter-17/01-xdp-drop/)（`xdp_drop.bpf.c` + `xdp_drop.c` + Makefile + README）。

## 四、阶段 2：报文解析与 ACL

### 教师视角引入：从“无脑丢”到“看懂再判”

阶段 1 的程序只认“是不是 IPv4”，防火墙真正的判定单元是五元组（源/目的 IP、源/目的端口、协议）。类比阶段 1 的保安学会了拦人，本阶段要学会查证件：把报文逐层剥开，取出五元组，再到规则表里查这一条“该放还是该拦”。先立三条本阶段反复出现的规则：**验证器不信任任何“看起来安全”**——每访问一层报文头之前都要做一次与 `data_end` 的显式比较，少写一次加载就失败；**五元组“原样存、原样查”**——报文里的地址与端口本来就是网络字节序，映射 key 原样保存，用户态用 `inet_pton`/`htons` 产出的同样是网络字节序，两侧都不转换，哈希全等匹配才能命中；**查找优先级从具体到宽泛**——精确五元组 → 源地址最长前缀 → 默认放行。

容易卡住的地方：IPv4 头长可变（`ihl` 字段，以 4 字节为单位），跳过可选字段时必须在新指针上重做边界检查；hash 映射的 key 是“六个字段全等”，不支持通配——这既是本阶段的限制，也是练习的自然扩展方向。

### 核心代码原理讲解

**共享头：五元组与规则结构。** 以下节选自 `book/examples/chapter-17/02-parse-acl/fw_common.h`（字段语义、字节序、pad 一经定义不再改动，阶段 3/4 在此演化，控制面按同一契约镜像）：

```c
/* 五元组 key：IP/端口一律网络字节序存储（原样存原样查即可匹配，无需主机序转换）；
 * pad[3] 使结构体 4 字节对齐，哈希与字节级比较行为确定 */
struct flow_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};
```

配套结构逐一对应：`enum fw_action { FW_ALLOW=0, FW_DROP=1 }` 是动作取值；`rule_value` 与 `lpm_value` 都是 1 字节 action + 7 字节 pad（对齐到 8 字节，黑/白名单由 action 区分）；`lpm_key` 以 `__u32 prefixlen` 开头、后接 `__be32 addr`——LPM trie 的 key 布局约定。统计下标 `enum stat_idx` 阶段 2 共 4 项（`STAT_TOTAL`/`STAT_PASS`/`STAT_DROP_ACL`/`STAT_DROP_PARSE`），顺序即契约，控制面按顺序展示。

**映射定义。** `book/examples/chapter-17/02-parse-acl/acl.bpf.c` 的三张映射（完整原文）：

```c
/* hash：key 全等匹配，O(1)；适合精确封禁，不适合网段/端口范围 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key);
    __type(value, struct rule_value);
} acl_rules SEC(".maps");

/* LPM trie：按最长前缀匹配网段；BPF_F_NO_PREALLOC 必需（节省内存、允许大 max_entries） */
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, MAX_LPM_KEYS);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct lpm_key);
    __type(value, struct lpm_value);
} acl_lpm SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");
```

选型对比：hash 是 key 全等匹配、O(1)，适合精确封禁，不适合网段与端口范围；LPM trie 按最长前缀匹配网段，天然适合源地址黑/白名单；端口范围、多字段通配两者都不适合。`BPF_F_NO_PREALLOC` 是创建 LPM trie 的必要标志：trie 节点改为插入时按需分配、不为 `max_entries` 预分配内存，条目可观的网段规则映射借此节省内存、允许更大的容量上限。

**逐层解析与验证器推理。** 解析函数 `parse_flow`（同一文件，完整原文）：

```c
/*
 * 逐层解析：每访问一层头之前都做一次边界检查。
 * verifier 把 (eth+1) > data_end 这类比较当作「指针定界」证据；少写一次就加载失败。
 * 成功填充 *key 并返回 1；非 IPv4/TCP/UDP 或残包返回 0。
 */
static __always_inline int parse_flow(struct xdp_md *ctx, struct flow_key *key)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return 0;                       /* ARP/IPv6 等不在本章策略范围 */

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return 0;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return 0;

    /* IPv4 头长以 4 字节为单位（ihl 最小 5=20B）；跳过可选字段前必须重查边界 */
    __u32 ihl_len = ip->ihl * 4;
    if (ihl_len < sizeof(*ip))
        return 0;
    void *l4 = (void *)ip + ihl_len;

    key->src_ip  = ip->saddr;           /* 网络字节序原样保存 */
    key->dst_ip  = ip->daddr;
    key->proto   = ip->protocol;
    key->pad[0] = key->pad[1] = key->pad[2] = 0;

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end)
            return 0;
        key->src_port = tcp->source;
        key->dst_port = tcp->dest;
    } else {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end)
            return 0;
        key->src_port = udp->source;
        key->dst_port = udp->dest;
    }
    return 1;
}
```

为什么每一层都必须有边界检查？验证器沿指令流逐条模拟，跟踪每个寄存器里指针的“已知安全区间”：`data`/`data_end` 加载进寄存器后，报文指针的安全上限就是 `data_end`；`(void *)(eth + 1) > data_end` 把“指针 + 14 字节仍在区间内”变成验证器可用的证据，此后的 `eth->h_proto` 解引用才被放行。检查是**按路径**生效的：验证器对每个分支分别推理，哪条路径缺证据，就在哪条路径的哪个解引用上拒绝加载。删掉任一层检查，clang 编译照常通过，加载时的报错形态示意如下（不同内核版本措辞有差异，共同点是定位到第一个未定界的解引用）：

```text
; 假设删去 (void *)(tcp + 1) > data_end 后加载：
;   R2 invalid mem access 'pkt'     （示意；指向第一个未定界解引用所在的指令）
```

三个经典细节：`ihl` 处理是 IPv4 特有的坑——头长最小 5（20 字节）、带可选字段时更大，`ihl_len < sizeof(*ip)` 先拒掉 `ihl<5` 的畸形头，再以 `l4 = (void *)ip + ihl_len` 跳过可选字段，而 L4 的边界检查是在**新指针 `l4` 上重新做**的，旧证据不会自动继承；协议过滤在提取端口之前，ARP/IPv6 等直接返回 0（调用方按“残包”处理）；字段按网络字节序原样保存，`bpf_htons` 只用于“常量与字段比较”。

**教学边界（如实说明）**：`parse_flow` 不检查 IP 分片（无 `frag_off` 判断）。非首片的分片没有 L4 头，本实现会从载荷位置“解析”出垃圾端口——这些五元组通常不命中任何规则，按默认策略放行。教学示例接受这一边界，生产实现必须在 `ihl` 检查之后补分片处理。

**两级 ACL 与查找路径。** 判定函数与主程序（`acl.bpf.c` 完整原文）：

```c
static __always_inline __u8 acl_verdict(const struct flow_key *flow)
{
    struct rule_value *exact = bpf_map_lookup_elem(&acl_rules, flow);
    if (exact)
        return exact->action;

    /* LPM 查找 key 的 prefixlen 固定 32：按主机地址查，由 trie 返回最长前缀规则 */
    struct lpm_key lk = { .prefixlen = 32, .addr = flow->src_ip };
    struct lpm_value *lv = bpf_map_lookup_elem(&acl_lpm, &lk);
    if (lv)
        return lv->action;

    return FW_ALLOW;
}

SEC("xdp")
int acl_prog(struct xdp_md *ctx)
{
    struct flow_key flow;
    if (!parse_flow(ctx, &flow)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    if (acl_verdict(&flow) == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
    bump(STAT_PASS);
    return XDP_PASS;
}
```

查找路径可视化（与源码头注释同口径）：

```text
        报文（已解析出五元组 flow）
              │
              v
  ┌─ 1) acl_rules 精确五元组查找 ──命中──► 返回该规则的 action（最具体，优先）
  │              │ 未命中
  │              v
  └─ 2) acl_lpm 源地址最长前缀匹配 ──命中──► 返回该网段规则的 action
                 │（LPM key 的 prefixlen 固定 32：按主机地址查，trie 返回最长前缀）
                 v
        3) 默认 FW_ALLOW（教学基线；生产默认策略应可配置）
```

主函数的统计口径：`STAT_TOTAL` 只计成功解析出五元组的报文；`STAT_PASS` 只计实际放行——残包与非 IPv4 报文也实际放行，计入 `pass` 但不计入 `total`；`STAT_DROP_PARSE` 阶段 2 保留未用、恒 0。对解析成功的报文，`pass + drop_acl = total` 成立。

**loader 侧：命令行规则下发。** 以下为 `book/examples/chapter-17/02-parse-acl/acl.c` 中 `parse_cidr_arg` 的完整原文（`parse_rule_arg` 同理，六个字段逐一解析为 `flow_key`/`rule_value`）：

```c
/* 解析 --cidr <action>,<cidr>（源地址网段，写 LPM map） */
static int parse_cidr_arg(const char *s, struct lpm_key *k, struct lpm_value *v)
{
    char action[8], cidr[64], *slash;
    memset(k, 0, sizeof(*k));
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%7[^,],%63s", action, cidr) != 2)
        return -1;
    if ((slash = strchr(cidr, '/')) == NULL)
        return -1;                       /* 缺 '/'：不是 CIDR 记法 */
    *slash = '\0';                       /* 拆出地址与前缀长度两段 */
    k->prefixlen = (unsigned char)atoi(slash + 1);
    if (k->prefixlen > 32 || inet_pton(AF_INET, cidr, &k->addr) != 1)
        return -1;                       /* 前缀长度超界或地址非法（/256 会被截断成 /0，需输入 0-32） */
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}
```

下发流程：loader 先在参数阶段解析全部 `--rule`/`--cidr`（两者可重复；尽早发现格式错误，非 root 也能自查拼写），`open_and_load` 成功后再遍历缓存、用 `bpf_map_update_elem(fd, key, value, BPF_ANY)` 写入映射，最后 attach XDP、每 2 秒打印 4 项统计。

`parse_cidr_arg` 用 `atoi` 换算前缀长度，属**教学从简**的取舍，解析行为要如实讲清：`/abc` 这类非数字路径 `atoi` 返回 0，前缀长度 0 意味着**匹配所有源地址**（本想封一个网段，实际封了全网）；`/24x` 这类尾部杂讯被静默取 24；`/256` 超出 `unsigned char` 表达范围、截断成 `/0`，效果同第一条。生产实现应改用 `strtol` + `endptr` 校验整段数字、错误即拒绝下发；本示例保留 `atoi` 是为了把篇幅留给数据面，把它改严正是熟悉这条链路的第一个练习。loader 侧重新定义 `flow_key` 等结构体的原因：skeleton 只导出 map/program 句柄、不导出 BPF 源码里的类型，而 `fw_common.h` 依赖 `vmlinux.h`（内核 BTF）不能被用户态包含——`acl.c` 按契约逐字段镜像，改任一侧必须同步另一侧，阶段 5 的 Go 控制面同理。
### 学生动手：看懂再判

三个终端的动手顺序（命令从 `book/examples/chapter-17/02-parse-acl/` 执行）：

```bash
# 终端 A：服务端命名空间起 iperf3 服务端
sudo ip netns exec ns-server iperf3 -s -p 5201
# 终端 B：客户端先验证基线连通（未下发规则时应能看到正常吞吐）
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -p 5201 -t 30
# 终端 C：挂载程序并下发固定端口五元组的丢弃规则，观察吞吐归零
sudo ./acl -i veth-fw-c --rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201
# 更简单的演示：不依赖端口的网段规则
sudo ./acl -i veth-fw-c --cidr drop,10.0.0.0/24
```

预期输出示例（`--cidr drop,10.0.0.0/24` 后，客户端跑 `iperf3 -c 10.0.2.2 -p 5201`）：

```text
已下发规则：精确五元组 0 条，源地址网段 1 条。
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0
total=512 pass=0 drop_acl=512 drop_parse=0
total=1337 pass=0 drop_acl=1337 drop_parse=0
```

怎么读：客户端 10.0.0.2 属于 10.0.0.0/24，每个 IPv4/TCP 报文命中网段规则被丢弃，`total` 与 `drop_acl` 同步递增（统计跨刷新累计、不清零——`total` 从 512 跳到 1337 是客户端多轮建流尝试累计的结果），iperf3 无法建立连接、吞吐为 0。若不下发任何规则（只写 `-i`），`total` 与 `pass` 同步递增——这正是查找路径第 3 步的默认放行。一个高频困惑：`--rule` 是六个字段**全等**的精确匹配，而 iperf3 客户端的源端口默认随机，规则可能永远不命中——用 `ss -tn` 在防火墙命名空间查真实五元组再按实下发，或改用 `--cidr`。

### 检查点：自测三问

1. **LPM trie 映射为什么必须带 `BPF_F_NO_PREALLOC`？**（提示：trie 节点的内存分配时机。）
   答案提示：不带该标志时内核按 `max_entries` 预分配；带标志后节点按需分配，才能设 4096 的容量上限而不预占内存。
2. **`ihl` 为什么要乘 4？为什么 `l4` 指针上还要重查边界？**
   答案提示：IPv4 头长的单位是 4 字节；`l4` 是新指针，旧指针的定界证据不随指针算术自动转移，越界与否必须重新比较。
3. **hash 精确五元组为什么做不到“匹配任意源端口”？**
   答案提示：hash 是 key 全等匹配，`flow_key` 里没有掩码或通配语义；端口填 0 也只是“匹配端口为 0 的报文”。通配要么展开成多条精确规则，要么换数据结构。

**独立示例**：[`book/examples/chapter-17/02-parse-acl/`](../examples/chapter-17/02-parse-acl/)（`acl.bpf.c` + `acl.c` + `fw_common.h` + Makefile + README）。

## 五、阶段 3：连接跟踪

### 教师视角引入：把报文组织成连接

阶段 2 的判定是逐包的、无状态的。真实防火墙需要“连接”的概念——首包判定一次，后续报文凭状态放行，还要能回答“现在有多少条活跃连接、每条跑了多少流量”。类比保安发访客牌：登记（首包）→ 命中（同向/反向报文）→ 状态推进（收尾）→ 回收（超时清理）。

两个容易讲反的点先立好口径：**LRU 表满不会拒绝新连接**——`BPF_MAP_TYPE_LRU_HASH` 满时自动淘汰最久未用项，新 key 插入**仍然成功**，淘汰是静默的，只能靠统计与控制面观测，下文的 `ct_full` 计数与满表无关；**单一挂载点只看得到接收方向**——本拓扑把 XDP 挂在 `veth-fw-c` 上，服务端回包从 `veth-fw-s` 进入 ns-fw、再从 `veth-fw-c` **发出**（发送方向），XDP 看不见，而 `CT_NEW → CT_ESTABLISHED` 由“反向报文”触发，因此本拓扑的预期输出里 `ct_established` 恒为 0、连接停在 `state=NEW`。这是“挂载点决定数据面可见性”的活教材，不是程序缺陷——正反 key 查找逻辑已就绪，反向报文可见即触发。

### 核心代码原理讲解

**共享头追加。** 阶段 3 在 `book/examples/chapter-17/03-conntrack/fw_common.h` 中追加连接跟踪部分：容量常量 `CT_MAX_ENTRIES = 65536`（注释即口径：满后内核静默淘汰最久未用项，新连接登记仍成功）；`enum ct_state` 定义最小状态机（完整原文）：

```c
/* 最小可用 TCP 状态机（不复刻 netfilter conntrack 全量语义） */
enum ct_state {
    CT_NEW         = 1,   /* 首包登记（TCP SYN 或 UDP 首包） */
    CT_ESTABLISHED = 2,   /* 看到反向报文 */
    CT_FIN_WAIT    = 3,   /* 收到 FIN，等待对端收尾 */
    CT_CLOSED      = 4,   /* 收到 RST 或双向收尾；控制面超时后可清理 */
};
```

`ct_key` 与 `flow_key` 同构——一条连接的两个方向各构成一个 key；`ct_value` 含 `last_seen_ns`（来自 `bpf_ktime_get_ns()`，单调时钟非墙钟）、`packets`/`bytes` 计量与 `state`。统计下标扩为 7 项——新增 `STAT_CT_NEW`/`STAT_CT_ESTABLISHED`/`STAT_CT_FULL`，追加在 `STAT_DROP_PARSE` 与 `STAT_MAX` 之间，既有下标值不变。

**正反 key 与 LRU 映射。** `book/examples/chapter-17/03-conntrack/conntrack.bpf.c` 的映射定义与正反 key 构造（完整原文）：

```c
/* LRU hash：容量固定，满时自动淘汰最久未用项——避免表无限增长拖垮内存，
 * 代价是「淘汰是静默的」，只能靠统计与控制面观测 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, CT_MAX_ENTRIES);
    __type(key, struct ct_key);
    __type(value, struct ct_value);
} conntrack SEC(".maps");

/* 正反 key 构造：reverse=0 取报文自身方向（正向）；reverse=1 源/目的互换（反向）。
 * 协议不变；pad 清零保证与查找侧逐字节一致 */
static __always_inline void fill_ct_key(struct ct_key *k, const struct flow_key *f, __u8 reverse)
{
    k->src_ip   = reverse ? f->dst_ip   : f->src_ip;
    k->dst_ip   = reverse ? f->src_ip   : f->dst_ip;
    k->src_port = reverse ? f->dst_port : f->src_port;
    k->dst_port = reverse ? f->src_port : f->dst_port;
    k->proto    = f->proto;
    k->pad[0] = k->pad[1] = k->pad[2] = 0;
}
```

正反 key 互换规则示意：

```text
连接：client 10.0.0.2:41322  <->  server 10.0.2.2:5201（proto=6）

正向 key（reverse=0）：10.0.0.2:41322 -> 10.0.2.2:5201 proto=6   （报文自身方向）
反向 key（reverse=1）：10.0.2.2:5201 -> 10.0.0.2:41322 proto=6   （源/目的互换，协议不变）

一条连接只登记其中一个 key：首个被看到的报文方向决定登记方向，
dump 出的条目因此是“归一化到首包方向”的。
```

`parse_flow` 相对阶段 2 只增加一个出参 `tcp_flags`（仅 TCP 有效；UDP 分支置 0，失败路径不写、调用方已预初始化）——以下为 `book/examples/chapter-17/03-conntrack/conntrack.bpf.c` 节选，TCP 标志位从位域成员打包为 4 bit：

```c
        *tcp_flags = (tcp->fin ? TCP_FLAG_FIN : 0) | (tcp->syn ? TCP_FLAG_SYN : 0)
                   | (tcp->rst ? TCP_FLAG_RST : 0) | (tcp->ack ? TCP_FLAG_ACK : 0);
```

源码注释专门说明了位域的可移植性边界：位域布局依赖编译器与字节序——x86_64/arm64 均为小端、`vmlinux.h` 的 `tcphdr` 位域顺序与报文中标志位顺序一致，此处访问安全；大端平台需改用位运算自行解析。最小状态机与迁移规则：

```text
                 首包（TCP SYN 或 UDP 首包）
   （不存在）────────────────────────────► CT_NEW(1)
                                              │ 反向报文（from_reply）
                                              v
                                         CT_ESTABLISHED(2)

  任意状态（仅 TCP）：带 RST → CT_CLOSED(4)；带 FIN → CT_FIN_WAIT(3)
  （RST 优先于 FIN 判断，两者直接覆盖当前状态）

  UDP 无状态机：只有 CT_NEW。TCP 非 SYN 的“中途报文”查不到记录时跳过登记
  （避免把观测开始前已建立的连接误登记成新连接）。超时回收归控制面。
```

**主程序。** `conntrack_prog`（完整原文）：

```c
SEC("xdp")
int conntrack_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u64 pkt_len = data_end - data;

    struct flow_key flow;
    __u8 flags = 0;
    if (!parse_flow(ctx, &flow, &flags)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    struct ct_key fwd, rev;
    fill_ct_key(&fwd, &flow, 0);
    fill_ct_key(&rev, &flow, 1);
    __u64 now = bpf_ktime_get_ns();

    /* 先查正向（报文自身方向），再查反向（回包方向） */
    struct ct_value *ct = bpf_map_lookup_elem(&conntrack, &fwd);
    __u8 from_reply = 0;
    if (!ct) {
        ct = bpf_map_lookup_elem(&conntrack, &rev);
        from_reply = 1;
    }

    if (ct) {
        /* 多 CPU 同时更新同一表项存在写竞争（后写覆盖）；教学实现接受该近似，
           正文与注释必须说明，生产可改 per-CPU 计量或 spin_lock */
        ct->last_seen_ns = now;
        ct->packets++;
        ct->bytes += pkt_len;
        if (ct->state == CT_NEW && from_reply) {
            ct->state = CT_ESTABLISHED;          /* 反向报文 = 连接建立 */
            bump(STAT_CT_ESTABLISHED);
        }
        if (flow.proto == IPPROTO_TCP) {
            if (flags & TCP_FLAG_RST)
                ct->state = CT_CLOSED;
            else if (flags & TCP_FLAG_FIN)
                ct->state = CT_FIN_WAIT;
        }
        bump(STAT_PASS);
        return XDP_PASS;                         /* 阶段 3 只观测，不做策略 */
    }

    /* 新连接登记：TCP 必须从 SYN 开始（非 SYN 且无记录的中途报文跳过登记） */
    if (flow.proto == IPPROTO_TCP && !(flags & TCP_FLAG_SYN)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    struct ct_value nv = {
        .state = CT_NEW,
        .last_seen_ns = now,
        .packets = 1,
        .bytes = pkt_len,
    };
    if (bpf_map_update_elem(&conntrack, &fwd, &nv, BPF_NOEXIST) == 0)
        bump(STAT_CT_NEW);
    else
        bump(STAT_CT_FULL);   /* 非 0 = 同 key 并发登记竞争（-EEXIST）或分配失败：放行并计数。
                                 LRU 满表不走此分支——内核静默淘汰最久未用项后插入仍成功
                                 （LRU「不阻塞新流」的设计行为）；是否对失败 DROP 由正文讨论取舍 */
    bump(STAT_PASS);
    return XDP_PASS;
}
```

逐段讲解：先查正向再查反向——反向命中说明这是回包（`from_reply=1`），正是 `CT_NEW → CT_ESTABLISHED` 的触发条件；命中路径只刷新时间戳、累加计量、按 TCP 标志推进状态，阶段 3 只观测、恒 `XDP_PASS`。**写竞争如实说明**：多 CPU 同时更新同一表项存在丢更新竞争（后写覆盖），教学实现接受该近似，生产可改 per-CPU 计量或 `bpf_spin_lock`。**`STAT_CT_FULL` 的真实触发路径**：`bpf_map_update_elem` 返回非 0 才计数，而非 0 只剩多 CPU 同 key 并发登记竞争（`-EEXIST`）与内存分配失败（`-ENOMEM`）两种场景——**LRU 满表不走这个分支**；策略上本阶段对登记失败选择放行并计数（观测优先）。**超时回收为什么归控制面**：数据面每包做超时扫描等于把 O(表长) 成本压进高频路径，表项只带 `last_seen_ns`，回收由控制面遍历、删除超过阈值的表项；时钟基准必须同源（`bpf_ktime_get_ns` 基于 `CLOCK_MONOTONIC`），混用墙钟会算出巨大或负的 idle。本章的教学实现没有提供清理子命令，表项仅受 LRU 容量约束自然淘汰。

**dump：弱遍历。** loader 侧 `book/examples/chapter-17/03-conntrack/conntrack.c` 的 `dump_conntrack`（完整原文）：

```c
/* 遍历是「无快照保证」的弱遍历：遍历期间表项可能被 LRU 淘汰，读到啥打印啥 */
static void dump_conntrack(int fd)
{
    struct ct_key cur = {}, next;
    struct ct_value v;
    while (bpf_map_get_next_key(fd, &cur, &next) == 0) {
        if (bpf_map_lookup_elem(fd, &next, &v) == 0) {
            struct in_addr s = { .s_addr = next.src_ip }, d = { .s_addr = next.dst_ip };
            /* 不能在一条 printf 里调用两次 inet_ntoa：它返回线程内静态缓冲区，
             * 第二次调用会覆盖第一次的结果（两个 IP 打成同一个），
             * 故改用可重入的 inet_ntop 各自写入独立缓冲区 */
            char sstr[INET_ADDRSTRLEN], dstr[INET_ADDRSTRLEN];
            printf("%-15s:%-5u -> %-15s:%-5u proto=%u state=%u idle=%.1fs pkts=%llu\n",
                   inet_ntop(AF_INET, &s, sstr, sizeof(sstr)), ntohs(next.src_port),
                   inet_ntop(AF_INET, &d, dstr, sizeof(dstr)), ntohs(next.dst_port),
                   next.proto, v.state,
                   now_s() - v.last_seen_ns / 1e9,
                   (unsigned long long)v.packets);
        }
        cur = next;
    }
}
```

`bpf_map_get_next_key` 逐个取下一个 key，遍历期间表项可能正被 LRU 淘汰（`lookup` 失败就跳过）——“无快照保证”的弱遍历。打印陷阱：`inet_ntoa` 返回线程内静态缓冲区，同一条 `printf` 调用两次会互相覆盖，所以用可重入的 `inet_ntop`；`now_s()` 用 `clock_gettime(CLOCK_MONOTONIC)` 与内核侧时间戳同源，`idle` 才是真实现。

### 学生动手：观察连接的一生

```bash
cd book/examples/chapter-17/03-conntrack
make
sudo ../scripts/setup-veth.sh
sudo ./conntrack -i veth-fw-c        # 每 2 秒打印 7 项统计并 dump 连接表
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -p 5201 -t 30   # 另一终端建一条 TCP 流
```

预期输出示例（iperf3 默认控制与数据共用一条连接，dump 中就是一条表项）：

```text
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0 ct_new=0 ct_established=0 ct_full=0

total=52 pass=52 drop_acl=0 drop_parse=0 ct_new=1 ct_established=0 ct_full=0
10.0.0.2       :41322 -> 10.0.2.2       :5201  proto=6 state=1 idle=0.1s pkts=52

total=1889 pass=1889 drop_acl=0 drop_parse=0 ct_new=1 ct_established=0 ct_full=0
10.0.0.2       :41322 -> 10.0.2.2       :5201  proto=6 state=3 idle=1.3s pkts=1889
```

怎么读：`state` 即 `enum ct_state` 取值（1=NEW、2=ESTABLISHED、3=FIN_WAIT、4=CLOSED）。建流时 SYN 首包以正向 key 登记 `state=1`；iperf3 结束后客户端发 FIN，状态推进为 `3`，`idle` 持续增长——表项不会自行消失，超时回收归控制面（本阶段 loader 未实现清理）。只有一条流时 `pkts` 与 `total` 同步递增；`pass ≥ total` 的差值是残包与非 IPv4/TCP/UDP 报文。

两个预期中的“看不见”：`ct_established` 恒为 0（反向报文不经过挂载点，要看到 `state=2` 需把同一程序挂到 `veth-fw-s`）；`ct_full` 恒为 0——65536 容量在教学拓扑打不满，把 `CT_MAX_ENTRIES` 临时调小（如 16）重建、多连接并发时，看到的是 `ct_new` 持续增长、旧表项在 dump 里**静默消失**，而 `ct_full` 仍是 0——“为什么调小容量也看不到 `ct_full`”本身就是本阶段要讲清的教学点。

### 检查点：自测三问

1. **为什么只登记一个方向的 key，而不是正反各登记一条？**
   答案提示：正反 key 指向同一条连接，登记两条浪费容量且要维护两份状态一致性；“先查正向再查反向”已覆盖两个方向。
2. **LRU 表满时 `ct_full` 会递增吗？什么情况下才递增？**
   答案提示：不会。满表时内核静默淘汰最久未用项、插入仍成功；`ct_full` 只在同 key 并发登记竞争（`-EEXIST`）或分配失败（`-ENOMEM`）时递增。
3. **`idle` 字段为什么必须用 `CLOCK_MONOTONIC` 计算？**
   答案提示：`last_seen_ns` 来自单调时钟 `bpf_ktime_get_ns()`；用墙钟相减会因校时/时区变化得到巨大或负值。

**独立示例**：[`book/examples/chapter-17/03-conntrack/`](../examples/chapter-17/03-conntrack/)（`conntrack.bpf.c` + `conntrack.c` + `fw_common.h` + Makefile + README）。

## 六、阶段 4：TC 与 NAT、校验和

### 教师视角引入：从“判”到“改”

前三个阶段都在 XDP 上“只读不写”。NAT 要改写报文——改地址、改端口、同步校验和——失败模式全新：改了字段没改校验和，报文能被 tcpdump 抓到但对端静默丢弃；改写顺序不对，验证器直接拒绝加载。类比改快递面单：改了收件地址，条码（校验和）也得重打。本阶段同时完成程序类型切换：XDP → TC，两个上下文的差异先列表立好：

| 维度 | XDP（`xdp_md`） | TC（`__sk_buff`） |
|---|---|---|
| 报文访问 | `data`/`data_end` 裸指针，直接解引用 | 同样有 `data`/`data_end`，另有 `bpf_skb_load_bytes` 等按偏移读取的辅助函数 |
| 改包方式 | 直接解引用写（自行负责一切） | `bpf_skb_store_bytes`（绝对偏移，内核代管缓冲区状态） |
| 校验和 | 全部手工增量计算 | `bpf_l3_csum_replace`/`bpf_l4_csum_replace` |
| 动作返回值 | `XDP_PASS`/`XDP_DROP`/`XDP_TX` 等 | `TC_ACT_OK`/`TC_ACT_SHOT` 等（`direct-action` 模式下即 verdict） |
| 挂载模型 | `bpf_program__attach_xdp`（bpf_link） | clsact qdisc + filter（netlink：`bpf_tc_hook_create`/`bpf_tc_attach`） |
| 生命周期 | link 对象销毁即卸载 | filter 持有引用，**不随进程退出自动卸载** |

一个高频误区：**TC 程序不能靠 skeleton 的 `__attach()` 自动挂载**——skeleton 的自动挂载只覆盖能从 section 名推断挂载方式的类型，TC 走 netlink 的 qdisc/filter 模型，必须显式创建 clsact、显式 attach。

### 核心代码原理讲解

**共享头追加。** 阶段 4 在 `book/examples/chapter-17/04-nat-tc/fw_common.h` 追加 NAT 部分：`NAT_MAX_ENTRIES = 4096`；`nat_key` 与 `flow_key` 同构，但语义不同——**key 中为 0 的字段是通配维度**（数据面精确 key 未命中时把报文侧对应字段清零再查一次即命中，下发侧据此写通配表项）；`nat_value` 是改写目标（完整原文）：

```c
/* is_snat：1=改源（SNAT，egress）0=改目的（DNAT，ingress）；
 * 数据面用 dir 过滤：DNAT 程序只认 is_snat==0 的表项，反之亦然 */
struct nat_value {
    __be32 new_addr;
    __be16 new_port;      /* 0 = 只改地址不改端口（SNAT 场景） */
    __u8   is_snat;
    __u8   pad;
};
```

统计下标扩为 8 项，新增 `STAT_NAT`（追加在 `STAT_CT_FULL` 与 `STAT_MAX` 之间）。

**映射与读端口。** `book/examples/chapter-17/04-nat-tc/nat.bpf.c` 中 `nat_map` 为普通 hash（key `nat_key`、value `nat_value`、容量 `NAT_MAX_ENTRIES=4096`）：NAT 规则是控制面静态下发的有限集合，没有“随流量增长、需自动淘汰”的压力，普通 hash 语义确定（不存在静默淘汰导致规则消失）；统计映射沿用 per-CPU 数组。读端口用 `load_ports`，走 `bpf_skb_load_bytes`——其源码注释点明选型原因：**读辅助函数不改动报文，不会使包指针失效**，这是与后面改写辅助函数的关键区别。

**改写核心 `do_nat`。** 以下为 `do_nat` 完整原文（两个入口 `nat_ingress`/`nat_egress` 只是 `dir` 取 0/1 的差别：ingress DNAT、egress SNAT）：

```c
/*
 * 统一改写核心：dir=0 DNAT（改目的地址/端口），dir=1 SNAT（改源地址/端口）。
 *
 * 实现要点（正文逐条讲解）：
 *  1) 所有包指针必须在第一个改写 helper 调用之前用完——bpf_skb_store_bytes /
 *     csum_replace 可能使 verifier 判定包指针失效；
 *  2) 改写字段用 store_bytes（绝对偏移），旧值/新值都先读入局部变量；
 *  3) 校验和用增量算法：改动 4 字节地址等价于在原校验和上叠加 ~(old)+new 的进位折叠，
 *     bpf_l4_csum_replace 带 BPF_F_PSEUDO_HDR 处理 TCP/UDP 伪头中的地址项；
 *  4) UDP 校验和为 0 表示「不校验」，此时不得调用 helper（会被改坏语义）；
 *  5) 通配规则：精确 key 未命中时，把「非 NAT 侧」的地址/端口清零再查一次；
 *  6) new_port==0 表示「只改地址不改端口」（SNAT 场景），此时跳过端口改写与端口增量。
 */
static __always_inline int do_nat(struct __sk_buff *skb, __u8 dir)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 l3_off = sizeof(struct ethhdr);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;                /* 残包直接放行：verifier 要求先定界再解引用 */
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return TC_ACT_OK;                /* ARP/IPv6 等不参与 NAT */

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return TC_ACT_OK;                /* 教学实现只改写 TCP/UDP（校验和增量的两种原型） */

    __u32 l4_off = l3_off + ip->ihl * 4;
    __be16 sport = 0, dport = 0;
    if (!load_ports(skb, l4_off, ip->protocol, &sport, &dport))
        return TC_ACT_OK;

    struct nat_key key = {
        .src_ip = ip->saddr, .dst_ip = ip->daddr,
        .src_port = sport, .dst_port = dport,
        .proto = ip->protocol,
    };
    struct nat_value *v = bpf_map_lookup_elem(&nat_map, &key);
    if (!v) {
        struct nat_key wild = key;
        if (dir == 0) { wild.src_ip = 0; wild.src_port = 0; }   /* DNAT：源侧通配 */
        else          { wild.dst_ip = 0; wild.dst_port = 0;     /* SNAT：目的侧通配 */
                        wild.src_port = 0; }   /* 源端口一并通配：--snat 只有 IP 入参，
                                                  出方向源端口是内核随机分配的临时值，
                                                  静态规则写不出，按 (源 IP, 协议) 命中 */
        v = bpf_map_lookup_elem(&nat_map, &wild);
    }
    if (!v || v->is_snat != dir)         /* 方向过滤：DNAT 程序只认 is_snat==0，反之亦然 */
        return TC_ACT_OK;

    __u8  is_tcp    = (ip->protocol == IPPROTO_TCP);
    __u32 old_addr  = (dir == 0) ? ip->daddr : ip->saddr;
    __u32 new_addr  = v->new_addr;
    __be16 old_port = (dir == 0) ? dport : sport;
    __be16 new_port = v->new_port;
    __u8  do_port   = (v->new_port != 0);   /* new_port==0：仅改地址（SNAT 场景） */
    __u32 addr_off  = l3_off + (dir == 0 ? offsetof(struct iphdr, daddr)
                                         : offsetof(struct iphdr, saddr));
    /* tcphdr/udphdr 的 source/dest 布局相同：SNAT 改 offset 0，DNAT 改 offset 2 */
    __u32 port_off  = l4_off + (dir == 0 ? 2 : 0);
    __u32 l4_ck_off = l4_off + (is_tcp ? offsetof(struct tcphdr, check)
                                       : offsetof(struct udphdr, check));

    /* 1) 改写端口与地址（先改字段，后修校验和：helper 参数自带 old/new，顺序无关正确性） */
    if (do_port)
        bpf_skb_store_bytes(skb, port_off, &new_port, sizeof(new_port), 0);
    bpf_skb_store_bytes(skb, addr_off, &new_addr, sizeof(new_addr), 0);

    /* 2) L4 校验和：地址增量按伪头计（BPF_F_PSEUDO_HDR），端口增量 2 字节 */
    if (is_tcp) {
        bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                            sizeof(__u32) | BPF_F_PSEUDO_HDR);
        if (do_port)
            bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
    } else {
        __u16 ck = 0;
        bpf_skb_load_bytes(skb, l4_ck_off, &ck, sizeof(ck));
        if (ck != 0) {                       /* UDP 校验和 0 = 不校验，跳过 */
            bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                                sizeof(__u32) | BPF_F_PSEUDO_HDR);
            if (do_port)
                bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
        }
    }

    /* 3) IP 头校验和最后更新（只含地址增量，与端口无关） */
    bpf_l3_csum_replace(skb, l3_off + offsetof(struct iphdr, check),
                        old_addr, new_addr, sizeof(__u32));

    bump(STAT_NAT);                          /* 只计实际改写成功的报文 */
    return TC_ACT_OK;
}
```

逐条讲解（与源码头注释同口径）：**指针失效约束是第一优先级**——改写类辅助函数可能重排报文缓冲区，验证器把这类调用视为“包指针失效点”，之后再解引用 `eth`/`ip` 直接加载失败，所以 `do_nat` 先把所有需要的信息读进局部变量（地址、端口、old/new、三个偏移）才调第一个改写辅助函数；约束的边界在“所有指针读取 vs 第一个改写调用”之间，“先改字段还是先修校验和”不影响正确性——校验和辅助函数的入参自带 old/new 增量、不读报文。**为什么用绝对偏移改写**——skb 数据区可能非线性（分页）或被内核移动，直接解引用写只对线性区安全且绕过内核的缓冲区状态维护，`bpf_skb_store_bytes(skb, off, ...)` 由内核处理数据区变化；`port_off` 用数字 0/2 是因为 `tcphdr`/`udphdr` 的 `source`/`dest` 布局相同。**两段通配查找与方向过滤**——DNAT（dir=0）清零源侧（客户端源端口不可预知），SNAT（dir=1）清零目的侧与源端口（源端口是内核随机分配的临时值，按“源 IP + 协议”命中），命中后 `is_snat == dir` 校验让一张表服务两个方向互不误伤。**UDP 校验和 0 是哨兵值**——0 表示“发送方未计算校验和”（真实零按约定以 0xFFFF 发送），对 0 值报文调用校验和辅助函数会把哨兵改写成非 0、接收方误判为校验和错误丢包，所以先读原值判 0 跳过（内核也提供 `BPF_F_MARK_MANGLED_0` 处理同一约定，教学实现用显式判 0 把协议语义摆在明面上）。

**增量校验和的数学直觉。** 反码和校验和是“所有 16 位字的反码和再取反”，对单个字段 old→new 的改动可增量维护、无需重算整包（RFC 1624 口径）：`HC' = ~（~HC + ~old + new）`，加法全部用 16 位反码算术、进位回卷到低位。手算一个 16 位例子（SNAT 把源地址 10.0.0.2 改为 10.0.2.1，只有第二个 16 位字变化：0x0002 → 0x0201；设当前校验和 HC = 0x1234）：

```text
~old = ~0x0002 = 0xfffd
~HC  = ~0x1234 = 0xedcb
0xedcb + 0xfffd = 0x1edc8 ──进位回卷──► 0xedc8 + 0x0001 = 0xedc9
0xedc9 + 0x0201 = 0xefca
HC'  = ~0xefca = 0x1035
```

`bpf_l3_csum_replace`/`bpf_l4_csum_replace` 以 `(from, to)` 增量入参把这段算术封进内核——理解它才能回答“为什么改动 4 字节地址要调两次辅助函数（L4 伪头一次、IP 头一次）”。IP 头校验和只补地址增量、与端口无关，且最后更新。

**挂载方向与 loader。** DNAT 与 SNAT 的挂载位置由转发路径决定：`nat_ingress`（DNAT）挂 `veth-fw-c` ingress——赶在 ns-fw 路由查找**之前**改目的地址，路由才知道把报文往 `veth-fw-s` 转发；`nat_egress`（SNAT）挂 `veth-fw-s` egress——路由决策后、出接口前改源地址。loader 侧（`book/examples/chapter-17/04-nat-tc/nat.c`）的规则解析把通配契约落在用户态：`--dnat <proto>,<原目的IP>,<原目的端口>,<新IP>,<新端口>` 由 `parse_dnat_arg` 解析，写源侧通配 key（`src_ip/src_port` 清零，目的侧精确，`is_snat = 0`）；`--snat <原源IP>,<新IP>` 由 `parse_snat_arg` 解析，写目的侧通配 key（`dst_ip/dst_port/src_port` 清零，`new_port = 0` 即“只改地址不改端口”，`is_snat = 1`）——两侧清零的字段与数据面二次查找清零的字段严格对应。

TC 挂载走 netlink：`bpf_tc_hook_create` 创建 clsact qdisc（已存在返回 `-EEXIST`，视为成功），`bpf_tc_attach` 把程序绑到 ingress/egress，`handle`/`priority` 是 filter 的定位键。关键的生命周期差异：**TC filter 持有程序引用、不随 loader 进程退出自动卸载**——退出前必须显式 `bpf_tc_detach`；被 `kill -9` 的残留会让下次同 handle/priority 的 attach 报 EEXIST，需手工 `tc filter del` 清理。

**教学边界（如实说明）**：其一，`do_nat` 不复检 `ihl<5`——`l4_off` 会落进 IP 头内部，读出的是垃圾端口、查不到规则而按 `TC_ACT_OK` 放行；也不处理 IP 分片（非首片没有 L4 头，同样从载荷位置读出垃圾端口）。其二，本阶段的挂载组合**不含回程 un-NAT**：服务端回包从 `veth-fw-s` 进入（ingress 无程序）、再从 `veth-fw-c` 发出（egress 无程序），两个挂载点都看不见回程，回程报文原样到达客户端，握手无法完成。这与阶段 3 的可见性口径同源——挂载点决定数据面可见性；`do_nat` 的通配与方向过滤逻辑已就绪，把同一张表按镜像规则补挂回程两个方向即可补全 un-NAT（数据面无需改动）。

### 学生动手：抓包看改写

```bash
cd book/examples/chapter-17/04-nat-tc
make
sudo ../scripts/setup-veth.sh
# 下发 NAT 规则并挂载（Ctrl-C 退出时显式 detach）：
sudo ./nat -i veth-fw-c -e veth-fw-s \
     --dnat tcp,10.0.1.1,8080,10.0.2.2,8080 \
     --snat 10.0.0.2,10.0.2.1
# 另开终端，ns-server 侧抓包（可选：先起 iperf3 服务端，回程才有 SYN-ACK）
sudo ip netns exec ns-server tcpdump -ni veth-s tcp port 8080
# 客户端向“原目的地址”10.0.1.1:8080 发起连接（SYN 逐条被改写转发）
sudo ip netns exec ns-client iperf3 -c 10.0.1.1 -p 8080 -t 30
```

预期输出示例——loader 每 2 秒打印 8 项统计，客户端发流后 `nat` 递增（每条前向报文在两个挂载点各改写一次——DNAT、SNAT 各 +1，故数值按 2 递进，具体大小取决于 SYN 重传节奏）：

```text
已下发 NAT 规则：DNAT 1 条，SNAT 1 条。
TC 已挂载：veth-fw-c ingress <- nat_ingress（DNAT），veth-fw-s egress <- nat_egress（SNAT）。Ctrl-C 退出。
total=0 pass=0 drop_acl=0 drop_parse=0 ct_new=0 ct_established=0 ct_full=0 nat=0

total=0 pass=0 drop_acl=0 drop_parse=0 ct_new=0 ct_established=0 ct_full=0 nat=4

total=0 pass=0 drop_acl=0 drop_parse=0 ct_new=0 ct_established=0 ct_full=0 nat=6
```

tcpdump 在 ns-server 抓到的报文：前向报文目的为 10.0.2.2:8080（原为 10.0.1.1:8080，DNAT）、源为 10.0.2.1（原为客户端地址 10.0.0.2，SNAT）；服务端回程报文的目的地址正是 SNAT 地址 10.0.2.1：

```text
IP 10.0.2.1.41322 > 10.0.2.2.8080: Flags [S], seq 0, ...
IP 10.0.2.2.8080 > 10.0.2.1.41322: Flags [S.], seq 0, ...   # 回程：目的 = SNAT 地址
```

`total`/`pass` 等 7 项属于阶段 2/3/5 数据面的计数，本阶段恒为 0，loader 按契约全量打印以保持各阶段输出可比；`nat` 只计实际改写成功的报文——未命中、非 IPv4/TCP/UDP、残包一律静默放行、不计数。

### 检查点：自测三问

1. **为什么所有包指针读取必须赶在第一个改写辅助函数之前？**
   答案提示：改写类辅助函数可能触发缓冲区重排，验证器将其视为包指针失效点，之后再解引用旧指针即加载失败；先读后写、旧值新值进局部变量是标准写法。
2. **为什么 L4 校验和的地址增量要带 `BPF_F_PSEUDO_HDR`，IP 头校验和不用？**
   答案提示：TCP/UDP 校验和覆盖伪头，伪头里含 IP 地址——地址改动同时影响两处；`BPF_F_PSEUDO_HDR` 告知内核改动来自伪头。IP 头校验和只覆盖 IP 头本身。
3. **UDP 校验和为 0 的报文为什么要跳过校验和改写？**
   答案提示：0 是“发送方未计算校验和”的线缆约定（真实零以 0xFFFF 发送）；对 0 值调用辅助函数会把哨兵改成非 0，接收方误判为校验和错误而丢包。

**独立示例**：[`book/examples/chapter-17/04-nat-tc/`](../examples/chapter-17/04-nat-tc/)（`nat.bpf.c` + `nat.c` + `fw_common.h` + Makefile + README）。

## 七、阶段 5：完整系统与 Go 控制面

### 教师视角引入：合并与收口

阶段 1–4 留下三个工程缺口：各阶段程序各自加载、互不相识（规则/连接/统计各自一份）；loader 退出一切消失（无法“加载一次、长期生效、多处读写”）；没有统一命令入口。阶段 5 一次性收口：数据面合并为 `fw_xdp.bpf.c`（XDP 侧：解析 + 有状态 ACL + 连接跟踪）与 `fw_tc.bpf.c`（TC 侧：NAT 改写）两个对象，控制面合并为 `fwctl`——一个 Go 二进制、七个子命令。类比：把四个各自记账的窗口合并成一个带中央账本（pin 目录）的柜台。

**合并策略：快路径先于 ACL。** `fw_xdp` 相对阶段 2/3 的关键差异只有一个：conntrack 命中且已建立的报文走快路径、跳过 ACL 查找。这条顺序由两个理由共同决定，缺一不可：**快路径为什么可以跳过 ACL**——连接的合法性在其首包过 ACL 时已判定，后续报文不必逐包重查，N 条规则的策略在已建立连接上的成本为 0，这正是有状态防火墙相对无状态防火墙的性能来源；**未命中为什么也必须过 ACL**——新连接的 SYN 是连接表必然未命中的报文，若未命中路径跳过 ACL 直接登记放行，任何连接的首包都能绕过策略、快路径反而成了后门。因此 ACL 只对“ESTABLISHED 命中”这一种情况跳过；未命中先 ACL、放行后才登记；“命中但未建立”的慢路径也先 ACL，且**被 ACL 拒绝的报文不刷新表项**——被拒的连接不因后续报文保持活跃。

### 核心代码原理讲解（数据面）

**共享头并集与字段来源。** `book/examples/chapter-17/05-firewall/bpf/fw_common.h` 是阶段 2–4 的严格并集，头文件里每项定义的注释都标注了来源阶段；阶段 5 不发明新结构，字段契约一经定义不再改动：

| 定义 | 来源阶段 | 内容要点 |
|---|---|---|
| `MAX_RULES` / `MAX_LPM_KEYS` / `CT_MAX_ENTRIES` / `NAT_MAX_ENTRIES` | 2 / 2 / 3 / 4 | 四张策略与状态映射的容量上限（1024 / 4096 / 65536 / 4096） |
| `enum stat_idx`（8 项） | 2 起逐阶段追加 | total/pass/drop_acl/drop_parse（阶段 2）+ ct_new/ct_established/ct_full（阶段 3）+ nat（阶段 4） |
| `flow_key`/`fw_action`/`rule_value`/`lpm_key`/`lpm_value` | 2 | 五元组与两级 ACL 规则（见阶段 2） |
| `ct_state`/`ct_key`/`ct_value` | 3 | 连接跟踪状态机与表项（见阶段 3） |
| `nat_key`/`nat_value` | 4 | NAT 通配规则（见阶段 4） |

**XDP 数据面。** `fw_xdp.bpf.c` 中四张映射全部声明 `__uint(pinning, LIBBPF_PIN_BY_NAME)`，以 `acl_rules` 为例（完整定义见示例目录）：

```c
/* hash：key 全等匹配，O(1)；适合精确封禁，不适合网段/端口范围。
 * __uint(pinning, LIBBPF_PIN_BY_NAME)：加载时按 map 名 pin 到 user/maps.go 传入的
 * PinPath（/sys/fs/bpf/fw17），已 pin 的同名同定义 map 直接复用——这是「XDP 与 TC
 * 共享同一实例」与「独立子命令读写同一份规则」的基础 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key);
    __type(value, struct rule_value);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} acl_rules SEC(".maps");
```

`pinning` 属性的含义：加载时按映射名 pin 到 Go 控制面传入的 `PinPath`（`/sys/fs/bpf/fw17`）；已 pin 的同名同定义映射直接复用（复用时校验类型/key/value/容量兼容）。没有这个属性，pin 目录里根本不会出现映射，独立子命令也就无从读写——它是“XDP 与 TC 共享同一实例”与“attach 之后规则持续生效”的共同基础。

`parse_flow`（三参版本，带 `tcp_flags` 出参）、`acl_verdict`、`fill_ct_key` 与阶段 2/3 逐字一致，不再重复粘贴；命中但未建立路径的“刷新表项 + 状态迁移”与未命中路径的“SYN/UDP 首包登记”也与阶段 3 逐字相同（先 `CT_NEW→ESTABLISHED` 转移、再 RST/FIN 覆盖，`BPF_NOEXIST` 登记，`STAT_CT_FULL` 口径不变）。阶段 5 真正的新逻辑是两段，节选如下——先是命中路径：命中且已建立走**快路径**，命中但未建立走“先 ACL”的慢路径：

```c
    if (ct) {
        /* 快路径（阶段 5 关键差异）：连接已建立，跳过 ACL 查找直接放行。
         * 首包过 ACL 时已判定该连接的合法性，这里只刷新计量与状态。
         * RST/FIN 迁移必须保留（与阶段 3 一致），否则已建立连接永远无法收尾 */
        if (ct->state == CT_ESTABLISHED) {
            /* 多 CPU 同时更新同一表项存在写竞争（后写覆盖）；教学实现接受该近似，
               生产可改 per-CPU 计量或 spin_lock（与阶段 3 同一口径） */
            ct->last_seen_ns = now;
            ct->packets++;
            ct->bytes += pkt_len;
            if (flow.proto == IPPROTO_TCP) {
                if (flags & TCP_FLAG_RST)
                    ct->state = CT_CLOSED;
                else if (flags & TCP_FLAG_FIN)
                    ct->state = CT_FIN_WAIT;
            }
            bump(STAT_CT_ESTABLISHED);   /* 阶段 5 起本计数兼记快路径放行数 */
            return XDP_PASS;
        }

        /* 慢路径（命中但未建立）：先 ACL 判定（阶段 2 acl_verdict 原样）。
         * 被 ACL 拒绝的报文不刷新表项——被拒的连接不因后续报文保持活跃 */
        if (acl_verdict(&flow) == FW_DROP) {
            bump(STAT_DROP_ACL);
            return XDP_DROP;
        }
```

（节选到慢路径的 ACL 拒绝分支为止；ACL 放行后是阶段 3 的刷新与状态迁移——源码注释原文为“先 CT_NEW->ESTABLISHED 转移、再 RST/FIN 覆盖，顺序与阶段 3 一致”——随后 `bump(STAT_PASS); return XDP_PASS;` 收尾，完整函数见 `book/examples/chapter-17/05-firewall/bpf/fw_xdp.bpf.c`。）关键是未命中路径——同样先 ACL，登记必须发生在策略放行之后：

```c
    /* 未命中：同样先 ACL——首包若被策略拒绝，连接不登记、不建立快路径。
       登记动作必须发生在策略放行之后，否则快路径反而成了绕过策略的后门 */
    if (acl_verdict(&flow) == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
```

快慢路径合起来读：连接的合法性在首包过 ACL 时判定，ACL 只对“ESTABLISHED 命中”这一种情况跳过；被 ACL 拒绝的报文既不刷新表项也不登记——被拒的连接不因后续报文保持活跃、永远进不了快路径。快慢两条路径对同一 `ct_value` 的更新都是裸写——多 CPU 同时命中同一连接时存在写竞争（后写覆盖），与阶段 3 同一口径，教学实现接受该近似。阶段 5 起 `ct_established` 兼记快路径放行数。

**TC 数据面。** `fw_tc.bpf.c` 的 `do_nat` 与阶段 4 逐字一致（两段通配查找、方向过滤、先读后改、增量校验和、UDP 0 跳过，含 SNAT 通配源端口清零），差异只有两处：程序名改为 `fw_tc_ingress`/`fw_tc_egress`（SEC 仍为 `"tc"`，前者 DNAT、后者 SNAT）；`nat_map`/`stats` 声明 pinning 属性、与 XDP 侧共用同一实例。两个数据面对象的映射名完全对齐（`stats` 双方同名同定义）——“同名必为同一实例”是下文加载逻辑的前提。

### 核心代码原理讲解（Go 控制面）

**入口与子命令。** `user/main.go` 定义五个映射名常量——`acl_rules`/`acl_lpm`/`conntrack`/`nat_map`/`stats`，与 `SEC(".maps")` 定义逐字一致（同名才共享同一 pin 实例）——以及七个子命令的分发；attach 路径先执行 `rlimit.RemoveMemlock()`（5.11 之前的内核以 memlock 计量 BPF 内存，新内核调用无害）。CLI 总览（与 `fwctl` usage 同口径）：

| 子命令 | 作用 | 操作对象 |
|---|---|---|
| `attach` | 加载两个 .o（同名映射合并为同一实例）、挂 XDP 与 TC、pin 全部对象 | 5 张映射 + 3 程序 + 1 link |
| `detach` | 解除 XDP link 与 TC filter，清理 pin 目录 | 同上 |
| `rule add/del/list` | 精确五元组规则 | `acl_rules` |
| `cidr add` | 源地址网段规则 | `acl_lpm` |
| `nat add` | SNAT/DNAT 通配规则 | `nat_map` |
| `conntrack list` | dump 连接表 | `conntrack` |
| `stats` | 8 项统计逐 CPU 求和展示 | `stats` |

attach 之后的子命令只读写 pin 的映射，不需要重新加载程序——detach 才清理。

**spec 合并：同名映射必为同一实例。** `user/maps.go` 的核心（完整原文）：

```go
/* 两个 .o 的同名 map 必须是同一实例（XDP 与 TC 要看到同一份规则/统计）。
 * cilium/ebpf 不允许一个 Collection 里出现同名 map——把 TC spec 的程序与缺失的
 * map 并入 XDP spec 后一次性加载，同名 map 自然共享。 */
func mergeSpecs(xdp, tc *ebpf.CollectionSpec) *ebpf.CollectionSpec {
	for name, prog := range tc.Programs {
		xdp.Programs[name] = prog
	}
	for name, m := range tc.Maps {
		if _, ok := xdp.Maps[name]; !ok {
			xdp.Maps[name] = m
		}
	}
	return xdp
}
```

cilium/ebpf 不允许一个 Collection 里出现同名映射，`mergeSpecs` 把 TC spec 的程序与缺失映射并入 XDP spec 后**一次性加载**，五个映射天然共享；再经 `NewCollectionWithOptions(mergeSpecs(specX, specT), ebpf.CollectionOptions{Maps: ebpf.MapOptions{PinPath: pinDir}})` 按 `PinPath` pin 到 `/sys/fs/bpf/fw17`。反过来说明后果：若对两个 .o 各跑一次 `NewCollection`，会得到两套独立实例——“XDP 侧登记的连接 TC 侧看不到、规则改了一半只见一半”这类事故在运行期几乎无法从报文表现定位，只能在加载方式上杜绝。这是多程序防火墙最常见的部署事故，也是本节检查点要考的问题。

**加载后的挂载。** `loadAndAttach` 加载成功后做三件事：三个程序逐个 pin 成 `<名>.prog`（供 TC filter 以 pinned 方式引用）；`link.AttachXDP` 把 `fw_xdp` 挂到 `--xdp-iface` 并把 XDP link pin 成 `xdp_link`（接口名用标准库 `net.InterfaceByName` 解析——v0.16.0 的 link 包没有该函数，那是更高版本的 API，依赖版本钉死在 `go.mod` 的 v0.16.0）；TC 侧执行两条命令——`tc qdisc replace dev <接口> clsact` 与 `tc filter replace dev <接口> <方向> bpf direct-action pinned /sys/fs/bpf/fw17/fw_tc_ingress.prog`（`tcAttach` 源码逐项对应）。

TC 程序不能靠 skeleton 自动挂载（本示例也本就没有 skeleton，Go 侧直接读 .o），改为把 pin 好的程序路径交给 `tc filter replace ... pinned` 挂载——**兼容 5.10+**，而 cilium/ebpf 的 TCX 编程接口需要 6.6+，教学示例不采用；`direct-action` 让程序的返回值（`TC_ACT_OK` 等）直接作为 verdict。

**detach 的全量清理责任。** `user/maps.go` 的 `detach` 依次做三件事：`LoadPinnedLink` 加载 `xdp_link` 并 close——关闭 pinned XDP link 即解除挂载；TC filter **按方向** `tc filter del dev <接口> ingress/egress` 删除——源码注释解释了原因：内核拒绝指定 kind 的 flush，`tc filter del dev <dev> bpf` 实测报 `Cannot flush filters with protocol, handle or kind set.`（exit 2）且 filter 原样残留；代码显式检查退出码、不静默吞错——filter 持有程序引用，残留会让防火墙在“卸载成功”的假象下继续过滤/改写流量，比报错更危险。最后 `os.RemoveAll(pinDir)` 把映射/程序/link 一并清理（qdisc 为尽力清理），避免残留导致二次 attach 失败。

**pin 对象生命周期。** 三类对象：

| 对象 | 何时 pin | 谁引用它 | 何时消失 |
|---|---|---|---|
| 映射（5 张） | 加载时按名 pin（`MapOptions.PinPath`） | 数据面程序、`fwctl` 各子命令（`LoadPinnedMap`） | `detach` 删除 pin 目录，最后一个引用消失即释放——规则随之消失 |
| prog（3 个） | 加载后逐个 pin 成 `<名>.prog` | `tc filter replace ... pinned`（TC 两个）、XDP link | 同上；TC filter 与 link 各持有程序引用 |
| link（1 个） | XDP `bpf_link` pin 成 `xdp_link` | 内核中的 XDP 挂载关系本身 | `detach` 加载该 link 并关闭 fd，挂载解除 |

attach 进程**退出后一切照常**——pinned 对象的引用在 bpffs，不随进程 fd 关闭而消失，这是“加载一次、其余子命令独立操作”的基础；重复 attach 时映射按名复用（规则保留）、程序/link 重建并覆盖同名 pin；detach 必须显式执行。

**镜像结构体：C/Go 逐字节对齐。** `user/maps.go` 用定长数组/显式 `Pad` 字段定义 `FlowKey`/`RuleValue`/`LpmKey`/`CtKey`/`CtValue`/`NatKey`/`NatValue` 七个镜像类型，逐字节复刻 `fw_common.h` 的布局（`CtValue.State` 在偏移 24、`NatValue.IsSnat` 在偏移 6），IP/端口由 `rules.go` 的 `ipBytes`/`portBytes` 以网络字节序手工填充。skeleton 不导出 BPF 源码里的类型，而 `fw_common.h` 依赖内核 BTF 不能被用户态包含——镜像结构体是 C/Go 两侧的唯一桥梁，改任一侧必须同步另一侧。

**规则解析与原子更新。** `user/rules.go` 的 `ParseRule` 把 CLI 参数转成镜像结构（proto 只认 tcp/udp、action 只认 allow/drop）；`AddLpmRule` 用 `net.ParseCIDR` 拆前缀长度与网络地址写 `lpm_key`；`AddNat` 按 type 写通配 key——DNAT 写 `{src=0,sport=0}`、SNAT 写 `{dst=0,dport=0}`（`--to` 为裸 IP 时 `NewPort` 保持 0 即“仅改地址”），与阶段 4 数据面的两段查找契约严格对应。下发侧的“原子更新”指**不做清空重建**：`AddRule` 先 `Lookup` 做冲突检查，key 已存在即报“规则已存在（冲突），如需覆盖请先 rule del”（防静默改写策略），不存在才 `Put`——hash 映射的逐 key `Put` 本身是原子的，规则集变更过程中不存在“瞬时全放行/全阻断”的中间态。

**统计聚合。** `user/stats.go` 的 `statNames` 数组按统计下标顺序固定为 `total/pass/drop_acl/drop_parse/ct_new/ct_established/ct_full/nat`（源码注释声明“顺序即统计下标契约，不得改动”）。`PrintStats` 按下标 0–7 逐项调用 `SumPerCPU` 打印：per-CPU 聚合一次 `Lookup` 返回 `nr_cpus` 份、逐 CPU 求和——漏掉任何一个 CPU 的副本就是漏计数。`PrintConntrack` 用 v0.16.0 的 `MapIterator.Next(keyOut, valueOut)` 弱遍历连接表——遍历期间被 LRU 淘汰的表项自动跳过，循环结束后必须检查 `Err`（与阶段 3 的弱遍历语义同源）。

**规则变更对已建立连接的生效时机。** 快路径跳过 ACL，新下发的 drop 规则不影响已在连接表中 ESTABLISHED 的连接——经典有状态防火墙取舍；要立即生效需清理对应连接表项（本示例未提供 `conntrack del` 子命令，留作练习）。

### 学生动手：完整系统走一遍

```bash
cd book/examples/chapter-17/05-firewall
make    # 首次如提示缺依赖：cd user && go mod tidy 后重试
sudo ../scripts/setup-veth.sh

# 1) 加载并挂载（attach 完成后即可退出，规则持续生效）
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s

# 2) 下发规则（可重复执行，不需要重新 attach）
sudo ./fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
sudo ./fwctl cidr add --action drop --cidr 10.0.0.0/24
sudo ./fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
sudo ./fwctl nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1

# 3) 建流（另开终端）：客户端向“原目的地址”10.0.1.1:8080 发起连接
sudo ip netns exec ns-client iperf3 -c 10.0.1.1 -p 8080 -t 30

# 4) 观察连接表与统计；5) 清理
sudo ./fwctl conntrack list
sudo ./fwctl stats
sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
```

预期输出示例——attach：

```text
已挂载：XDP@veth-fw-c、TC ingress@veth-fw-c（DNAT）、TC egress@veth-fw-s（SNAT）。
map/prog/link 已 pin 到 /sys/fs/bpf/fw17；规则将持续生效，detach 时清理。
```

iperf3 建流后 `fwctl conntrack list`（XDP 只见入方向原始五元组，目的为 NAT 前的 10.0.1.1）：

```text
10.0.0.2:41322 -> 10.0.1.1:8080 proto=6 state=NEW
```

`fwctl stats`（8 项同一张 per-CPU 统计表：total/pass/ct_* 由 XDP 数据面维护，nat 由 TC 数据面维护）：

```text
total           18734
pass            18734
drop_acl        0
drop_parse      0
ct_new          1
ct_established  0
ct_full         0
nat             37468
```

三条读数口径：其一，`ct_established=0` 与 `state=NEW` 是本拓扑的预期现象——反向报文不经过挂载点（阶段 3 可见性口径在这里重演）；当反向报文可见时（例如把同一程序按同一 pin 再挂到 `veth-fw-s`，两个挂载点共享同一张 conntrack），连接表即列出 ESTABLISHED、后续报文走快路径：

```text
10.0.0.2:41322 -> 10.0.1.1:8080 proto=6 state=ESTABLISHED
```

其二，每条前向报文被 DNAT、SNAT 各改写一次，故 `nat` 按 2 递进。其三，对**解析成功**的报文恒等式 `pass + ct_established + drop_acl = total` 成立（本例 18734 + 0 + 0 = 18734）；残包放行计入 `pass` 但不计入 `total`（无独立计数，`drop_parse` 恒 0）。

### 检查点：自测三问

1. **为什么 XDP 与 TC 必须共享同一 map 实例？怎么保证？**
   答案提示：XDP 判定与 TC 改写必须看到同一份规则/连接/NAT 数据；同名同定义映射经 `mergeSpecs` 合并后一次加载，再按 `PinPath` pin。
2. **attach 进程退出后，哪些对象还在？谁负责清理？**
   答案提示：pin 到 bpffs 的 5 张映射、3 个程序、1 个 link 都在，挂载由 pinned link 与 TC filter 维持；清理责任在 `fwctl detach`。
3. **`rule add` 的“原子更新”指什么？为什么不清空重建？**
   答案提示：指逐 key 操作语义——先冲突检查再 `Put`，单 key 写入本身原子；清空重建会产生“规则表为空”的中间态，等于瞬时全放行或全阻断。

**独立示例**：[`book/examples/chapter-17/05-firewall/`](../examples/chapter-17/05-firewall/)（`bpf/` 数据面 + `user/` Go 控制面 + Makefile + README）。

## 八、压测与结果解读

压测脚本位于 `book/examples/chapter-17/scripts/`，均属**运行级待验证**——编写环境为非 root、无特权内核，脚本按“命令 + 指标口径”交付，数字应在读者自己的环境中测得，本节只给命令、口径与解读方法。

**吞吐：iperf3。** `book/examples/chapter-17/scripts/bench-iperf.sh` 完整原文（依赖拓扑与 iperf3；时长参数需为正整数）：

```bash
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
```

手动等价命令：

```bash
sudo ip netns exec ns-server iperf3 -s
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -P 8 -t 30
```

指标口径：看 `SUM` 行的 sender/receiver 吞吐（Gbits/sec）与重传数——重传异常增多通常是中间路径丢包，对照 `fwctl stats` 的 `drop_acl` 区分“策略丢”与“质量丢”；CPU 观测用 `mpstat -P ALL 1` 看软中断在各 CPU 的分布（XDP/TC 处理集中在收包 CPU）。预期输出示例（数字为教学示意、非实测）：

```text
[ ID] Interval           Transfer     Bitrate         Retr
[SUM]   0.00-30.00  sec  10.8 GBytes  3.10 Gbits/sec   35             sender
[SUM]   0.00-30.00  sec  10.7 GBytes  3.08 Gbits/sec                  receiver
```

**pps 与丢包率：pktgen。** `book/examples/chapter-17/scripts/bench-pktgen.sh`（必须 root；仅作用于 veth；`count` 换算依赖 `bc`，脚本会检查并提示安装）。其配置序列的手动等价命令：

```bash
echo "rem_device_all"            > /proc/net/pktgen/kpktgend_0
echo "add_device veth-fw-c@0"    > /proc/net/pktgen/kpktgend_0
echo "count 10000000"            > /proc/net/pktgen/veth-fw-c@0   # 包数（非微秒）：按 1 Mpps 假设折算秒数
echo "pkt_size 64"               > /proc/net/pktgen/veth-fw-c@0
echo "dst 10.0.0.2"              > /proc/net/pktgen/veth-fw-c@0
echo "dst_mac 00:00:00:00:00:02" > /proc/net/pktgen/veth-fw-c@0   # 按拓扑实际对端 MAC 修改
echo "start"                     > /proc/net/pktgen/pgctrl
grep -E "Result|pps" /proc/net/pktgen/veth-fw-c@0
```

**方向注意（本节最重要的一条口径，即脚本尾部注释原意）**：脚本默认对 `veth-fw-c` 打流，走的是 TX 路径（`veth-fw-c` 发送 → ns-client），**不经过** `veth-fw-c` 的 XDP（RX）与 `veth-fw-s` 的 TC egress——与挂载点不交叉，此时对照 `fwctl stats` 看不到任何计数变化。要对照 `drop_acl`，需在 ns-client 内对 `veth-c` 打流，使流量经 `veth-fw-c` 的 RX 路径进入防火墙，并相应调整脚本里的 `dst`/`dst_mac`。pktgen 的指标口径：`pps`（发送速率）、`acked`/`dropped`（丢包率 = dropped / 总数）；pktgen 依赖 `/proc/net/pktgen`，属运行级待验证（脚本以命令 + 预期输出交付）。

**四类常见失败模式定位表：**

| 失败模式 | 现象 | 定位方法 |
|---|---|---|
| XDP 未生效 | 挂载报错，或丢包策略看起来“没跑” | 确认挂载接口正确（`ip link show <接口>` 应显示 `xdp` 标记）；veth 上可能落到 generic 模式（执行位置在 skb 之后，语义一致但性能口径不同）；确认流量真的经过该接口的接收方向 |
| conntrack 满表 | 活跃连接数到达 `CT_MAX_ENTRIES` 后旧连接信息消失、新连接仍能建立 | 观察 dump 中旧表项**静默消失**、`ct_new` 持续增长——而不是 `ct_full` 递增（LRU 满表静默淘汰最久未用项、新连接登记仍成功；`ct_full` 只在并发竞争/分配失败时递增，见阶段 3） |
| NAT 校验和错 | 抓包能抓到改写后的报文，但对端静默丢弃、连接建立不起来 | `tcpdump -v` 显示 `bad cksum`；回查三点：L4 地址增量是否带 `BPF_F_PSEUDO_HDR`、UDP 校验和 0 是否跳过、IP 头校验和是否最后更新 |
| 规则不生效 | 下发成功但流量未被拦/未改写 | 按序排查：字节序（key 两侧是否都为网络字节序）、方向（conntrack/NAT key 记录的是 NAT 前原始五元组，规则方向要与报文方向一致）、key 是否逐字段一致（精确规则六个字段全等）、连接是否已 ESTABLISHED（快路径跳过 ACL，见阶段 5） |

结果解读的定性方法（不给假数据，只给思路）：XDP 丢弃在驱动层、TC 处理在 skb 之后，同一丢弃策略在两者上的 pps 差距反映“提前退出协议栈”的收益；规则数量对 hash 查找几乎无影响（O(1)）；conntrack 容量不足的表现是“旧连接静默消失”而非报错——容量规划按活跃连接数峰值留余量。

**教师视角**：压测放在阶段 5 之后，是因为压测对象是完整系统而非单阶段组件；先 iperf3（吞吐，人类可读）后 pktgen（pps，极限速率），先会测、再给失败模式定位表、最后给定性解读——顺序反过来，学生容易拿一组漂亮数字直接下结论。类比体检：单项数值脱离对照没有意义——吞吐要对照 `fwctl stats`、重传要对照 `drop_acl`，才分得清「策略丢」与「质量丢」。最大的误区是打流方向：脚本默认对 `veth-fw-c` 打流走 TX 路径，与挂载点不交叉，统计纹丝不动——学生常据此误判程序失效；第二条误区是把预期输出示例当成实测数字，本节所有数字都必须在读者环境重测。

**学生动手**：压测清单（全部运行级待验证，需 root）：

- [ ] 跑 `bench-iperf.sh`，记录 SUM 行 sender/receiver 吞吐与重传数
- [ ] `mpstat -P ALL 1` 观察软中断分布，定位收包 CPU
- [ ] 按手动等价命令跑 pktgen，实测「默认方向 stats 不动」的口径
- [ ] 对照四类失败模式定位表逐项自查

阶段检查点：

1. 吞吐读数配有 `fwctl stats` 对照，能区分「策略丢」与「质量丢」。
2. 能解释 pktgen 默认打流为何对照不到计数，并给出正确打流方向。

自测两问：

1. **conntrack 满表时 `ct_full` 为何不增长？**（提示：LRU 满表静默淘汰最久未用项、新登记仍成功；`ct_full` 只在并发竞争/分配失败时递增。）
2. **pktgen 默认打流方向上统计不变，是否说明防火墙失效？**（提示：默认对 `veth-fw-c` 打流走 TX 路径，不经过其 XDP（RX）与 `veth-fw-s` 的 TC egress；要对照 `drop_acl` 应在 ns-client 内对 `veth-c` 打流。）

## 可视化：数据路径决策树

阶段 5 XDP 数据面（`fw_xdp`）的完整决策路径，`STAT_*` 标注计数点（与代码逐行对应）：

```text
收到报文（veth-fw-c 接收方向）
  │
  v
parse_flow 成功？
  |-- 否（残包 / 非 IPv4/TCP/UDP）──► bump(STAT_PASS)；XDP_PASS
  |
  v 是
bump(STAT_TOTAL)
  │
  v
conntrack 查正向 key ──未命中──► 查反向 key（from_reply=1）
  │
  ├─ 命中且 state==CT_ESTABLISHED？
  │    └─ 是 ──► 快路径：刷新 last_seen/packets/bytes（多 CPU 后写覆盖为已知近似）
  │              TCP 带 RST→CT_CLOSED、带 FIN→CT_FIN_WAIT（迁移必须保留）
  │              bump(STAT_CT_ESTABLISHED)；XDP_PASS                ←跳过 ACL
  │
  ├─ 命中但未建立（慢路径 1）：
  │    acl_verdict（acl_rules 精确 ─► acl_lpm 最长前缀 ─► 默认 ALLOW）
  │    |-- FW_DROP ──► bump(STAT_DROP_ACL)；XDP_DROP（不刷新表项）
  │    └─ FW_ALLOW ──► 刷新表项；CT_NEW 且 from_reply→CT_ESTABLISHED（bump）
  │                    RST/FIN 覆盖；bump(STAT_PASS)；XDP_PASS
  │
  └─ 未命中（慢路径 2）：同样先 acl_verdict
       |-- FW_DROP ──► bump(STAT_DROP_ACL)；XDP_DROP（不登记、不建立快路径）
       └─ FW_ALLOW：
            TCP 非 SYN ──► bump(STAT_PASS)；XDP_PASS（跳过登记）
            TCP SYN / UDP 首包 ──► BPF_NOEXIST 登记 CT_NEW
                 |-- 成功 ──► bump(STAT_CT_NEW)
                 |-- 失败 ──► bump(STAT_CT_FULL)（并发竞争/分配失败；LRU 满表不在此列）
                 └────────► bump(STAT_PASS)；XDP_PASS
```

TC 数据面（`fw_tc_ingress` DNAT @ veth-fw-c ingress / `fw_tc_egress` SNAT @ veth-fw-s egress）：

```text
收到 skb（ingress@veth-fw-c 或 egress@veth-fw-s）
  │ 解析失败 / 非 IPv4/TCP/UDP ──► TC_ACT_OK（静默放行，不计数）
  v
nat_map 精确查找 ──未命中──► 通配二次查找（清零非 NAT 侧字段；SNAT 源端口一并清零）
  │ 命中且 is_snat==dir？
  |-- 否 ──► TC_ACT_OK
  v
读齐 old/new 与三个偏移（先读后改，指针只读到第一个改写调用之前）
  → store_bytes 改端口（new_port!=0 时）与地址
  → L4 校验和增量（地址带 BPF_F_PSEUDO_HDR；UDP 校验和 0 跳过；端口 2 字节增量）
  → L3 校验和增量（只含地址）
  → bump(STAT_NAT)；TC_ACT_OK
```

## 常见问题与排错

汇总各阶段示例 README 的失败点，按“环境 → 挂载 → 规则 → 状态 → NAT”归类：

| 类别 | 现象 | 原因与处理 |
|---|---|---|
| 环境 | 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载。用 `00-env/check-env.sh` 确认权限档位 |
| 环境 | 编译报 `vmlinux.h file not found` | 未先 `make`（首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 生成），或内核缺少 BTF |
| 环境 | 编译报 `use of undeclared identifier 'ETH_P_IP'` / `'TC_ACT_OK'` | uapi 宏、BTF 不导出；示例源码顶部已补定义，删掉该行即复现 |
| 环境 | `go build` 报缺依赖或校验和不符 | `cd user && go mod tidy`（依赖已提交 `go.sum`，正常联网环境无需手工处理） |
| 挂载 | `attach_xdp 失败`，或挂载后远程会话失联 | 误挂物理网卡/SSH 接口：XDP 在驱动层直接丢包。只挂实验拓扑的 veth 接口 |
| 挂载 | `创建 clsact 失败` 或 TC attach 失败 | 无 `CAP_NET_ADMIN`（TC 挂载走 netlink），或误挂物理网卡 |
| 挂载 | 重跑时报 `attach ... 失败: File exists` | 上次被 `kill -9` 未走 detach，残留 filter 占用同 handle/priority：按方向 `tc filter del` 后重跑（正常 Ctrl-C 退出不会发生） |
| 挂载 | `attach 失败: ... No such device` / 接口不存在 | 拓扑未建立：先 `sudo ../scripts/setup-veth.sh`；接口名以 `ip link` 实际输出为准 |
| 挂载 | 子命令报 `未找到 /sys/fs/bpf/fw17：请先执行 attach 创建并 pin 映射` | 先于 attach 执行了 rule/nat/conntrack/stats：先 attach（或确认 pin 目录未被误删） |
| 规则 | 下发 `--rule` 后 `drop_acl` 不增长 | 精确匹配要求六字段全等，客户端源端口随机时不命中：`ss -tn` 查实际五元组按实下发，或改用 `--cidr` |
| 规则 | `--cidr drop,10.0.0.0/256` 丢弃了所有流量 | 前缀长度超界被截断成 `/0`（`atoi` 教学从简，匹配全部源地址）：输入 0–32 的合法前缀长度 |
| 规则 | 报 `规则下发失败（规则数超上限或格式错误）` | 映射容量用尽（1024 / 4096）；阶段 2 未提供删除接口，重启 loader 恢复空表 |
| 规则 | `fwctl rule add` 报 `规则已存在（冲突）` | 同五元组规则已下发：如需覆盖先 `rule del`（冲突检查防止静默改写策略） |
| 规则 | XDP 与 TC 的统计/规则对不上 | 两个 .o 被分开独立加载产生两套映射实例——必须经 `fwctl attach` 的 `mergeSpecs` 共享路径加载（见阶段 5） |
| 状态 | dump 一直为空（有流量但无表项） | TCP 只登记以 SYN 开始的连接：程序挂载晚于连接建立，中途报文按设计跳过登记；重跑 iperf3 重新建流 |
| 状态 | `state` 一直停在 1、`ct_established` 恒为 0 | 反向报文未经过挂载点：XDP 只作用于接收方向（阶段 3/5 的可见性口径），非程序缺陷 |
| 状态 | 表项在 dump 里突然消失（`ct_new` 仍增长、`ct_full` 恒 0） | LRU 表满的正常行为：静默淘汰最久未用项、新连接登记仍成功；调小 `CT_MAX_ENTRIES` 可观察 |
| 状态 | `idle` 数值巨大或为负 | 时间基准混用：用户态必须用 `CLOCK_MONOTONIC` 对齐 `bpf_ktime_get_ns`，不能用墙钟 |
| NAT | tcpdump 看不到改写报文 | 按序排查：loader 是否显示“已下发 NAT 规则”；流量是否真的经过 fw（拓扑/默认路由）；协议是否匹配（`--snat` 复用 `--dnat` 的 proto） |
| NAT | 客户端握手失败、回包未被还原 | 预期——阶段 4/5 的挂载组合不含回程 un-NAT（挂载点可见性口径），补全方向见阶段 4 讲解 |
| NAT | 服务端丢包、tcpdump 提示校验和错误 | 校验和增量被删改：地址增量必须带 `BPF_F_PSEUDO_HDR`，UDP 校验和 0 必须跳过改写（见阶段 4） |

## 课后扩展任务

本章配套四项课后扩展任务，覆盖数据面与控制面两个扩展维度，按难度由浅入深编号，每项对应 [`book/examples/chapter-17/exercises/`](../examples/chapter-17/exercises/) 下的一个独立目录（任务编号即目录编号，总览见其 [README](../examples/chapter-17/exercises/README.md)）：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 数据面 | IPv6 ACL 支持：解析分支 + map key 扩展 | [`book/examples/chapter-17/exercises/01-ipv6-acl/`](../examples/chapter-17/exercises/01-ipv6-acl/) |
| 2 | 控制面 | 规则持久化与重启恢复：保存/加载/原子替换 | [`book/examples/chapter-17/exercises/02-rule-persistence/`](../examples/chapter-17/exercises/02-rule-persistence/) |
| 3 | 控制面 | Prometheus 指标导出：per-CPU 聚合 + /metrics | [`book/examples/chapter-17/exercises/03-prometheus-export/`](../examples/chapter-17/exercises/03-prometheus-export/) |
| 4 | 数据面 | 按端口限速：令牌桶 map + 超额动作 | [`book/examples/chapter-17/exercises/04-port-rate-limit/`](../examples/chapter-17/exercises/04-port-rate-limit/) |

**防剧透约定**：每项课后扩展任务的 README 均按「任务在前、参考实现思路独立成节放在后部」的结构组织——先只读任务描述与提示、自行实现，再对照参考实现；各练习复用本章主示例的构建体系（Makefile 同构）。验证状态：四项参考实现已按 1.5 节三档约定逐项复标为「编译级已验证（本机，`verify-build.sh` 全量通过）」；运行级待验证——编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行，各 README「预期输出」均为教学示例（练习 3 含 Go 依赖，离线环境另按「依赖待拉取，编译级待验证」标注）。

## 总结

一个可信的 eBPF 防火墙不是单个 XDP 函数，而是解析、判定、状态、改写、控制面与观测共同组成的系统。本章用五个阶段把它拆开讲透：阶段 1 建立「加载 → 验证 → 挂载 → 触发」闭环与“边界检查是硬约束”的肌肉记忆；阶段 2 给出逐层定界的解析范式、网络字节序契约与两级 ACL 的查找结构；阶段 3 用正反 key 与最小状态机把报文组织成连接，讲清 LRU 容量语义（满表静默淘汰、`ct_full` 与满表无关）与“挂载点决定数据面可见性”；阶段 4 在 TC 上下文完成 NAT 改写与增量校验和，建立“先读后改”的指针纪律与伪头校验和直觉；阶段 5 把三者合并为快路径先于 ACL 的有状态防火墙，用 spec 合并、pin 生命周期与逐 key 原子下发补齐控制面工程。

同样重要的是如实记录的边界：单挂载点只见接收方向（`ct_established` 与快路径在本拓扑不可复现、回程 un-NAT 缺失）、IP 分片与畸形头长只做了教学级处理、conntrack 计量接受多 CPU 写竞争近似、规则解析存在 `atoi` 级别的从简——这些不是缺陷清单，而是把教学实现推向生产时第一批要处理的工程问题；生产部署与安全加固的系统话题在第 24 章展开。

## 本章参考来源

- Linux kernel documentation：BPF 验证器（`Documentation/bpf/verifier.rst`）、BPF 映射（`Documentation/bpf/maps.rst`：hash、per-CPU 数组、LRU hash、LPM trie 的语义与 map pinning）、XDP 与 TC 的内核网络文档。
- `include/uapi/linux/bpf.h`：`enum xdp_action`、`BPF_MAP_TYPE_*`、`BPF_F_NO_PREALLOC`、`bpf_skb_store_bytes`/`bpf_l3_csum_replace`/`bpf_l4_csum_replace` 与 `BPF_F_PSEUDO_HDR`/`BPF_F_MARK_MANGLED_0`。
- `include/uapi/linux/pkt_cls.h`：`TC_ACT_OK` 等 TC 动作返回值。
- RFC 1624：Computation of the Internet Checksum via Incremental Update（增量校验和的算术口径）。
- libbpf 文档：`bpf_program__attach_xdp`、TC 挂载 API（`bpf_tc_hook_create`/`bpf_tc_attach`/`bpf_tc_detach`）、`LIBBPF_PIN_BY_NAME` 与对象 pinning 模型、per-CPU 映射的用户态语义、`bpf_map_get_next_key` 遍历组合。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式（阶段 1–4 同源）。
- cilium/ebpf 文档（v0.16.0）：`MapOptions.PinPath` 与同名映射复用语义、`link.AttachXDP`、`MapIterator`、`rlimit.RemoveMemlock`。
- `tc(8)`/`tc-bpf(8)`：clsact qdisc、`tc filter replace ... pinned` 的生命周期与 `direct-action` 语义。
- Cilium documentation：eBPF networking 与 network policy（生产级有状态防火墙的工程参照）。
- LWN.net：BPF 与 XDP 系列文章（验证器与 XDP 机制背景）。
- ebpf.top：BPF 入门系列（XDP 动作返回值与挂载点）。
- 本仓库 `book/examples/chapter-09/09-netfilter-go/`：Go 控制面 + pinning 的工程骨架先例（依赖版本同源）。

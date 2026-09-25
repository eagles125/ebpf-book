# 05-firewall：完整系统与 Go 控制面（XDP 快路径 ACL + conntrack、TC NAT、fwctl）

第 17 章《实战：构建高性能 eBPF 防火墙》渐进示例的**阶段 5（最终阶段）**：把阶段 2–4 的解析/ACL、连接跟踪、NAT 合并为一套完整的数据面（`fw_xdp.bpf.c` 快路径先于 ACL 的有状态防火墙 + `fw_tc.bpf.c` 双向 NAT 改写），配上 Go 控制面 `fwctl`（加载/挂载、规则下发、连接表与统计读取），五个 map 通过 pin 共享为全系统唯一实例。这是阶段 1–4 所有教学点的汇合处：数据面/控制面分离、同名 map 同实例、pin 对象生命周期。

## 演示目标

- 理解**快路径先于 ACL** 的有状态防火墙模型：连接的合法性在其首包过 ACL 时已判定，已建立（ESTABLISHED）连接的后续报文直接刷新连接表并放行、跳过 ACL 查找——策略条数再多，已建立连接上的成本也是 0，这是有状态防火墙相对无状态逐包过滤的性能来源。
- 掌握**同名 map 必为同一实例**的部署语义：XDP 与 TC 两个 .o 的 `acl_rules`/`acl_lpm`/`conntrack`/`nat_map`/`stats` 在 Go 控制面里合并为一次加载（`mergeSpecs`）并 pin 到 `/sys/fs/bpf/fw17`；若两个 .o 各自独立加载出两套 map，就会出现「XDP 放行的连接 TC 不认识、规则改一半只见一半」的规则不一致事故。
- 学会 Go 控制面的完整工程形态：`flag.NewFlagSet` 子命令分发、`MapOptions.PinPath` 按名 pin/复用 map、`tc filter replace ... pinned` 挂 TC 程序（兼容 5.10+，TCX 需 6.6+ 不采用）、逐 key 原子下发规则（不清空重建）、per-CPU 统计逐 CPU 求和。
- 认识 pin 对象的三类生命周期（map/prog/link）与 detach 的全量清理责任（见下文「pin 对象生命周期」）。

## 与阶段 1–4 的关系

| 来源 | 复用内容 | 阶段 5 的变化 |
|---|---|---|
| 阶段 1（01-xdp-drop） | XDP 挂载与最简 DROP 语义 | 并入完整流程，不再是独立示例 |
| 阶段 2（02-parse-acl） | `parse_flow`/`acl_verdict`/`acl_rules`/`acl_lpm` | ACL 只在「未命中连接表」与「命中但未建立」两条慢路径上执行 |
| 阶段 3（03-conntrack） | 三参 `parse_flow`、正反 key、状态机、LRU `conntrack` | 从「只观测」变为策略的一部分：命中且 ESTABLISHED 走快路径（跳过 ACL） |
| 阶段 4（04-nat-tc） | `do_nat`/`nat_map`/增量校验和（原样迁入，含 SNAT 通配源端口清零） | 仅程序名改为 `fw_tc_ingress`/`fw_tc_egress`，`stats` 与 XDP 侧共用同一实例 |
| 阶段 5 新增 | `fw_common.h` 为阶段 2–4 的严格并集；Go 控制面 `fwctl`；共享 map pin | —— |

两点**如实说明**（与阶段 3/4 相同的可见性口径）：(1) XDP 挂 `veth-fw-c` 只见接收方向（客户端到防火墙），服务端回包从 `veth-fw-s` 进入、再从 `veth-fw-c` 发出，都不经过 XDP——连接表的 `CT_NEW -> CT_ESTABLISHED` 迁移在本拓扑的单挂载下不触发，快路径要等反向报文可见后才生效（见「预期输出」）；(2) 本示例的 NAT 挂载组合与阶段 4 相同（DNAT@veth-fw-c ingress、SNAT@veth-fw-s egress），仍不含回程 un-NAT，回程报文原样到达客户端、握手不能完成；`do_nat` 的通配与方向过滤已就绪，把同一张 `nat_map` 按镜像规则补挂回程两个方向即可补全（数据面无需改动，留作正文/练习）。

## 文件说明

| 文件 | 说明 |
|---|---|
| `bpf/fw_common.h` | 阶段 2–4 共享头的严格并集：常量区（`MAX_RULES`/`MAX_LPM_KEYS`/`CT_MAX_ENTRIES`/`NAT_MAX_ENTRIES`）、`enum stat_idx` 全 8 项（逐项标注来源阶段）、flow/rule/lpm/ct/nat 全部结构（字段契约一经定义不再改动） |
| `bpf/fw_xdp.bpf.c` | XDP 数据面：解析 -> conntrack 正反查找 -> 命中且 ESTABLISHED 走快路径（跳过 ACL）-> 慢路径先 ACL 后状态机/登记；四个 map 定义（均 `LIBBPF_PIN_BY_NAME`） |
| `bpf/fw_tc.bpf.c` | TC 数据面：阶段 4 的 `do_nat` 原样迁入（两段通配查找、增量校验和、UDP 0 校验和跳过），SEC 改 `fw_tc_ingress`/`fw_tc_egress`；`nat_map`/`stats` 与 XDP 侧共享 |
| `user/main.go` | `fwctl` 入口：七个子命令分发（attach/detach/rule/cidr/nat/conntrack/stats），中文 usage |
| `user/maps.go` | 与 `fw_common.h` 逐字节对应的 Go 镜像结构体、spec 合并（`mergeSpecs`）、加载/pin/attach/detach |
| `user/rules.go` | 五元组/LPM/NAT 规则的解析、冲突检查与逐 key 原子下发；共享格式化小工具 |
| `user/stats.go` | `statNames`（C1 契约顺序）、per-CPU 聚合、统计与连接表展示 |
| `user/go.mod` | Go 模块定义（`cilium/ebpf` 固定 `v0.16.0`，与第 9 章 09-netfilter-go 一致） |
| `user/go.sum` | 依赖校验和（`go mod tidy` 生成，随仓库提交） |
| `Makefile` | BPF 两个 .o + `go build` 联合构建（无 skeleton：Go 侧用 cilium/ebpf 直接读 .o） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（XDP bpf_link、LRU hash、TC clsact、`bpf_skb_store_bytes` 等在 5.10 上均已可用）。
- `clang`、`bpftool`、`make`、Go 1.22+（本仓库编写环境为 Go 1.27.1）；Go 依赖已提交 `go.sum`，首次构建如提示缺失执行 `go mod tidy`。
- `tc`（iproute2，运行期挂 TC filter 用）；运行需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`。
- 实验拓扑由 `../scripts/setup-veth.sh` 创建（见 C5）：XDP 与 TC ingress 挂 `veth-fw-c`，TC egress 挂 `veth-fw-s`；建流工具用 `iperf3`（编译不依赖）。
- 安全警告：本示例会**丢弃报文并真实改写 NAT 字段**，且 TC egress 挂载影响该接口发出的所有流量——只应挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口。

## 编译与运行

```bash
cd book/examples/chapter-17/05-firewall
make    # 首次如提示缺依赖：cd user && go mod tidy 后重试

# 需要先建立实验拓扑（见 ../scripts/setup-veth.sh）：
sudo ../scripts/setup-veth.sh

# 1) 加载并挂载（map/prog/link pin 到 /sys/fs/bpf/fw17，attach 完成后即可退出）
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s

# 2) 下发规则（都可重复执行；不需要重新 attach）
sudo ./fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
sudo ./fwctl rule list
sudo ./fwctl cidr add --action drop --cidr 10.0.0.0/24
sudo ./fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
sudo ./fwctl nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1

# 3) 建流（另开终端）：客户端向「原目的地址」10.0.1.1:8080 发起连接
sudo ip netns exec ns-client iperf3 -c 10.0.1.1 -p 8080 -t 30

# 4) 观察连接表与统计（attach 之后的任意时刻、任意进程）
sudo ./fwctl conntrack list
sudo ./fwctl stats

# 5) 清理（解除挂载并删除 pin 目录，规则/连接/NAT 表随之消失）
sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
```

`nat add` 的 key 契约与阶段 4 一致：DNAT 写源侧通配 key（`src=0,sport=0`），SNAT 写目的侧通配 key（`dst=0,dport=0`，`--to` 为裸 IP 时 `new_port=0` 即「仅改地址」）。

## pin 对象生命周期

| 对象 | 何时 pin | 谁引用它 | 何时消失 |
|---|---|---|---|
| map（5 个） | `NewCollectionWithOptions` 加载时按名 pin（`MapOptions.PinPath`） | 数据面程序、`fwctl` 各子命令（`LoadPinnedMap`） | `detach` 删除 pin 目录，最后一个引用消失即释放——规则随之消失 |
| prog（3 个） | 加载后逐个 pin 成 `<名>.prog` | `tc filter replace ... pinned`（TC 两个）、XDP link（`fw_xdp`） | 同上；TC filter 与 link 各持有程序引用 |
| link（1 个） | XDP `bpf_link` pin 成 `xdp_link` | 内核中的 XDP 挂载关系本身 | `detach` 加载该 link 并关闭 fd、随后删除 pin，挂载解除 |

三点工程语义：(1) attach 进程**退出后一切照常**——pinned 对象的引用在 bpffs，不随进程 fd 关闭而消失，这正是「加载一次、其余子命令独立操作」的基础；(2) 重复 attach 时 map 按 `PinByName` 语义**复用**已有 pin（规则保留），程序/link 重建并覆盖同名 pin（等效搬家，不会累积）；(3) detach 必须显式执行——TC filter 与 pinned 对象不随进程退出自动卸载，被 `kill -9` 后的残留需要按「常见失败点」手工清理。

## 预期输出

（预期输出示例 / 运行级待验证）

`attach` 成功：

```text
已挂载：XDP@veth-fw-c、TC ingress@veth-fw-c（DNAT）、TC egress@veth-fw-s（SNAT）。
map/prog/link 已 pin 到 /sys/fs/bpf/fw17；规则将持续生效，detach 时清理。
```

iperf3 建流后，`fwctl conntrack list`（XDP 只见入方向原始五元组，目的为 NAT 前的 10.0.1.1）：

```text
10.0.0.2:41322 -> 10.0.1.1:8080 proto=6 state=NEW
```

`fwctl stats`（8 项同一张 per-CPU stats 表：total/pass/ct_* 由 XDP 数据面维护，nat 由 TC 数据面维护；每条前向报文被 DNAT、SNAT 各改写一次，故 nat 按 2 递进）：

```text
total           18734
pass            18734
drop_acl        0
drop_parse      0
ct_new          1
ct_established  0
ct_full         0
nat             18732
```

- **`ct_established=0` 与 `state=NEW` 是本拓扑的预期现象**（如实说明）：`CT_NEW -> CT_ESTABLISHED` 由反向报文触发，而 XDP 单挂载只见接收方向（见「与阶段 1–4 的关系」），服务端回包不经过挂载点，连接停在 `state=NEW`、每个报文都走「命中但未建立」的慢路径（ACL 照查、表项照常刷新）。当反向报文可见时（例如把同一程序按同一 pin 再挂到 `veth-fw-s`——两个挂载点共享同一张 pin 的 conntrack；本示例 CLI 固定为单挂载，双挂载变体留作正文/练习），连接表即列出 ESTABLISHED 表项、后续报文走快路径：

```text
10.0.0.2:41322 -> 10.0.1.1:8080 proto=6 state=ESTABLISHED
```

- 计数口径（阶段 5 起）：`total` = 成功解析出五元组的报文数；`pass` = 慢路径放行（ACL 放行、登记放行、残包放行各 +1）；`ct_established` = 快路径放行数（含连接转入 ESTABLISHED 的那一次）；`pass + ct_established + drop_acl = total`（残包放行在 total 之外）；`drop_parse` 为保留计数恒 0；`nat` 只计实际改写成功的报文（与阶段 4 口径一致）。

## 代码讲解要点

- **快路径先于 ACL 的顺序与理由**：`fw_xdp` 在 conntrack 正反查找命中且 `state==CT_ESTABLISHED` 时直接刷新表项、`bump(STAT_CT_ESTABLISHED)`、返回 `XDP_PASS`——不再调用 `acl_verdict`。理由：连接的合法性在首包过 ACL 时已判定，有状态防火墙的性能优势正来自「已建立连接零策略成本」。快路径里保留 RST/FIN 状态迁移（与阶段 3 一致），否则已建立连接永远无法进入 CLOSED/FIN_WAIT。
- **未命中也必须先过 ACL**：新连接的 SYN 是连接表必然未命中的报文，若未命中路径跳过 ACL 直接登记放行，任何连接的首包都能绕过策略、快路径反而成了后门。因此 ACL 只对「ESTABLISHED 命中」这一种情况跳过；被 ACL 拒绝的报文也不刷新连接表项（被拒的连接不因后续报文保持活跃）。
- **同名 map 必为同一实例（C3）**：cilium/ebpf 不允许一个 Collection 里出现同名 map，`mergeSpecs` 把 TC spec 的程序与缺失 map 并入 XDP spec 后一次加载，五个 map 天然共享，再经 `MapOptions.PinPath` pin 到 `/sys/fs/bpf/fw17`——XDP 与 TC 看到同一份规则/连接/NAT/统计。若图省事对两个 .o 各跑一次 `NewCollection`，会得到两套独立实例：「XDP 侧登记的连接 TC 侧看不到、规则只改了一半」这类事故在运行期几乎无法从报文表现定位，只能在加载方式上杜绝。
- **map 定义里的 `__uint(pinning, LIBBPF_PIN_BY_NAME)`**：`MapOptions.PinPath` 只对声明了该属性的 map 生效（按名 pin/复用，复用时校验类型/key/value/max_entries 兼容）；没有它，pin 目录里根本不会出现 map，独立子命令也就无从读写。
- **规则的原子更新**：hash map 的逐 key `Put` 本身是原子的，`fwctl` 的「原子更新」指**不做清空重建**——`rule add` 先 `Lookup` 冲突检查（已存在即拒绝，如需覆盖先 `rule del`）再 `Put`，规则集变更过程中不存在「瞬时全放行/全阻断」的中间态。
- **per-CPU 统计聚合**：`stats` 是 per-CPU 数组，每 CPU 一份独立 `__u64`、数据面无锁自增；用户态 `Lookup` 一次返回 `nr_cpus` 份，`SumPerCPU` 逐 CPU 求和——漏掉任何一个 CPU 的副本就是漏计数。展示顺序 `statNames` 即 C1 契约的枚举顺序，不得改动。
- **Go 镜像结构体与 C 布局逐字节对齐**：`FlowKey` 等结构用定长数组/显式 `Pad` 字段复刻 `fw_common.h` 的布局（如 `CtValue` 的 `State` 必须落在偏移 24、`NatValue.IsSnat` 落在偏移 6），IP/端口以网络字节序手工填充（`ipBytes`/`portBytes`），原样存原样查。skeleton 不导出 BPF 源码里的类型，而 `fw_common.h` 依赖内核 BTF 不能被用户态包含——镜像结构体是唯一桥梁，改任一侧必须同步另一侧。
- **固定版本 v0.16.0 的 API 口径**：依赖版本钉死在 `go.mod`（与第 9 章一致），示例按该版本书写——`MapIterator.Next(keyOut, valueOut)` 一次取出一对并返回 bool（更高版本的链式/Decode 风格在这里编译不过）、接口名解析用标准库 `net.InterfaceByName`（`link.InterfaceByName` 是更高版本的 API）。升级依赖前先核对这两处。
- **TC 挂载用 `tc filter replace ... pinned`**：TC 程序不能靠 skeleton `__attach()` 自动挂载（本示例也本就没有 skeleton），Go 侧通过 pin 好的 `<名>.prog` 路径交给 tc 命令挂载，兼容 5.10+；cilium/ebpf 的 TCX 编程接口需要 6.6+，教学示例不采用。`direct-action` 让程序的返回值（`TC_ACT_OK` 等）直接作为 verdict。
- **`do_nat` 与阶段 4 逐字一致**：两段通配查找（精确未命中后清零侧字段二次查找）、`is_snat == dir` 方向过滤、先读后改（所有包指针读取先于第一个改写辅助函数）、增量校验和（地址项带 `BPF_F_PSEUDO_HDR`，UDP 校验和 0 跳过）、SNAT 通配源端口清零（出方向源端口是内核临时值，按 (源 IP, 协议) 命中）。逐条机制讲解见阶段 4 README，此处不再重复。
- **规则变更对已建立连接的生效时机**：快路径跳过 ACL，因此新下发的 drop 规则不影响已在 conntrack 里 ESTABLISHED 的连接（其表项也不被 LRU 之外的方式清理）——这是经典有状态防火墙取舍；要立即生效需清理对应连接表项（本示例未提供 `conntrack del` 子命令，留作练习）。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `加载 BPF（需 root/CAP_BPF）...` 或子命令报 `打开 ...: permission denied` | 预期行为：非 root 且 `unprivileged_bpf_disabled=2` 时不能加载/打开。先用 `../00-env/check-env.sh` 确认权限档位 |
| 子命令报 `未找到 /sys/fs/bpf/fw17：请先执行 attach 创建并 pin 映射` | 先于 attach 执行了 rule/nat/conntrack/stats：先 attach（或确认没有被误删的 pin 目录） |
| `attach 失败: tc attach: ... File exists` | 上次未 detach（如被 `kill -9`），残留 filter 占位：`sudo tc filter del dev veth-fw-c ingress`、`sudo tc filter del dev veth-fw-s egress`、必要时 `sudo tc qdisc del dev <接口> clsact` 后重跑 |
| `attach 失败: ... No such device` / 接口不存在 | 拓扑未建立：先 `sudo ../scripts/setup-veth.sh`；接口名以 `ip link` 实际输出为准 |
| 误挂物理网卡后 SSH 断连 | TC egress/ingress 会过滤真实流量。务必只挂实验拓扑的 veth 接口；恢复需在能登录的控制台上 detach |
| `rule add` 报 `规则已存在（冲突）` | 同五元组规则已下发：如需覆盖先 `rule del`（冲突检查是防止静默改写策略） |
| 规则已下发但流量未被拦/未改写 | 按序排查：attach 输出的挂载方向是否正确（XDP 只见接收方向）；规则五元组方向与报文方向是否一致（conntrack/NAT key 记录的是 NAT 前的原始五元组）；连接是否已 ESTABLISHED（快路径跳过 ACL，见「代码讲解要点」） |
| XDP 与 TC 的统计/规则对不上 | 两个 .o 被分开独立加载、产生了两套 map 实例——必须经 `fwctl attach` 的 `mergeSpecs` 共享路径加载（见「代码讲解要点」同名 map 条目） |
| 客户端握手失败、回包未被还原 | 预期——本示例挂载组合不含回程 un-NAT（与阶段 4 相同，见「与阶段 1–4 的关系」第 2 条） |
| 编译报 `vmlinux.h file not found` / `use of undeclared identifier 'ETH_P_IP'` 或 `'TC_ACT_OK'` | 前者未先 `make` 或内核缺 BTF；后者是 uapi 宏、`vmlinux.h` 不导出，示例源码顶部已补定义，删掉即复现 |
| `go build` 报缺依赖 / 校验和不符 | 执行 `cd user && go mod tidy`（依赖已提交 `go.sum`，正常联网环境无需手工处理） |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3 / Go 1.27.1）`make` 通过（两个 BPF 对象 + `go build` + `go vet`）；另以静态断言核对了 C/Go 两侧镜像结构的逐字节布局、以 spec 解析核对了两个 .o 的程序/map 名与 `Pinning=PinByName` 属性及 `mergeSpecs` 合并结果。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在 clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出」均为教学示例。

## 参考来源

- cilium/ebpf 文档与源码（v0.16.0）：`MapOptions.PinPath` 与 `LIBBPF_PIN_BY_NAME` 语义（同名 map 复用时的兼容性校验）、`link.AttachXDP`（bpf_link 路径，5.9+）、`MapIterator.Next` 的弱遍历语义、`rlimit.RemoveMemlock`（5.11 前内核的 memlock）。
- `tc(8)`/`tc-bpf(8)`：clsact qdisc、`tc filter replace ... pinned` 的生命周期与 `direct-action` 语义。
- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，LRU hash 的容量与淘汰语义、per-CPU 数组映射、map pinning）。
- `include/uapi/linux/bpf.h`、`include/uapi/linux/pkt_cls.h`：`BPF_NOEXIST`、`bpf_skb_store_bytes`/`bpf_l3_csum_replace`/`bpf_l4_csum_replace` 与 `BPF_F_PSEUDO_HDR`、`TC_ACT_OK`。
- RFC 1624：增量校验和的算术（与阶段 4 同源）。
- libbpf 文档：`LIBBPF_PIN_BY_NAME` 与对象 pinning 模型（BPF 源侧属性与加载器行为）。
- 本仓库 `book/examples/chapter-09/09-netfilter-go/`：Go 控制面 + pinning 的工程骨架先例（依赖版本同源）。
- iperf3 用户文档：建流方法学（与阶段 2–4 的压测口径一致）。

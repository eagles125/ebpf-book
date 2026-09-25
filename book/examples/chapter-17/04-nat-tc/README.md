# 04-nat-tc：TC 路径 SNAT/DNAT 与增量校验和

第 17 章《实战：构建高性能 eBPF 防火墙》渐进示例的阶段 4：数据面从 XDP 走到 TC（clsact）——在「进入防火墙的第一跳 ingress」做目的地址改写（DNAT）、在「离开防火墙的最后一跳 egress」做源地址改写（SNAT），配合增量校验和更新。核心是统一的改写函数 `do_nat(skb, dir)`：两段通配查找定位规则、先改字段后修校验和、全部包指针读取赶在第一个改写辅助函数之前完成。

## 演示目标

- 理解 TC 挂载模型：clsact qdisc + ingress/egress 两个方向，libbpf 侧用 `bpf_tc_hook_create` + `bpf_tc_attach` 走 netlink 挂载（TC 程序不能靠 skeleton `__attach` 自动挂载，与 XDP 的 `attach_xdp` 生命周期也不同）。
- 掌握 NAT 改写的失败模式：`bpf_skb_store_bytes`/`bpf_l4_csum_replace` 一旦调用，验证器（verifier）即判定包指针失效——所有 `ip->xxx` 读取、旧值/新值、偏移量必须先算好；改字段与修校验和之间顺序无关正确性，因为校验和辅助函数的入参自带 old/new 增量。
- 掌握增量校验和的数学直觉：反码和校验和对「字段改动」是可增量维护的，改动 old→new 等价于在校验和上叠加 `~old + new`（16 位反码算术、进位端回卷，RFC 1624 口径），无需重算整包。
- 理解 UDP 校验和 0 的线缆语义：0 表示「发送方未计算校验和」，对这样的报文不得调用校验和辅助函数。
- 学会通配规则的写法与命中路径：key 中为 0 的字段是通配维度，由数据面「精确未命中后清零侧字段二次查找」命中，并用 `is_snat` 按方向过滤表项。

## 文件说明

| 文件 | 说明 |
|---|---|
| `fw_common.h` | 阶段 4 共享头（阶段 3 全文 + NAT 追加）：`NAT_MAX_ENTRIES`、`nat_key`/`nat_value`（字段契约一经定义不再改动，Go 控制面按同一契约镜像），统计下标扩为 8 项（新增 `STAT_NAT`） |
| `nat.bpf.c` | TC 数据面：`do_nat(skb, dir)` 改写核心（两段通配查找 → 先改字段后修校验和 → UDP 0 校验和跳过）+ `nat_ingress`（DNAT）/`nat_egress`（SNAT）两个入口，per-CPU 计数 |
| `nat.c` | 用户态 loader：解析 `-i`/`-e` 与 `--dnat`/`--snat` → 下发规则到 `nat_map` → TC 挂载（`-i` 接口 ingress、`-e` 接口 egress）→ 每 2 秒打印 8 项统计，退出时显式 detach |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`bpf_skb_store_bytes`、`bpf_l3/l4_csum_replace` 与 libbpf TC 挂载 API 在 5.10 上均已可用）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`（TC 挂载走 netlink，`CAP_NET_ADMIN` 必需）；实验拓扑由 `../scripts/setup-veth.sh` 创建：DNAT 挂 `veth-fw-c` ingress，SNAT 挂 `veth-fw-s` egress；建流工具用 `iperf3`（编译不依赖）。
- 安全警告：本阶段程序会**真实改写经过的报文**（不再是前几阶段的只观测），且 TC egress 挂载会影响该接口发出的所有流量——仍只应挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口。

## 编译与运行

```bash
cd book/examples/chapter-17/04-nat-tc
make

# 需要先建立实验拓扑（见 ../scripts/setup-veth.sh）：
sudo ../scripts/setup-veth.sh

# 下发 NAT 规则并挂载（Ctrl-C 退出时显式 detach）：
sudo ./nat -i veth-fw-c -e veth-fw-s \
     --dnat tcp,10.0.1.1,8080,10.0.2.2,8080 \
     --snat 10.0.0.2,10.0.2.1
```

`--dnat <proto>,<原目的IP>,<原目的端口>,<新IP>,<新端口>` 写源侧通配 key（`src=0,sport=0`）；`--snat <原源IP>,<新IP>` 写目的侧通配 key（`dst=0,dport=0,同 proto`，复用第一条 `--dnat` 的协议，且只改地址不改端口——`new_port=0` 即数据面的「仅改地址」语义）。两者均可重复。

另开终端，在 ns-server 侧抓包并观察改写效果：

```bash
# 抓包看两个方向的改写（可选：先在 ns-server 起 iperf3 服务端，回程才有 SYN-ACK）
sudo ip netns exec ns-server tcpdump -ni veth-s tcp port 8080

# 客户端向「原目的地址」10.0.1.1:8080 发起连接（SYN 逐条被改写转发）
sudo ip netns exec ns-client iperf3 -c 10.0.1.1 -p 8080 -t 30
```

## 预期输出

（预期输出示例 / 运行级待验证）

loader 启动后每 2 秒打印 8 项统计；客户端发流后 `nat` 递增（每条前向报文在两个挂载点各改写一次——DNAT、SNAT 各 +1，故数值按 2 递进，具体大小取决于 SYN 重传节奏）：

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

- **单挂载点的可见性（如实说明）**：`nat_ingress` 只在 `veth-fw-c` 的接收方向生效、`nat_egress` 只在 `veth-fw-s` 的发送方向生效。服务端回包从 `veth-fw-s` **进入** ns-fw（ingress 无程序）、再从 `veth-fw-c` **发出**（egress 无程序）——两个挂载点都看不见回程，因此本阶段**不做回程 un-NAT**：回程报文原样到达客户端（`10.0.2.2:8080 -> 10.0.2.1:41322`），客户端套接字等待的是 `10.0.1.1:8080` 的应答，握手无法完成。这正是「挂载点决定数据面可见性 + NAT 是双向对称改写」的活教材：`do_nat` 的通配与方向过滤逻辑已就绪，把同一张表按镜像规则挂到回程的两个方向即可补全 un-NAT（阶段 5 完整版的路线）。
- `nat` 只计实际改写成功的报文（命中规则并完成改写才 +1）；未命中、非 IPv4/TCP/UDP、残包一律静默放行、不计数——与阶段 2/3 的「计数=实际发生」口径一致。
- `total`/`pass` 等 7 项属于阶段 2/3/5 数据面的计数，本阶段数据面不维护、恒为 0；loader 按契约 C1 全量打印以保持各阶段输出可比。

## 代码讲解要点

- **DNAT/SNAT 挂载方向**：DNAT 挂 `veth-fw-c` ingress——必须赶在 ns-fw 的路由查找**之前**改目的地址，路由才知道把报文往 `veth-fw-s` 转发；SNAT 挂 `veth-fw-s` egress——路由决策后、出接口前改源地址。两个入口共用 `do_nat(skb, dir)`，`dir=0/1` 区分目的/源改写。
- **改写顺序与指针失效**：`do_nat` 先把所有需要的信息读进局部变量（`ip->saddr/daddr`、端口、old_addr/new_addr、三个改写偏移），然后才调第一个改写辅助函数。原因是 `bpf_skb_store_bytes`/`bpf_l4_csum_replace` 可能重排报文缓冲区，verifier 会把这类调用视为「包指针失效点」，之后再解引用 `eth`/`ip` 直接加载失败。注意顺序约束在「所有指针读取 vs 第一个改写调用」之间；「先改字段后修校验和」本身不影响正确性——校验和辅助函数的入参自带 old/new 增量，不读报文。
- **为什么用绝对偏移的改写辅助函数**：TC 上下文的 skb 数据区可能非线性（分页）或被内核移动，直接解引用写 `ip->daddr` 只对线性区安全且绕过内核的缓冲区状态维护；`bpf_skb_store_bytes` 按绝对偏移改写、由内核处理数据区变化，读端口也统一用 `bpf_skb_load_bytes`（读辅助函数不改动报文、不会使包指针失效）。
- **增量校验和数学直觉**：反码和校验和是「所有 16 位字的反码和再取反」，对单个字段 old→new 的改动可增量维护：`HC' = ~（~HC + ~old + new）`，加法用 16 位反码算术、进位回卷到低位（RFC 1624 修正了 RFC 1141 的进位次序）。`bpf_l3_csum_replace`/`bpf_l4_csum_replace` 以 `(from, to)` 增量入参把这段算术封进内核；IP 头校验和只覆盖 IP 头（只补地址增量），TCP/UDP 校验和还覆盖伪头里的 IP 地址——所以 L4 侧的地址增量要带 `BPF_F_PSEUDO_HDR`（告诉内核这次改动来自伪头，内核据此同步维护部分校验和状态，校验和卸载场景必需），端口增量则是普通 2 字节替换。
- **UDP 校验和 0 是哨兵值**：IPv4 的 UDP 校验和字段为 0 表示「发送方未计算校验和」（真实的零结果按约定以 0xFFFF 发送）。对 0 值报文调用校验和辅助函数会把哨兵改写成非 0，接收方会把「未校验」的报文误判为「校验和错误」丢掉——数据面先 `bpf_skb_load_bytes` 读出原值、判 0 跳过。内核辅助函数其实提供了 `BPF_F_MARK_MANGLED_0` 标志处理同一约定，教学实现用显式判 0 把这条协议语义摆在明面上。
- **通配规则的两段查找与方向过滤**：先按报文五元组精确查 `nat_map`；未命中再构造 wild key 查一次——DNAT（dir=0）清零源侧（`src_ip/src_port`，客户端源端口不可预知），SNAT（dir=1）清零目的侧与源端口（目的因 DNAT 而异、源端口是内核随机分配的临时值，`--snat` 只有 IP 入参，静态规则写不出）。命中后还要 `is_snat == dir` 校验：DNAT 程序只认 `is_snat==0` 的表项、SNAT 程序只认 `is_snat==1`，一张表服务两个方向互不误伤。
- **map 类型选型**：`nat_map` 用普通 hash 而非阶段 3 的 LRU hash——NAT 规则是控制面静态下发的有限集合（上限 `NAT_MAX_ENTRIES=4096`），没有 conntrack 那种「随流量增长、需自动淘汰」的压力；普通 hash 语义确定（不存在静默淘汰导致规则消失）、查找最快。统计仍用 per-CPU 数组：数据面无锁更新，loader 侧求和展示。
- **verifier 边界检查**：每层头解引用前都有 `(eth + 1) > data_end` 这类比较——verifier 只认这种与 `data_end` 的比较作为指针定界证据，少写一次即加载失败；L4 偏移由 `ihl*4` 计算、跨过可选字段，改用读辅助函数后天然受长度保护。
- **TC filter 的生命周期**：`bpf_tc_attach` 装上的 filter 持有程序引用，**不随 loader 进程退出自动卸载**（XDP 的 link 对象 destroy 即卸载，TC 没有对应物）——loader 退出前显式 `bpf_tc_detach`；若被 `kill -9`，残留 filter 会让下次同 handle/priority 的 attach 报 EEXIST，需手工 `tc filter del` 清理（见常见失败点）。
- **loader 侧镜像结构体**：skeleton 只导出 map/program 句柄、不导出 BPF 源码里的类型与枚举，而 `fw_common.h` 依赖 `vmlinux.h`（内核 BTF）不能被用户态包含——`nat.c` 按契约逐字段镜像 `nat_key`/`nat_value`/统计枚举（与阶段 2/3 同理，改任一侧必须同步另一侧）。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。先用 `../00-env/check-env.sh` 确认权限档位 |
| `创建 clsact 失败` 或 `attach ... 失败` | 无 `CAP_NET_ADMIN`（TC 挂载走 netlink），或误挂物理网卡。务必只挂实验拓扑的 veth 接口 |
| 重跑时报 `attach ... 失败: File exists` | 上次进程被 `kill -9`、未走 detach 清理，残留 filter 占用了同 handle/priority：`sudo tc filter del dev veth-fw-c ingress`、`sudo tc filter del dev veth-fw-s egress` 后重跑（正常 Ctrl-C 退出不会发生） |
| 编译报 `vmlinux.h file not found` | 未先 `make`（首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 生成），或内核缺少 BTF（参见附录 C） |
| 编译报 `use of undeclared identifier 'ETH_P_IP'` / `'TC_ACT_OK'` | 二者都是 uapi 头文件里的 `#define`，`vmlinux.h` 只含类型不含宏；示例源码顶部已补定义，删掉即复现 |
| tcpdump 看不到 `10.0.2.2:8080` 的改写报文 | 按序排查：loader 启动行是否显示「已下发 NAT 规则：DNAT 1 条」；流量是否真的经过 fw（拓扑/默认路由）；协议是否匹配（`--snat` 复用 `--dnat` 的 proto，只对 tcp 生效） |
| tcpdump 看到源仍是 `10.0.0.2`（SNAT 未生效） | `--snat` 的原源 IP 与客户端地址不一致，或流量的协议与 `--dnat` 声明的不同（SNAT key 按源 IP + 协议命中） |
| 客户端握手失败、回包未被还原 | 预期——单挂载点看不见回程，本阶段不做 un-NAT（见「预期输出」第 1 条），完整双向 NAT 由阶段 5 补全 |
| 服务端丢包、tcpdump 提示校验和错误 | 校验和增量被删改：地址增量必须带 `BPF_F_PSEUDO_HDR`，UDP 校验和为 0 的报文必须跳过改写（见「代码讲解要点」） |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- RFC 1624：Computation of the Internet Checksum via Incremental Update（增量校验和的
  正确算术与对 RFC 1141 进位次序的修正）。
- `include/uapi/linux/bpf.h`：`bpf_skb_store_bytes`/`bpf_l3_csum_replace`/`bpf_l4_csum_replace`
  的语义，`BPF_F_PSEUDO_HDR`/`BPF_F_MARK_MANGLED_0` 标志，`BPF_F_PSEUDO_HDR` 与部分校验和状态。
- `include/uapi/linux/pkt_cls.h`：`TC_ACT_OK` 等 TC 动作返回值。
- libbpf 文档：TC 挂载 API（`bpf_tc_hook_create`/`bpf_tc_attach`/`bpf_tc_detach`，clsact
  qdisc 与 handle/priority 的定位语义）。
- `tc(8)`/`tc-bpf(8)`：clsact qdisc、tc filter 的生命周期与手工清理。
- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，hash 与 LRU hash 的选型对比、
  per-CPU 数组映射）。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式（阶段 1–3 同源）。
- iperf3 用户文档：建流方法学（与阶段 2/3 的压测口径一致）。

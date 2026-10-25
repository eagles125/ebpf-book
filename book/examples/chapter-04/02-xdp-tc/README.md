# 02-xdp-tc：同一个包，两副眼镜——XDP 与 TC 同挂 lo 对照读数（网络类集成锚点）

《第 4 章 eBPF 程序类型与钩子点深入》「四、实战：为任务选对类型并挂上钩子」的网络类集成锚点，
同时是「三、类型画廊：十四类逐类走读」中 XDP 与 TC 两张 card 的对照实物：同一份回环流量
（lo 上的 ping/curl）先后经过两个网络挂载点——驱动层收包路径的 XDP 与协议栈入口的 TC。
两个程序各自数包、各自认 UDP，两个计数器并排打印；ctx 一个是 `struct xdp_md`、
一个是 `struct __sk_buff`，「同一个包，两副眼镜」的字段与语义差异在这一个目录里逐项对照。

## 演示目标

- **两个对象、两个程序、两个 map、一个 loader**：`xdp_count.bpf.c`（`SEC("xdp")`，ctx 为
  `struct xdp_md`）与 `tc_count.bpf.c`（`SEC("tc")`，ctx 为 `struct __sk_buff`）是除挂载点与
  ctx 外逐行同构的两个 BPF 对象——解析逻辑完全一样（以太网头 → IPv4 头 → `protocol==17`
  计 UDP），差异只剩「站在哪、戴什么眼镜」；`xdp_tc.c` 一个 loader 把两者同挂 lo，
  每秒打印两行对照读数。
- **verdict 语义对照**：XDP 返回 `XDP_PASS`（放行，裁决发生在驱动层，报文尚未进入协议栈）；
  TC 返回 `TC_ACT_OK`（放行，位置在协议栈入口的 clsact qdisc/filter）。两者都是「只观测不干预」。
- **map 选型**：两个 `BPF_MAP_TYPE_ARRAY`（`xdp_stats`/`tc_stats`，单 key 全局计数）——
  教学聚焦 ctx 差异而非 map 选型；并发计数用 `__sync_fetch_and_add` 原子递增。
- **挂载选型即教学**：XDP 有 bpf_link 对应物（`bpf_program__attach_xdp` 返回 link），
  TC 没有——走 netlink 的 qdisc/filter 模型（`bpf_tc_hook_create` + `bpf_tc_attach`，
  沿第 17 章 04-nat-tc 先例）。两套挂载生命周期在同一个 loader 里对照呈现。
- **观察即教学**：两个程序都只计数不改包；加载与运行需要特权（见「环境前提」与「常见失败点」）。

## 文件说明

| 文件 | 说明 |
|---|---|
| `xdp_count.bpf.c` | 内核态 BPF 程序（XDP 侧）：`SEC("xdp")`，ctx=`struct xdp_md`，`data`/`data_end` 手工边界检查后解析以太网/IPv4 头，计数入 `xdp_stats` |
| `tc_count.bpf.c` | 内核态 BPF 程序（TC 侧）：`SEC("tc")`，ctx=`struct __sk_buff`，解析逻辑与 XDP 侧逐行同构，计数入 `tc_stats`；`TC_ACT_OK` 手工 `#define`（uapi 常量不在 BTF/vmlinux.h） |
| `xdp_tc.c` | 用户态 loader：两个 skeleton open/load → XDP 走 `bpf_program__attach_xdp`（返回 `bpf_link`）、TC 走 `bpf_tc_hook_create`+`bpf_tc_attach`（netlink 模型）→ 每秒打印两行对照计数 → Ctrl-C 退出即清理 |
| `Makefile` | 全链构建（vmlinux.h → 两个 `.bpf.o` → 两个 `.skel.h` → 可执行 `xdp_tc`），本章 C4 模板的双对象变体 |

## 环境前提

- 先运行 `bash ../00-env/check-env.sh` 自检：无 root 也可运行，按「编译级 / 加载级 / 运行级」
  三档给出结论。编译级通过即可完成本目录 `make`；加载需要 root 或 `CAP_BPF` +
  `CAP_NET_ADMIN`；本机若 `unprivileged_bpf_disabled=2`，非 root 时 load 失败属预期。
- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成，BTF 缺失则无法编译）；
  TC 侧需内核开启 `CONFIG_NET_CLS_ACT`（clsact qdisc，主流发行版默认开启）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`（`bpf_tc_*` API 需 libbpf 0.4+）、`make`。
- 回环接口 `lo` 可见（loader 硬编码挂 lo，`if_nametoindex("lo")` 失败即退出）。
- 安全说明：本目录程序均为只读观测、只返回放行 verdict，不丢包不改包；`Makefile` 本身只编译不加载。
  **XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。**

## 编译与运行

```bash
cd book/examples/chapter-04/02-xdp-tc
make
sudo ./xdp_tc    # Ctrl-C 退出即清理（TC filter 显式 detach，clsact qdisc 留置无害）
```

触发：另开终端制造本机流量（`ping 127.0.0.1`、`curl 本机服务` 等走 lo 的流量），
两行计数同步增长；对比「同一时刻、同一批报文、两个挂载点各记一笔」即本锚点要钉住的事实。

## 预期输出

（预期输出示例 / 运行级待验证）

```text
XDP 与 TC 已同挂 lo，制造流量（如 ping 127.0.0.1、curl 本机服务）观察对照……
  [xdp      ] packets=128      udp=12
  [tc       ] packets=126      udp=12
----
```

- 首行为挂载成功提示；之后每秒打印一次两行对照计数（tag/总包数/UDP 包数）+ 分隔线。
- `udp` 两行应一致（同一段 UDP 流量先后经过两个挂载点）；`packets` 两行接近但不必然相等——
  XDP 在驱动层收包路径、TC 在协议栈入口，可见报文的时机与集合有细微差别，差异本身是教学点，
  具体以实测为准（运行级待验证）。

## 代码讲解要点

两种 ctx 对照（同一段解析代码、同一批报文，两副眼镜）：

| 维度 | XDP 侧：`struct xdp_md` | TC 侧：`struct __sk_buff` |
|---|---|---|
| 挂载位置 | 驱动层收包路径（报文尚未进入协议栈） | 协议栈入口 qdisc/filter（clsact ingress） |
| 报文数据 | `data`/`data_end`（另有 `data_meta`），手工边界检查是 verifier 硬要求 | `data`/`data_end`（与 xdp_md 形状相似！） |
| 独有字段 | `ingress_ifindex`/`rx_queue_index` 等少量元数据 | `mark`/`protocol`/`len`/`priority` 等——协议栈加工后已填好的字段 |
| 解析方式 | 全手工：以太网头 → IPv4 头逐字段推进 | 与 XDP 侧逐行同构（本示例刻意保持一致，便于 diff 出 ctx 差异） |
| 放行 verdict | `XDP_PASS`——BTF enum `xdp_action` 成员，vmlinux.h 直接可用 | `TC_ACT_OK`——uapi pkt_cls.h 的 `#define`，不在 BTF/vmlinux.h，需手工定义 |
| 看不到什么 | 协议栈状态（socket、skb->mark、qdisc） | 驱动层最早时刻（XDP 能在协议栈之前裁决） |

挂载方式差异（「类型决定挂载方式」的网络类另一半）：

| 维度 | XDP | TC |
|---|---|---|
| libbpf API | `bpf_program__attach_xdp(prog, ifindex)` | `bpf_tc_hook_create()` 建 clsact + `bpf_tc_attach()` 绑 filter |
| 返回物 | `struct bpf_link *`（link 模型） | 无 bpf_link 对应物——netlink 的 qdisc/filter 模型 |
| 已存在冲突 | 同接口同模式已挂其他 XDP 程序时 attach 失败 | `bpf_tc_hook_create` 对已存在的 clsact 返回 `-EEXIST`，本 loader 视为成功 |
| 退出清理 | `bpf_link__destroy()` | `bpf_tc_detach()`（filter；qdisc 留置无害） |

- **边界检查逐项对应第 3 章画廊 card ②**：`(void *)(eth + 1) > data_end`、
  `(void *)(ip + 1) > data_end`——每次指针推进都必须先检查再解引用；XDP/TC 两侧同构，
  verifier 对两种程序类型一视同仁。
- **常量三态教学点**：`ETH_P_IP`、`IPPROTO_UDP` 以数值（`0x0800`/`17`）直接写入；
  `XDP_PASS` 在 BTF（enum `xdp_action`）；`TC_ACT_OK` 不在 BTF（uapi `#define`）——
  「哪些常量在 BTF 里、哪些要手工定义」正是冒烟结论④要钉住的差异。
- **map 读取**：loader 侧 `bpf_map__fd()` 拿 fd、`bpf_map_lookup_elem()` 每秒读一次；
  ARRAY 单 key，读到的就是当前累计值。
- **失败分支必检**：`bpf_map_lookup_elem` 返回 NULL 即放行退出（第 3 章 card ①）；
  skeleton open/load 失败、任一 attach 失败均 `goto out` 统一清理（先 TC filter、
  再 XDP link、最后两个 skeleton destroy——第 2 章生命周期回指）。

## 常见失败点

| 现象 | 原因 | 处理 |
|---|---|---|
| `load 失败（无特权环境的预期失败）` | 非 root 且 `unprivileged_bpf_disabled=2`，open/load 阶段即报 EPERM | 加载级实验用 `sudo`；无特权环境只做编译级验证（`make` 通过即本目录验收口径） |
| `XDP attach 失败（需 root）` | 非 root 缺 `CAP_NET_ADMIN`；或该接口同模式已挂了其他 XDP 程序 | 先卸载已有程序（如 `ip link set dev lo xdp off`）或换空闲的 veth 接口实验 |
| `clsact 创建失败（需 root）` | 非 root 缺 `CAP_NET_ADMIN`；或内核未开 `CONFIG_NET_CLS_ACT` | 用 `sudo`；确认内核配置（主流发行版默认开启） |
| `TC attach 失败` | 相同 handle/priority 的 filter 已存在 | 换 handle/priority，或先 `sudo tc qdisc del dev lo clsact` 清理旧 filter |
| lo 上 XDP 行为与预期不符（如无 native 模式） | lo 无驱动 XDP 支持，flags=0 时内核一般自动回退 generic（SKB）模式 | 属预期；具体行为随内核版本，以实测为准（运行级待验证） |
| `make` 报 `vmlinux.h` 生成失败 | 内核缺 BTF（`CONFIG_DEBUG_INFO_BTF` 未启用） | 参见附录 C 发行版 BTF 构建指南 |

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，
  全链生成（`xdp_count.bpf.o`/`tc_count.bpf.o` → `xdp_count.skel.h`/`tc_count.skel.h` →
  可执行 `xdp_tc`）。编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警
  （bpftool 导出的前向声明在 clang 18 下的已知现象），与示例源码无关。
- **本章补充（C6 口径）**：全链锚点验证状态为「编译级（含 gcc 链接）已通过；
  加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/networking/`（XDP）与 `Documentation/bpf/`
  下 XDP/TC 类程序文档；`include/uapi/linux/pkt_cls.h`（`TC_ACT_OK` 等返回码定义）。
- 内核源码 `include/uapi/linux/bpf.h`（`struct xdp_md`/`struct __sk_buff` 的 uapi 视图）、
  `net/core/dev.c`（XDP 与 TC 的执行路径）。
- libbpf 文档：`bpf_program__attach_xdp()`；`bpf_tc_hook_create`/`bpf_tc_attach`/`bpf_tc_detach`
  （libbpf TC 接口，netlink qdisc/filter 模型，libbpf 0.4+）。
- 第 17 章 `book/examples/chapter-17/04-nat-tc/`：TC attach 模式（`bpf_tc_hook_create` 容忍
  `-EEXIST` + `bpf_tc_attach`）的先例。

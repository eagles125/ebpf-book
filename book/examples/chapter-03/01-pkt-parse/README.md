# 01-pkt-parse：能通过验证的最小 XDP 报文解析——「通过路径」锚点

《第 3 章 验证器与安全保障》「四、实战：读懂你的第一条 verifier 日志」5.2 节走读的「通过路径」代码对象：
用 XDP（eXpress Data Path，快速数据路径）程序在网卡收包路径上解析报文并计数，
完整展示验证器（verifier）放行一段程序需要看到什么——每层协议头解引用前的边界检查。

## 演示目标

- 提供「验证通过」路径的实物：eth→ipv4→udp 三层报文解析，每层解引用前先做
  `data + sizeof(层) > data_end` 边界检查，再加映射（map）计数组成最小交互闭环——
  加载 → 验证器校验 → JIT → 挂载 → 触发 → 读回 → Ctrl-C 卸载。
- 与失败画廊（`../02-fail-gallery/`）的对照关系：画廊 02-pkt-bounds 的 bad 程序
  就是本程序去掉边界检查后的形态——同一个解析链，缺一层检查即被验证器拒绝
  （编译成功≠能加载）；先读本程序建立「通过时长什么样」的基线，再看画廊理解「拒绝时缺了什么」。

## 文件说明

| 文件 | 说明 |
|---|---|
| `pkt_parse.bpf.c` | 内核态 BPF 程序：`SEC("xdp")` 三层报文解析 + 映射计数，能通过验证的最小形态 |
| `pkt_parse.c` | 用户态 loader：open_and_load → attach XDP → 每秒打印 UDP 计数 → Ctrl-C 退出即卸载 |
| `Makefile` | 全链构建（vmlinux.h → bpf.o → skel.h → 可执行文件） |

## 环境前提

- 先运行 `bash ../00-env/check-env.sh` 自检：无 root 也可运行，按「编译级 / 加载级 / 运行级」
  三档给出结论。编译级通过即可完成本目录 `make`；加载需要 root 或 `CAP_BPF`，
  挂载 XDP 还需 `CAP_NET_ADMIN`；本机若 `unprivileged_bpf_disabled=2`，非 root 时
  `open_and_load` 失败属预期。
- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成，BTF 缺失则无法编译）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`。
- 安全警告：XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。

## 编译与运行

```bash
cd book/examples/chapter-03/01-pkt-parse
make
sudo ./pkt_parse lo    # 建议挂 lo 或 veth；Ctrl-C 退出即卸载
```

触发：另开终端执行 `ping 127.0.0.1`，或任意本机 UDP 流量（DNS 查询等）都会让计数增长；
本程序只观测不干预，一律返回 `XDP_PASS`（放行）。

挂载模式：`flags=0` 挂载时若驱动不支持原生 XDP，内核一般会自动回退到 generic（SKB）模式——
具体行为随内核版本，以实测为准。

## 预期输出

（预期输出示例 / 运行级待验证）

```text
XDP 已挂载到 lo，每秒打印 UDP 计数，Ctrl-C 退出。
udp_packets=42
udp_packets=57
```

- 首行为挂载成功提示；之后每秒打印一次累计 UDP 报文数，有本机 UDP 流量时持续增长。

## 代码讲解要点

- **三层边界检查的推进模式**：上一层尾 = 这一层头——以太网头检查通过后，
  IPv4 头指针取 `data + sizeof(*eth)`，UDP 头指针取 `ip + sizeof(*ip)`；
  每个头解引用前都用 `(void *)(指针 + 1) > data_end` 证明整个头落在报文内。
  验证器据此把「报文内」状态沿着指针传播，任一层缺检查即被拒（对照画廊 02）。
- **字节序转换**：`eth->h_proto` 是网络字节序，与协议常量比较前用 `bpf_htons(ETH_P_IP)`
  转换（来自 `<bpf/bpf_endian.h>`）；漏转换在编译期合法、运行期行为错误——验证器不管语义对错。
- **映射选型与原子计数**：`BPF_MAP_TYPE_ARRAY` 单槽计数器（key 恒为 0）承载最小映射交互；
  多网卡队列并发递增用 `__sync_fetch_and_add` 原子操作——验证器天然放行，无需锁。
- **XDP 显式挂载与 tracepoint SEC 推断的对比**：tracepoint 的挂载点（attachment point）
  可从 SEC 名完整推断（`trace_stat_bpf__attach(skel)` 无参即可）；XDP 的挂载目标是网络接口，
  SEC 名 `xdp` 只决定程序类型（program type），loader 必须显式传 `ifindex`
  （`bpf_program__attach_xdp(skel->progs.pkt_parse, ifindex)`）。
- `char LICENSE[] SEC("license") = "GPL"`：GPL 兼容声明，必需。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。用 `../00-env/check-env.sh` 确认权限档位，改用 `sudo` 运行 |
| 挂到物理网卡 | 安全警告：XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡 |
| `make` 报 `vmlinux.h file not found` 或 bpftool 生成失败 | 首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 自动生成 `vmlinux.h`，生成失败即内核缺少 BTF（`CONFIG_DEBUG_INFO_BTF` 未启用），参见附录 C |
| 报 `网卡 xxx 不存在` | `if_nametoindex` 查不到该接口名：用 `ip link` 确认接口名，或改用 `lo` |
| 报 `attach XDP 失败（需 root/CAP_NET_ADMIN…）` | 已加载成功但挂载被拒：缺少 `CAP_NET_ADMIN`；确认 `sudo` 且接口支持（驱动不支持时按上文挂载模式说明回退 generic，仍失败则换 lo/veth 实测） |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：XDP（`Documentation/networking/xdp-rx-metadata.rst` 及
  `Documentation/bpf/` 下 XDP 相关文档）。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式。
- `bpf_helpers.h`（libbpf）：`SEC`、映射定义宏与辅助函数声明；`bpf_endian.h`：字节序转换宏。

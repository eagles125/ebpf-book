# 01-xdp-drop：最小 XDP 丢弃

第 17 章《实战：构建高性能 eBPF 防火墙》渐进示例的阶段 1：用约 40 行的 XDP 程序完成第一个「加载 → verifier → JIT → 挂载 → 触发」闭环。

## 演示目标

- 跑通第一个完整闭环：`open_and_load`（打开对象 → verifier 校验 → JIT 编译）→ 显式 attach 到 XDP 挂载点 → 报文触发 → 轮询映射观察效果。
- 理解 XDP 挂载点的位置：网卡驱动收包最早处，报文尚未生成 skb、不进协议栈。
- 理解 XDP 动作返回值的语义：`XDP_DROP` 在驱动层直接丢弃，`XDP_PASS` 交给内核协议栈继续处理。
- 认识 per-CPU 数组映射：数据面每 CPU 独立副本、无锁计数，用户态轮询时再按 CPU 求和。

## 文件说明

| 文件 | 说明 |
|---|---|
| `xdp_drop.bpf.c` | 内核态 BPF 程序：丢弃所有 IPv4 报文（`XDP_DROP`），放行 ARP 等（`XDP_PASS`），per-CPU 数组计数 |
| `xdp_drop.c` | 用户态 loader：open_and_load → attach XDP → 每秒轮询 stats 映射（阶段 2–4 loader 复用其骨架） |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`；实验拓扑由 `../scripts/setup-veth.sh` 创建，XDP 挂载点为拓扑中的 `veth-fw-c`。
- 安全警告：XDP 程序会直接丢包，**只挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**，否则可能立即失联。

## 编译与运行

```bash
cd book/examples/chapter-17/01-xdp-drop
make

# 需要先建立实验拓扑（见 ../scripts/setup-veth.sh）：
sudo ../scripts/setup-veth.sh

# 在拓扑的转发接口上挂载（Ctrl-C 退出并自动卸载）：
sudo ./xdp_drop -i veth-fw-c
```

另开一个终端，从客户端命名空间触发流量：

```bash
sudo ip netns exec ns-client ping 10.0.2.2
```

## 预期输出

（预期输出示例 / 运行级待验证）

```text
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
pass=14 drop=12
pass=14 drop=12
...
```

- `pass` 统计所有到达报文：程序入口无条件 `bump(STAT_PASS)`，含 ARP 等非 IPv4 报文；
  `drop` 统计被丢弃的 IPv4 报文。因此恒有 `pass >= drop`，ping（如 `-c 12`）期间两者同步递增。
- 由于 IPv4 报文在 `veth-fw-c` 的驱动层即被丢弃，`10.0.2.2` 应不可达（ping 全部超时）——
  正好用来对照「报文在协议栈之前就被丢了」与「计数映射里看得见丢了多少」。

## 代码讲解要点

- `SEC("xdp")`：声明程序类型为 XDP。与第 9 章 tracepoint 示例不同，XDP 挂载点无法从
  section 名推断到具体接口，loader 必须调用 `bpf_program__attach_xdp(prog, ifindex)` 显式挂载。
- 动作返回值：`XDP_DROP(1)` 在驱动层直接丢弃，不生成 skb、不进协议栈；`XDP_PASS(2)` 交给协议栈。
  两者来自 `include/uapi/linux/bpf.h` 的 `enum xdp_action`。
- verifier 边界检查：`xdp_md` 的 `data`/`data_end` 描述报文起止，verifier 只认显式边界检查
  （`(void *)(eth + 1) > data_end`），「人看起来安全」的解引用一律拒绝加载。
- 映射选型：`BPF_MAP_TYPE_PERCPU_ARRAY` 每 CPU 独立副本，数据面更新无锁、零竞争；
  用户态 `bpf_map_lookup_elem` 一次返回全部 CPU 份的值，loader 端按 `libbpf_num_possible_cpus()` 求和。
- 宏与 BTF：`ETH_P_IP` 是 uapi 头文件里的 `#define`，BTF 只导出类型、不导出宏，因此
  `vmlinux.h` 中没有它——CO-RE 程序遇到这类宏需在源码内自行补定义。
- `char LICENSE[] SEC("license") = "GPL"`：GPL 兼容声明，必需。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。先用 `../00-env/check-env.sh` 确认权限档位 |
| `attach_xdp 失败`，或挂载后远程会话失联 | 误挂到物理网卡/SSH 接口：XDP 在驱动层直接丢包。务必只挂实验拓扑的 veth 接口 |
| 编译报 `vmlinux.h file not found` | 未先 `make`（首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 生成），或内核缺少 BTF（参见附录 C） |
| 编译报 `use of undeclared identifier 'ETH_P_IP'` | `vmlinux.h` 只含类型不含宏；示例源码内已 `#define ETH_P_IP 0x0800`，删掉该行即复现 |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式。
- Linux kernel documentation：XDP 与 `include/uapi/linux/bpf.h` 的 `enum xdp_action`。
- libbpf 文档：`bpf_program__attach_xdp`、per-CPU 映射的用户态 `bpf_map_lookup_elem` 语义。
- ebpf.top：BPF 入门系列（XDP 动作返回值与挂载点）。

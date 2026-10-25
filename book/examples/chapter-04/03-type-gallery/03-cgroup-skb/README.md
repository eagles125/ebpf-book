# 03-cgroup-skb：cgroup 维度的网络 verdict——1 放行 / 0 丢弃（类型画廊③）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.10 节
（cgroup/skb card）的配套最小程序。同一个 `__sk_buff` ctx，TC 返回 `TC_ACT_OK`、
XDP 返回 `XDP_PASS`，cgroup/skb 却是裸的 **1 放行 / 0 丢弃**——「类型决定返回值语义」
的活例子。

## 任务一句话

写一个 `SEC("cgroup/skb")` 程序：用 `bpf_get_current_cgroup_id()` 演示 cgroup 上下文
获取，然后把放行计数记入 PER-CPU ARRAY 并恒返回 1（放行）——0 丢弃分支留给读者改造。

## 代码走读

- **返回值语义**：1 放行 / 0 丢弃——与 TC 的 `TC_ACT_*`、XDP 的 `XDP_*` 都不同，
  这是「类型决定返回值语义」三元组维度的活例子；本示例 map 异常分支也返回 1——
  观测程序不因自身异常误杀流量。
- **ctx 形状**：`int cg(struct __sk_buff *skb)`——与 TC、socket filter（画廊④）同形；
  同一 ctx 形状配不同挂载点与返回值语义，对照见画廊④与锚点②。
- **cgroup 上下文**：`bpf_get_current_cgroup_id()` 拿到触发本程序执行的 cgroup——
  cgroup 族类型独有的上下文维度（对照：XDP/TC 没有 cgroup 语义）；本示例
  `(void)cg_id` 只演示获取、不做过滤决策。
- **map 选型**：`BPF_MAP_TYPE_PERCPU_ARRAY`——verdict 计数是高频写，per-CPU 免锁聚合
  （第 2 章映射全景回指）；0=放行 1=丢弃 两 key，读数时把各 CPU 值相加。
- **失败分支必检**：`if (!v) return 1`——lookup 检查不可省（第 3 章画廊 card ①），
  且 verdict 类程序的失败分支返回什么本身就是设计点：这里选放行。

## 挂载方式与前提

- **预期加载前提**：root + 挂到某个 cgroup（attach 对象是 cgroup fd，非网卡）；
  veth/netns 拓扑见第 17 章。内核需 `CONFIG_CGROUP_BPF=y`（主流发行版默认开启）。
- **挂载形态**：libbpf `bpf_program__attach_cgroup(prog, cgroup_fd)`，或
  `bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_INGRESS/EGRESS, flags)`——
  cgroup/skb 分入口与出口两个挂载方向；`bpftool cgroup attach` 亦可。
- **实验路径**：对自建 cgroup 挂载后，在该 cgroup 内进程制造流量（`curl`/`ping`）
  观察放行计数；卸载用 `bpf_link__destroy()` 或 `bpftool cgroup detach`。

## 预期输出示例

（预期输出示例 / 运行级待验证）per-CPU map 的读数按 CPU 分列（与 ARRAY 的单值不同）：

```text
$ sudo bpftool map dump name cg_verdict
key:
  00 00 00 00
values:
  cpu: 0  value: 18 00 00 00 00 00 00 00   # CPU0 上的放行计数
  cpu: 1  value: 06 00 00 00 00 00 00 00   # CPU1 上的放行计数
key:
  01 00 00 00
values:
  cpu: 0  value: 00 00 00 00 00 00 00 00   # 本示例恒放行，丢弃恒 0
  cpu: 1  value: 00 00 00 00 00 00 00 00
Found 2 elements
```

总放行数 = 各 CPU 值相加（per-CPU 语义）；1 号 key 的读数留给读者把返回值改成 0 后对照。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `cgroup_skb.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/`（cgroup/skb 与 attach 类型语义）。
- libbpf 文档：`bpf_program__attach_cgroup()`；helper `bpf_get_current_cgroup_id()`。
- 内核源码 `include/uapi/linux/bpf.h`（`BPF_CGROUP_INET_INGRESS`/`EGRESS`）。
- 第 17 章 veth/netns 拓扑示例（本类型实验的网络环境参照）。

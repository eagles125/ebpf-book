# 01-trace-stat：sched_switch 统计——第一个 map 交互闭环

第 2 章《eBPF 架构与核心概念》「五、实战：第一个追踪统计示例」一节的配套集成示例：用 sched_switch tracepoint + per-CPU 数组映射，跑通本章八要素的最小可编译闭环。

## 演示目标

- 跑通第一个「加载 → verifier → JIT → 挂载 → 触发 → 读回」的映射（map）交互闭环：
  `open_and_load`（打开对象 → verifier 校验 → JIT 编译）→ `attach`（挂载点从 SEC 名自动推断）
  → 系统调度触发程序 → 每秒从映射读回各 CPU 统计 → Ctrl-C 退出即卸载。
- 把本章「八要素」逐一钉到具体代码行（与章级 README 的对照表一致）：

| 八要素 | 在本示例中的落点 |
|---|---|
| 程序（program） | `trace_stat.bpf.c` 中 `SEC("tracepoint/sched/sched_switch") int trace_stat(...)` 的程序定义行 |
| 映射（map） | `trace_stat.bpf.c` 中 `sched_counts` 的 map 定义（`BPF_MAP_TYPE_PERCPU_ARRAY`，`SEC(".maps")`） |
| 辅助函数（helper） | `trace_stat.bpf.c` 函数体内的 `bpf_get_smp_processor_id()` 与 `bpf_map_lookup_elem()` 调用行 |
| 程序类型（program type） | SEC 名前缀 `tracepoint/` 隐式决定 `BPF_PROG_TYPE_TRACEPOINT`（同程序定义行） |
| 挂载点（attachment point） | `trace_stat.c` 中 `trace_stat_bpf__attach(skel)` 一行（从 SEC 名完整推断，无需显式参数） |
| 上下文对象（context） | `trace_stat.bpf.c` 的形参 `struct trace_event_raw_sched_switch *ctx` 与 `p->last_next_pid = ctx->next_pid` 读取行 |
| 事件通道（event channel） | `trace_stat.c` 的 `while` 轮询循环（每秒经 `bpf_map_lookup_elem` 读回并求和；推送形态见练习 1） |
| 生命周期（lifecycle） | `trace_stat.c` 的 `open_and_load`（加载→verifier→JIT）→ attach → 触发 → Ctrl-C 后 `destroy`（卸载） |

## 文件说明

| 文件 | 说明 |
|---|---|
| `trace_stat.bpf.c` | 内核态 BPF 程序：sched_switch tracepoint 中按 CPU 累计调度次数并记录 `next_pid` |
| `trace_stat.c` | 用户态 loader：open_and_load → attach → 每秒轮询映射读回求和并打印 |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成，BTF 缺失则无法编译）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`。
- 先运行 `bash ../00-env/check-env.sh` 自检：无 root 也可运行，按「编译级 / 加载级 / 运行级」
  三档给出结论。编译级通过即可完成本目录 `make`；加载与运行需要 root 或 `CAP_BPF`
  （+ `CAP_PERFMON`/`CAP_SYS_ADMIN`）；本机若 `unprivileged_bpf_disabled=2`，非 root 时
  `open_and_load` 失败属预期。
- 安全警告：tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

## 编译与运行

```bash
cd book/examples/chapter-02/01-trace-stat
make
sudo ./trace_stat    # Ctrl-C 退出即卸载
```

触发：无需专门负载——sched_switch 是每次进程切换都会触发的 tracepoint，系统空闲时
也持续触发，因此运行后立刻能看到各 CPU 计数增长。

## 预期输出

（预期输出示例 / 运行级待验证）

```text
已挂载 tracepoint/sched/sched_switch，每秒打印，Ctrl-C 退出。
--- 各 CPU 调度次数 ---
cpu0  switches=1234       last_next_pid=1823
cpu1  switches=987        last_next_pid=910
total=2221
```

- 只打印 `switches` 非零的 CPU 行；`total` 为全部 CPU 计数之和；`last_next_pid` 随
  调度持续变化。

## 代码讲解要点

- `SEC("tracepoint/sched/sched_switch")`：SEC 名同时决定程序类型（前缀 `tracepoint/`
  隐式选择 `BPF_PROG_TYPE_TRACEPOINT`）与挂载点（`sched/sched_switch` 类别/事件），
  loader 侧 `trace_stat_bpf__attach(skel)` 因此无需任何显式参数。
- per-CPU「双层去竞争」：第一层 key 选 CPU 下标（`bpf_get_smp_processor_id()`），
  第二层映射类型选 `BPF_MAP_TYPE_PERCPU_ARRAY`——每 CPU 只写自己的槽位，数据面
  无锁、零竞争；代价是用户态读回时要对全部 per-CPU 槽位求和。
- `last_next_pid` 的 CO-RE 读取：tracepoint 上下文字段经 `vmlinux.h`（BTF 导出）定义，
  内核布局变化时 libbpf 在加载期重定位；`ctx->next_pid` 是直接解引用读取。
- 读回求和语义：`bpf_map_lookup_elem` 对 per-CPU 映射一次返回全部 CPU 的槽位副本，
  loader 按 `libbpf_num_possible_cpus()` 对 `switches` 求和；本示例中只有「写者 CPU =
  key」的槽位非零，求和即该 key（CPU）的真实计数。
- `MAX_CPUS` 防御定界：数组映射对越界 key 在 helper 内部返回 NULL（运行时定界），
  程序内的 `cpu >= MAX_CPUS` 检查是防御式写法，用于区分「CPU 下标异常」与「表项异常」。
- `char LICENSE[] SEC("license") = "GPL"`：GPL 兼容声明，必需。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 非 root 运行报 `open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）` | 预期行为：非 root 且 `kernel.unprivileged_bpf_disabled=2` 时不能加载 BPF。用 `../00-env/check-env.sh` 确认权限档位，改用 `sudo` 运行 |
| `make` 报 `vmlinux.h file not found` 或 bpftool 生成失败 | 首次构建由 bpftool 从 `/sys/kernel/btf/vmlinux` 自动生成 `vmlinux.h`，生成失败即内核缺少 BTF（`CONFIG_DEBUG_INFO_BTF` 未启用），参见附录 C |
| 启动即报 `CPU 数 N 超出教学上限 64` | 机器 CPU 数超过 `MAX_CPUS`：把 `trace_stat.bpf.c` 与 `trace_stat.c` 两侧的 `MAX_CPUS` 同步调大 |

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
  编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：BPF subsystem 与 tracepoints（`Documentation/trace/tracepoints.rst`）。
- libbpf-bootstrap 上游示例：skeleton + `open_and_load` 模式。
- `bpf_helpers.h`（libbpf）：`SEC`、map 定义宏与 helper 声明。

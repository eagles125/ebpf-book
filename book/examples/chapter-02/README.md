# 第 2 章独立示例资产

本目录是《第 2 章 eBPF 架构与核心概念》的配套示例代码，对应正文「五、实战：第一个追踪统计示例」一节；
全部资产围绕一个 sched_switch tracepoint 集成示例（01-trace-stat）与 2 项课后扩展任务组织。

## 目录结构

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | 内核/BTF/工具链/权限分级诊断（无 root 可跑） |
| `01-trace-stat/` | 集成示例 | sched_switch tracepoint + per-CPU 统计：八要素最小闭环 |
| `scripts/` | 编译级验证 | `verify-build.sh` 汇总本目录全部 make |
| `exercises/` | 课后扩展任务 | 2 项完整参考实现 + 防剧透 README |

## 演示目标

01-trace-stat 用一个可编译、可运行的最小闭环，把本章「八要素」逐一钉到具体代码行：

| 八要素 | 在 01-trace-stat 中的落点 |
|---|---|
| 程序（program） | `trace_stat.bpf.c` 中 `SEC("tracepoint/sched/sched_switch") int trace_stat(...)` 的程序定义行 |
| 映射（map） | `trace_stat.bpf.c` 中 `sched_counts` 的 map 定义（`BPF_MAP_TYPE_PERCPU_ARRAY`，`SEC(".maps")`） |
| 辅助函数（helper） | `trace_stat.bpf.c` 函数体内的 `bpf_get_smp_processor_id()` 与 `bpf_map_lookup_elem()` 调用行 |
| 程序类型（program type） | SEC 名前缀 `tracepoint/` 隐式决定 `BPF_PROG_TYPE_TRACEPOINT`（同程序定义行） |
| 挂载点（attachment point） | `trace_stat.c` 中 `trace_stat_bpf__attach(skel)` 一行（挂载点从 SEC 名完整推断，无需显式参数） |
| 上下文对象（context） | `trace_stat.bpf.c` 的形参 `struct trace_event_raw_sched_switch *ctx` 与 `p->last_next_pid = ctx->next_pid` 读取行 |
| 事件通道（event channel） | `trace_stat.c` 的 `while` 轮询循环（每秒经 `bpf_map_lookup_elem` 读回并求和） |
| 生命周期（lifecycle） | `trace_stat.c` 的 `open_and_load`（加载→verifier→JIT）→ attach → 触发 → Ctrl-C 后 `destroy`（卸载） |

## 学习路径

1. `bash 00-env/check-env.sh` 自检：无 root 也可运行，按「编译级/加载级/运行级」三档给出结论。
2. 进入 `01-trace-stat/`：先 `make` 编译，再按其 README 的「编译与运行」体验八要素闭环
   （加载与运行需要 root/CAP_BPF；编译级验证在无特权机器即可完成）。
3. 进入 `exercises/`：完成 2 项课后扩展任务（先自行实现，再看防剧透 README 后部的参考实现思路）。

## 安全警告

- tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。
- 本章示例为单机 tracepoint 场景：不涉及 veth/netns 拓扑，也不涉及网卡挂载（第 17 章「勿挂物理网卡」的警告措辞在本章不适用）。

## 统一构建模式

01-trace-stat 与 exercises 两个练习的 C 侧构建采用 libbpf-bootstrap 三步（各目录内直接 `make`）：

vmlinux.h（bpftool 由 /sys/kernel/btf/vmlinux 生成） -> *.bpf.o（clang -target bpf）
    -> *.skel.h（bpftool gen skeleton） -> 可执行 loader（gcc -lbpf -lelf -lz）

`00-env/` 为纯脚本目录（无 Makefile）；`scripts/verify-build.sh` 汇总本目录全部 `make`。

## 验证状态说明

遵循 `book/STYLE.md` 约定：

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  各 README 中「预期输出」均为教学示例。
- 全量编译结果由 `scripts/verify-build.sh` 生成：2026-10-20 本地全量运行——BTF 前置 OK，
  `00-env` 自检、`01-trace-stat`、`exercises/01-ringbuf-events`、`exercises/02-lru-top-proc`
  共 4/4 PASS、FAIL=0（退出码 0）；工具链：clang 18.1.3、bpftool 7.7.0、libbpf-dev 1.3.0
  （内核 7.0.0-31）。

## 参考来源

- Linux kernel documentation：BPF subsystem、tracepoints（`Documentation/trace/tracepoints.rst`）。
- libbpf 与 libbpf-bootstrap 项目文档。
- `bpftool` 手册页（BTF dump 与 skeleton 生成）。

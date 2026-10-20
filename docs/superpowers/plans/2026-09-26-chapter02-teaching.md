---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-09-26-chapter02-teaching-design.md
base-ref: 5f5bda5a0a24d93eec9c5e40c57d6b41995a8bba
---

# 第 2 章完整教学章节（build 阶段）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 2 章《eBPF 架构与核心概念》从零写为 35–48k 字符的完整教学章节（概念章 + 完整实战化），并落地 `book/examples/chapter-02/` 全部独立示例资产（00-env + 01-trace-stat 集成示例 + scripts 编译级验证）与 2 项课后练习的完整可编译参考实现（含防剧透 README）。

**架构:** 单一集成示例 01-trace-stat（sched_switch tracepoint + per-CPU array 统计，libbpf-bootstrap C loader 模式）贯穿八要素教学；正文按「完整章节模板」组织为 总览→指令集→映射全景→bpf() 内核分析→实战 五段，双视角落总览/映射/实战三节；课后练习（ringbuf 事件流、LRU top 进程）各带完整参考实现；`scripts/verify-build.sh` 做单章编译级汇总验证。

**技术栈:** C（vmlinux.h + clang -target bpf + bpftool gen skeleton + libbpf loader）、bash（环境自检/编译级验证脚本）、Markdown（中文正文）。本章无 Go。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md`「Chapter 2: eBPF Architecture and Core Concepts」9 条。
- delta spec 无需变更（无 Spec Patch），不修改 `openspec/changes/generate-ebpf-book/specs/`。
- **第 1 章两条遗留任务（终稿深度 review / 五层与双视角回填）不在本计划范围**，不要触碰 `book/chapters/01-*.md` 与 `book/examples/chapter-01/`。
- 不触碰 `book/chapters/09-*.md`、`book/chapters/17-*.md`、`book/examples/chapter-09/`、`book/examples/chapter-17/`。
- 只修改：`book/chapters/02-ebpf-architecture-and-core-concepts.md`（新建）、`book/examples/chapter-02/**`（新建）、`openspec/changes/generate-ebpf-book/tasks.md`（勾选）、`.gitignore`（追加产物条目）。

## 全局约束

以下约束适用于所有任务（复制自设计文档与 `book/STYLE.md`，逐字为准）：

1. **语言**：全书与全部代码注释使用中文（`Language: zh-CN`）；术语遵循 `book/STYLE.md`（map=映射、helper=辅助函数、program type=程序类型、attachment point=挂载点）；术语首次出现用「英文（中文解释）」或「中文（English）」格式。
2. **验证策略（硬性验收）**：编译级真实验证在本机执行——clang 18.1.3、bpftool 7.7.0（libbpf 1.3 系统头）、gcc、BTF（`/sys/kernel/btf/vmlinux` 存在）齐备，本章无 Go、无网络依赖；**无 root，`kernel.unprivileged_bpf_disabled=2`**。凡涉及加载/运行的效果一律写「预期输出示例」并标注「运行级待验证」，不得写成「已验证通过」。
3. **运行级验证**：只允许出现「预期输出示例 / 运行级待验证」标注；如未来在有特权环境验证，才可改为实测记录。
4. **篇幅**：章节正文目标 35–48k 字符（`wc -m` 口径；2026-09-26 控制裁定上调上限，原 45k——Task 3/5 的计划强制要素[八要素表、BTF 实测块、五层阐述、五段逐字代码块]自然落点约 40–48k，设计文档 §3.3 同步修订）；分节预算按设计 §3.3：总览 5–6k、指令集 5–6k、映射类型 6–8k、bpf() 内核分析 5–6k、实战（含五层）9–11k、可视化 1.5–2.5k、排错 3–4k、课后任务与总结/参考 3–4k（学习目标/导读约 1–1.5k 计入总预算，不单独设上限）。
5. **叙事**：双视角落「总览（一）、映射类型（三）、实战（五）」三节——各节内用「**教师视角**」「**学生动手**」加粗引导段，检查点用编号列表（2–4 个自测问题 + 答案提示）。本章非渐进阶段章节，「四段式阶段教学节奏」不约束一/二/三/四节；「五、实战」5.2 内以教师视角引入/核心代码原理讲解/学生动手/检查点四段组织（设计 §3.2）。
6. **代码注释**：所有 `.bpf.c` 与 loader C 源文件中文注释，覆盖五类要点：数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。
7. **内核兼容**：helper/特性保守选择，5.10+ 兼容优先；版本相关细节用保守措辞并注明（本机内核 7.0.0-31）。
8. **安全**：本章无 veth/netns/物理网卡风险（第 17 章「勿挂物理网卡」警告措辞**不适用**）；每份脚本头部与 README 的安全警告统一为：「tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。」
9. **构建产物不入库**：`.gitignore` 全局规则已排除 `book/examples/**/vmlinux.h`、`*.skel.h`、`*.o`；新增二进制产物按任务追加条目（Task 1：trace_stat；Task 7：两个练习可执行物）。
10. **任务对应**：每个任务必须引用 tasks.md「Chapter 2」矩阵的唯一任务文本片段；全部任务完成后由 Task 9 统一勾选 tasks.md 的 9 条。
11. **提交纪律**：每个任务以一次 git 提交结束；提交信息格式 `feat(ch02): ...` / `docs(ch02): ...` / `chore(ch02): ...`；从仓库根 `/work/ai/ebpf-book` 执行命令。
12. **工作区**：开始前确认 `git rev-parse HEAD == 5f5bda5a0a24d93eec9c5e40c57d6b41995a8bba`（或基于该提交的工作树）。工作区允许存在 Comet 编排产物（`openspec/changes/generate-ebpf-book/.comet/*`、`.zcodeignore`）的未提交改动——它们**不属于任何 ch02 提交**，每次 `git add` 只加当前任务 Files 列出的路径。

## 现状基线（编写计划时已核实）

| 事实 | 值 |
|---|---|
| HEAD | `5f5bda5a0a24d93eec9c5e40c57d6b41995a8bba`（工作区含 .comet 编排产物改动，见全局约束 12） |
| 章节现状 | 第 2 章正文**不存在**（`book/chapters/` 仅 01/09/17）；`book/examples/chapter-02/` **不存在** |
| 第 1 章回指锚点 | `book/chapters/01-ebpf-overview-and-history.md`「## 实战：最小 tracepoint eBPF 程序」：程序 `hello_exec`、`SEC("tracepoint/syscalls/sys_enter_execve")`、无 map、用 `bpf_printk` 输出到 trace_pipe；章末预告「第 2 章补齐指令集和 map」 |
| 附录 | `book/appendix/a-ebpf-instruction-set.md`（指令集 stub，已建）、`book/appendix/c-distro-btf-build-guide.md`（BTF 构建指南 stub，已建）——正文可交叉引用 |
| 本机工具链 | clang 18.1.3、bpftool 7.7.0、gcc、libbpf-dev 1.3.0（`/usr/include/bpf/bpf_helpers.h` 存在）、内核 7.0.0-31、nproc=16（MAX_CPUS=64 安全） |
| 权限 | 非 root；`/proc/sys/kernel/unprivileged_bpf_disabled` = 2（非特权不可加载） |
| BTF | `/sys/kernel/btf/vmlinux` 存在；sched_switch 上下文经本机 BTF 核实：`struct trace_event_raw_sched_switch { struct trace_entry ent; char prev_comm[16]; pid_t prev_pid; int prev_prio; long prev_state; char next_comm[16]; pid_t next_pid; int next_prio; }`——`prev_comm` 为**定长数组**（非 `__data_loc`），直接读取不越界 |
| 冒烟测试（编写计划时已完成） | 上述 trace_stat BPF 代码在本机 `clang -target bpf` 编译、`bpftool gen skeleton` 生成成功；**skeleton 不导出自定义 map value 类型**（`struct cpu_stat` 需在 loader 侧重新定义）；loader 侧使用 `bpf_map_lookup_elem` 需 `#include <bpf/bpf.h>`（否则隐式声明告警）；整链 gcc 链接通过 |
| 可复用模式 | 第 17 章计划「共享接口契约」的 C6（验证状态）/C7（Makefile 模板）/C8（README 十段）——本计划以同字母契约引用并给出本章替换表（见下） |

## 共享接口契约（跨任务定义，类型一致性以此为准）

各任务实现者只看自己的任务，以下契约必须逐字一致。

### C1. 01-trace-stat 集成示例契约

```c
#define MAX_CPUS 64    /* 教学上限：仅统计 CPU 0–63；两侧各自定义，数值必须一致 */

/* map value：一次统计同时演示「计数」与「上下文对象读取」两个教学点 */
struct cpu_stat {
    __u64 switches;      /* 本 CPU 的 sched_switch 触发次数 */
    __u64 last_next_pid; /* 最近一次被调度切入的进程 pid（来自 ctx 字段读取） */
};
```

- map：`sched_counts`，`BPF_MAP_TYPE_PERCPU_ARRAY`，key `__u32`（CPU 下标），value `struct cpu_stat`，`max_entries MAX_CPUS`。loader 侧**必须重新定义** `struct cpu_stat` 与 `MAX_CPUS`（skeleton 不导出自定义 map value 类型，见现状基线冒烟测试），字段顺序与数值与 BPF 侧一致。
- 程序与 SEC：`SEC("tracepoint/sched/sched_switch") int trace_stat(struct trace_event_raw_sched_switch *ctx)`；skeleton 符号前缀 `trace_stat_bpf`（`skel->progs.trace_stat`、`skel->maps.sched_counts`）。
- 挂载：`trace_stat_bpf__attach(skel)` 自动挂载（tracepoint 从 SEC 完整推断，无需显式参数）。
- 统计口径（正文与 README 同口径）：key=CPU 下标、per-CPU 槽位去写竞争；`bpf_get_smp_processor_id()` 选下标；`MAX_CPUS` 之外的 CPU 事件被防御性丢弃并在注释注明；用户态读回时对 key 的全部 per-CPU 槽位求和（仅写者 CPU 的槽位非零）。

### C2. 练习 1（01-ringbuf-events）契约

```c
/* 事件结构必须 8 字节对齐（ringbuf 硬性要求），24 字节 = 8 的倍数 */
struct sched_event {
    __u64 ts_ns;     /* bpf_ktime_get_ns：内核单调时钟（非墙钟） */
    __u32 prev_pid;  /* 让出 CPU 的进程 */
    __u32 next_pid;  /* 接管 CPU 的进程 */
    __u32 cpu;       /* 事件发生的 CPU（bpf_get_smp_processor_id） */
    __u32 pad;       /* 对齐填充：保持整体 8 字节对齐 */
};
```

- map：`events`，`BPF_MAP_TYPE_RINGBUF`，`max_entries (1 << 24)`（16 MB 教学容量，满则 `bpf_ringbuf_reserve` 返回 NULL、事件静默丢弃）。
- 程序与 SEC：`SEC("tracepoint/sched/sched_switch") int trace_events(struct trace_event_raw_sched_switch *ctx)`；skeleton 前缀 `trace_events_bpf`。
- loader 侧同样**必须重新定义** `struct sched_event`（理由同 C1）。

### C3. 练习 2（02-lru-top-proc）契约

- map：`proc_counts`，`BPF_MAP_TYPE_LRU_HASH`，key `__u32`（pid，被调度切入的进程），value `__u64`（被调度次数），`max_entries 4096`（`#define PROC_MAX 4096`，两侧一致）。LRU 满表时内核**静默淘汰最久未用表项**，插入仍成功（口径与第 17 章阶段 3 一致）。
- 程序与 SEC：`SEC("tracepoint/sched/sched_switch") int top_proc(struct trace_event_raw_sched_switch *ctx)`；skeleton 前缀 `top_proc_bpf`；pid 0（idle/swapper）不入榜。
- loader：`bpf_map_get_next_key` 弱遍历 + `qsort` 降序 + 打印 top-N（N 缺省 10、上限 64）。

### C4. Makefile 模板（沿用第 17 章计划 C7 原文，仅替换注释与目标名）

本章全部 Makefile 使用第 17 章计划（`docs/superpowers/plans/2026-09-22-chapter17-teaching.md` 共享接口契约 C7）的模板原文，即 libbpf-bootstrap 三步：`vmlinux.h`（bpftool 由 /sys/kernel/btf/vmlinux 生成）→ `*.bpf.o`（clang -target bpf）→ `*.skel.h`（bpftool gen skeleton）→ 可执行 loader（gcc -lbpf -lelf -lz）。本章各目录**无共享头文件**，`.bpf.o` 规则依赖仅 `<名>.bpf.c vmlinux.h`，无需追加。替换表：

| 目录 | `<名>` | 可执行目标 |
|---|---|---|
| `01-trace-stat/` | `trace_stat` | `trace_stat` |
| `exercises/01-ringbuf-events/` | `trace_events` | `trace_events` |
| `exercises/02-lru-top-proc/` | `top_proc` | `top_proc` |

模板首行注释统一改为「第 2 章示例构建脚本（libbpf-bootstrap 模式，与第 17 章示例一致）」，并加注释行安全警告（全局约束 8 措辞）。

### C5. README 结构（沿用第 17 章计划 C8 与其 Task 12 Step 3 原文）

- **示例/章级 README 十段结构**：1 标题 + 一句话定位（对应章节小节）　2 演示目标　3 文件说明（表）　4 环境前提　5 编译与运行（命令）　6 预期输出（标注「预期输出示例 / 运行级待验证」）　7 代码讲解要点（中文注释呼应）　8 常见失败点（表：现象/原因与处理）　9 验证状态（按 C6）　10 参考来源。
- **练习 README 防剧透七段结构**：1 `## 任务`（面向学生的任务描述与验收标准）　2 `## 提示`（涉及哪些 helper/map/结构，不给答案）　3 `## 环境前提与构建`　4 `## 预期输出示例`（运行级待验证）　5 `## 参考实现思路`（独立成节，防剧透分界线 `---` 在第 4 节之后）　6 `## 验证状态`　7 `## 参考来源`。

### C6. 验证状态约定（沿用第 17 章计划 C6 原文，去除 Go 相关行）

- **编译验证**：写明「已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过」。
- **待运行验证**：写明「编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例」。

### C7. 章节正文标题（逐字固定，所有正文任务不得改动一二级标题）

```text
# 第 2 章 eBPF 架构与核心概念
## 学习目标
## 本章导读
## 一、eBPF 架构总览
## 二、指令集基础
## 三、映射类型全景
## 四、bpf() 系统调用与内核实现分析
## 五、实战：第一个追踪统计示例
## 可视化：eBPF 程序生命周期与数据通路
## 常见问题与排错
## 课后扩展任务
## 总结
## 本章参考来源
```

### C8. 交叉链接惯例（沿用第 17 章计划 Task 14 惯例）

相对路径链接 + 仓库根路径文本：章首「本章独立示例已全部落地到 [`book/examples/chapter-02/`](../examples/chapter-02/)」；每个正文代码块前后标注对应文件路径（如 `book/examples/chapter-02/01-trace-stat/trace_stat.bpf.c`）；小节末尾「**独立示例**：`../examples/chapter-02/<目录>/`」链接；指令集节链接 `../appendix/a-ebpf-instruction-set.md`；课后任务节链接 exercises/ 各目录。

---

### Task 1: 章级 README、00-env 环境自检与 .gitignore 追加
- [x] Task 1: 章级 README、00-env 环境自检与 .gitignore 追加

**tasks.md 条目**：「Create standalone Chapter 2 examples under `book/examples/chapter-02/` if runnable code is included.」（章级资产部分）

**Files:**
- Create: `book/examples/chapter-02/README.md`
- Create: `book/examples/chapter-02/00-env/check-env.sh`（可执行，`chmod +x`）
- Modify: `.gitignore`（追加一行 `book/examples/chapter-02/01-trace-stat/trace_stat`）

**Interfaces:**
- Consumes: C5（章级 README 十段）、C6（验证状态约定）、全局约束 8（安全警告措辞）。
- Produces: `check-env.sh` 三档结论（编译级/加载级/运行级），供 01-trace-stat README「环境前提」与正文 5.1 第一层引用；`00-env/` 无 Makefile（纯脚本）。

- [x] **Step 1: 创建 `book/examples/chapter-02/README.md`**（十段结构，内容口径如下）

标题「# 第 2 章独立示例资产」+ 定位句（对应正文「五、实战」）。目录结构表：

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | 内核/BTF/工具链/权限分级诊断（无 root 可跑） |
| `01-trace-stat/` | 集成示例 | sched_switch tracepoint + per-CPU 统计：八要素最小闭环 |
| `scripts/` | 编译级验证 | `verify-build.sh` 汇总本目录全部 make |
| `exercises/` | 课后扩展任务 | 2 项完整参考实现 + 防剧透 README |

其余段落：演示目标（八要素逐一落到 01-trace-stat 的哪一行）；学习路径（check-env → 01-trace-stat → 练习）；**安全警告**（全局约束 8 措辞，逐字）；统一构建模式（C4 三步图）；验证状态说明（C6 两条 + 「全量编译结果由 `scripts/verify-build.sh` 生成，摘要见 Task 8 回填」占位句——Task 8 会替换为实际摘要）。

- [x] **Step 2: 创建 `book/examples/chapter-02/00-env/check-env.sh`**（完整内容如下）

```bash
#!/usr/bin/env bash
# 第 2 章示例环境自检（分级诊断）。
# 用法：bash check-env.sh（无 root 也可运行，按「编译级/加载级/运行级」三档给出结论）
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只读检查，不做任何加载。

set -u
fail=0

check() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        printf 'OK    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        fail=1
    fi
}

echo "== 内核与 BTF =="
printf 'kernel: %s\n' "$(uname -r)"
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用，CO-RE 可用）'
else
    echo 'FAIL  内核 BTF 缺失：全部示例无法编译，参见附录 C'
    fail=1
fi

echo "== 工具链（编译级） =="
check 'clang（编译 BPF C）'              clang --version
check 'gcc（编译用户态 loader）'          gcc --version
check 'bpftool（BTF dump / skeleton）'   bpftool version
check 'make'                             make --version
check 'libbpf 头文件（libbpf-dev）'       test -f /usr/include/bpf/bpf_helpers.h

echo "== 权限分级（加载级/运行级） =="
if [ "$(id -u)" -eq 0 ]; then
    echo 'OK    当前为 root：可加载、可挂载 tracepoint'
elif capsh --print 2>/dev/null | grep -q 'cap_bpf'; then
    echo 'OK    具备 CAP_BPF：可加载程序，挂载 tracepoint 还需 CAP_PERFMON/CAP_SYS_ADMIN'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo 未知)"
    echo "WARN  非 root 且无 CAP_BPF（unprivileged_bpf_disabled=${ubd}）："
    echo '      - 可以完成：编译级验证（make / scripts/verify-build.sh）'
    echo '      - 不能完成：加载与挂载 tracepoint、bpftool prog/map 观察（需 sudo）'
fi
if [ -d /sys/kernel/tracing/events/sched/sched_switch ]; then
    echo 'OK    tracefs 可见 sched/sched_switch（运行级挂载目标存在）'
else
    echo 'WARN  tracefs 未见 sched/sched_switch（运行级挂载会失败，编译不受影响）'
fi

echo
if [ "$fail" -eq 0 ]; then
    echo '结论：编译级依赖齐备；加载级/运行级能力见上方权限分级输出。'
else
    echo '结论：存在 FAIL 项，先补齐依赖（Ubuntu: sudo apt install clang llvm gcc bpftool libbpf-dev make）。'
fi
exit "$fail"
```

- [x] **Step 3: 本机验证（无特权可执行）**

Run: `chmod +x book/examples/chapter-02/00-env/check-env.sh && bash book/examples/chapter-02/00-env/check-env.sh`
Expected: 退出码 0；工具链各行 OK；权限分级输出 WARN 段（本机非 root）并明示「可以完成编译级验证」；tracefs 行 OK（本机 7.0 内核存在 sched/sched_switch）。

- [x] **Step 4: 追加 `.gitignore` 并提交**

```bash
printf '%s\n' 'book/examples/chapter-02/01-trace-stat/trace_stat' >> .gitignore
git add book/examples/chapter-02/README.md book/examples/chapter-02/00-env/ .gitignore
git commit -m "feat(ch02): 章级 README 与 00-env 分级环境自检"
```

---

### Task 2: 01-trace-stat 集成示例（trace_stat.bpf.c + trace_stat.c + Makefile + README）

- [x] Task 2: 01-trace-stat 集成示例（trace_stat.bpf.c + trace_stat.c + Makefile + README）

**tasks.md 条目**：「Include an integrated example showing a simple tracing program with map interaction.」＋「Create standalone Chapter 2 examples under `book/examples/chapter-02/` if runnable code is included.」（01-trace-stat 部分）

**Files:**
- Create: `book/examples/chapter-02/01-trace-stat/trace_stat.bpf.c`
- Create: `book/examples/chapter-02/01-trace-stat/trace_stat.c`
- Create: `book/examples/chapter-02/01-trace-stat/Makefile`
- Create: `book/examples/chapter-02/01-trace-stat/README.md`

**Interfaces:**
- Consumes: C1（map/结构/SEC/统计口径）、C4（Makefile 替换表 `<名>=trace_stat`）、C5（README 十段）、C6（验证状态）。
- Produces: 正文 Task 5「五、实战」逐段讲解的唯一代码对象；verify-build.sh 的第一编译项。

- [x] **Step 1: 创建 `trace_stat.bpf.c`**（逐字如下，~60 行）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-trace-stat：sched_switch tracepoint + per-CPU 数组统计。
 * 教学闭环（八要素落点）：程序类型与挂载点（SEC 隐式推断）-> 上下文对象（vmlinux.h
 *   CO-RE 读取）-> 辅助函数选型 -> map 选型（per-CPU array）-> 事件通道（loader 轮询）。
 * 失败模式：无 root 时 open_and_load 失败（预期，见 README）；事件推送形态见练习 1。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define MAX_CPUS 64   /* 教学上限假设：仅统计 CPU 0–63；更多 CPU 的机器需调大并同步 loader */

/* value：一次统计同时演示「计数」与「上下文对象读取」两个教学点 */
struct cpu_stat {
    __u64 switches;      /* 本 CPU 的 sched_switch 触发次数 */
    __u64 last_next_pid; /* 最近一次被调度切入的进程 pid（来自 ctx 字段读取） */
};

/* 选 per-CPU 数组：key=CPU 下标（bpf_get_smp_processor_id 选定），每 CPU 只写自己的
 * 槽位，数据面无锁、零竞争；代价是用户态读回时要对 per-CPU 槽位求和。
 * 数组 map 对越界 key 会在 helper 内部返回 NULL（运行时定界），下方显式检查是为了
 * 把「CPU 下标异常」与「表项异常」区分开（防御式写法，非 verifier 硬性要求）。 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, MAX_CPUS);
    __type(key, __u32);
    __type(value, struct cpu_stat);
} sched_counts SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int trace_stat(struct trace_event_raw_sched_switch *ctx)
{
    __u32 cpu = bpf_get_smp_processor_id();   /* 选型原因：稳定获取当前 CPU，无需解析 ctx */
    if (cpu >= MAX_CPUS)
        return 0;                             /* 防御定界：超出教学上限的 CPU 不统计 */
    struct cpu_stat *p = bpf_map_lookup_elem(&sched_counts, &cpu);
    if (!p)
        return 0;                             /* per-CPU 槽位由内核零初始化，正常不应失败 */
    p->switches++;
    /* tracepoint 上下文字段经 vmlinux.h CO-RE 读取：内核布局变化时 libbpf 重定位。
     * prev_comm/next_comm 为定长数组（本机 BTF 实测 16 字节），直接读不越界；
     * 变长字符串才需要 bpf_probe_read_str（语义讲解见正文 5.2 与排错节）。 */
    p->last_next_pid = ctx->next_pid;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 2: 创建 `trace_stat.c`**（逐字如下；`struct cpu_stat`/`MAX_CPUS` 为 loader 侧重新定义，见 C1）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-trace-stat 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./trace_stat
 * 职责: open_and_load（打开对象 -> verifier 校验 -> JIT）-> attach（tracepoint 由
 *       SEC 名自动推断）-> 每秒打印各 CPU 调度计数 -> Ctrl-C 退出即卸载。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "trace_stat.skel.h"

#define MAX_CPUS 64   /* 与 trace_stat.bpf.c 的 MAX_CPUS 契约一致（skeleton 不导出宏） */

/* 与 trace_stat.bpf.c 中的定义逐字段一致（skeleton 不导出自定义 map value 类型） */
struct cpu_stat {
    __u64 switches;
    __u64 last_next_pid;
};

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

int main(void)
{
    int nr = libbpf_num_possible_cpus();
    if (nr < 1 || nr > MAX_CPUS) {
        fprintf(stderr, "CPU 数 %d 超出教学上限 %d，请同步调大两侧 MAX_CPUS\n",
                nr, MAX_CPUS);
        return 1;
    }

    /* 加载失败常见原因：权限不足（-EPERM）、verifier 拒绝（-EACCES，见正文排错节） */
    struct trace_stat_bpf *skel = trace_stat_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* tracepoint 挂载点可从 SEC("tracepoint/sched/sched_switch") 完整推断，
       无需像 XDP 那样显式传接口；失败返回负数错误码 */
    if (trace_stat_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 目标是否存在见 ../00-env/check-env.sh）\n");
        trace_stat_bpf__destroy(skel);
        return 1;
    }
    printf("已挂载 tracepoint/sched/sched_switch，每秒打印，Ctrl-C 退出。\n");

    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.sched_counts);

    while (!exiting) {
        __u64 total = 0;
        printf("--- 各 CPU 调度次数 ---\n");
        /* key 遍历 CPU 下标：只有 CPU c 写 key c，读回该 key 的 per-CPU 槽位中
           仅下标 c 非零——对全部槽位求和即该 CPU 的计数（per-CPU 读回语义见正文 5.2） */
        for (__u32 cpu = 0; cpu < MAX_CPUS; cpu++) {
            struct cpu_stat buf[MAX_CPUS] = {};
            if (bpf_map_lookup_elem(fd, &cpu, buf))
                continue;             /* 理论不发生：max_entries 覆盖全部 key */
            __u64 sum = 0;
            for (int i = 0; i < nr; i++)
                sum += buf[i].switches;
            if (sum)
                printf("cpu%-2u switches=%-10llu last_next_pid=%llu\n",
                       cpu, (unsigned long long)sum,
                       (unsigned long long)buf[cpu].last_next_pid);
            total += sum;
        }
        printf("total=%llu\n", (unsigned long long)total);
        sleep(1);
    }

    trace_stat_bpf__destroy(skel);   /* 关闭 fd + 解引用：引用计数归零，对象随之释放 */
    return 0;
}
```

- [x] **Step 3: 创建 `Makefile`** —— 使用共享契约 C4 模板，替换 `<名>=trace_stat`、目标 `trace_stat`；`.bpf.o` 依赖仅 `trace_stat.bpf.c vmlinux.h`。

- [x] **Step 4: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-02/01-trace-stat`
Expected: 退出码 0，生成 `trace_stat`；`clang -target bpf` 与 `bpftool gen skeleton` 无 error（vmlinux.h 的 `-Wmissing-declarations` warning 属正常）。失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 5: 创建 `README.md`** —— 按 C5 十段结构。关键口径：
  - 演示目标：第一个「加载→verifier→JIT→挂载→触发→读回」的 map 交互闭环；八要素各对应代码哪一处。
  - 文件说明表：`trace_stat.bpf.c` / `trace_stat.c` / `Makefile`。
  - 环境前提：指向 `../00-env/check-env.sh` 三档结论。
  - 编译与运行：`make`；`sudo ./trace_stat`；触发：无需专门负载，系统空闲即持续触发 sched_switch；Ctrl-C 退出即卸载。
  - 预期输出示例（标注运行级待验证）：

```text
已挂载 tracepoint/sched/sched_switch，每秒打印，Ctrl-C 退出。
--- 各 CPU 调度次数 ---
cpu0  switches=1234       last_next_pid=1823
cpu1  switches=987        last_next_pid=910
total=2221
```

  - 代码讲解要点：SEC 推断挂载点、per-CPU「key 选 CPU + 槽位无竞争」双层去竞争、`last_next_pid` 的 CO-RE 读取、读回求和语义。
  - 常见失败点表：非 root 加载失败（预期，指向 check-env.sh）；`vmlinux.h` 缺失（make 自动生成失败=无 BTF）；CPU 数超 64（loader 启动即报错）。
  - 验证状态：按 C6。
  - 参考来源：内核文档 BPF/tracepoint、libbpf-bootstrap、`bpf_helpers.h`。

- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-02/01-trace-stat/
git commit -m "feat(ch02): 01-trace-stat 集成示例（BPF+loader+Makefile+README）"
```

---

### Task 3: 正文「学习目标/本章导读/一、架构总览/二、指令集基础」

- [x] Task 3: 正文「学习目标/本章导读/一、架构总览/二、指令集基础」

**tasks.md 条目**：「Write full chapter prose explaining programs, maps, helpers, program types, attach points, context objects, event channels, and lifecycle.」（前半：程序/映射/辅助函数/程序类型/挂载点/上下文对象六要素；事件通道与生命周期后半由 Task 5 承接）＋「Explain instruction-set basics: 64-bit RISC model, registers, calling convention, and instruction encoding.」（交叉要求拆分落地）

**Files:**
- Create: `book/chapters/02-ebpf-architecture-and-core-concepts.md`

**Interfaces:**
- Consumes: C7（标题逐字固定）；第 1 章回指锚点（现状基线表）；附录 A 路径。
- Produces: 章节文件与前四节成稿；「一」节中 1.7/1.8 两小节只写要素讲解，其与 01-trace-stat 的具体对照由 Task 5 的 5.2 回扣（本任务先写通识形态）。

- [x] **Step 1: 写「# 第 2 章」题头、「学习目标」「本章导读」（约 1–1.5k）**
  - 学习目标：成果导向 6–8 条（能说出八要素及其协作；能手工解码一条 `bpf_insn`；能按决策表为场景选 map 类型；能讲清 `bpf()` 对象的 fd 所有权/引用计数/pin 生命周期；能独立构建并解读 01-trace-stat 输出；能完成 2 项课后练习）。
  - 本章导读：承接第 1 章章末预告「第 2 章补齐指令集和 map」；学习路线图——先全景（一）→ 两个支柱（指令集二/映射三）→ 内核如何管理对象（四）→ 动手（五）→ 可视化/排错/课后练习；每段点明对应节号。
- [x] **Step 2: 写「一、eBPF 架构总览」（5–6k）**——小节锚点固定为 `### 1.1`–`### 1.9`：
  - 1.1 程序（program）：经 verifier 的受限 C 子集→字节码；**教师视角**段（类比：内核里的「外包脚手架」；误区：eBPF 不是内核模块）。
  - 1.2 映射（map）：内核态/用户态共享数据对象；第 1 章示例**没有 map**（用 `bpf_printk` 直出 trace_pipe）——明确「本章补上这块拼图」的承接点，预告 01-trace-stat 的 `sched_counts`。
  - 1.3 辅助函数（helper）：内核提供的受控能力面；`bpf_get_smp_processor_id`/`bpf_map_lookup_elem` 两例；helper 可用面由程序类型决定（为 1.4 伏笔）。
  - 1.4 程序类型（program type）：决定上下文结构、helper 集合、返回值语义；tracepoint 类型展开；完整类型谱系指向第 4 章（交叉引用）。
  - 1.5 挂载点（attach point）：SEC 名隐式推断 vs 显式 attach 两模式；tracepoint 从 SEC 完整推断，XDP 需要显式接口（第 17 章预告式交叉引用）。
  - 1.6 上下文对象（context）：tracepoint ctx 经 vmlinux.h CO-RE 读取；给本机 BTF 实测的 `trace_event_raw_sched_switch` 字段（现状基线已核实），注明「字段布局随内核版本变化，CO-RE 负责重定位」。
  - 1.7 事件通道（event channel）：演进线——`bpf_printk`→trace_pipe（第 1 章）→ 轮询 map（本章 01）→ ring buffer 推送（练习 1）；各自延迟/开销/丢失语义一句话。
  - 1.8 生命周期（lifecycle）：加载→验证→JIT→挂载→触发→卸载六步总览；fd 引用计数一句话（详析留给四节与可视化节）。
  - 1.9 八要素对照第 1 章示例：表格（要素/第 1 章 `hello_exec` 代码位置/本章扩展），落实「把概念钉在读者已见过的代码上」；**学生动手**段（重读第 1 章示例并对照表格自查）+ 检查点（3 问 + 提示：为什么第 1 章不需要 map？SEC 名如果写错会发生在编译期还是运行期？上下文字段是谁定义的？）。
- [x] **Step 3: 写「二、指令集基础」（5–6k）**——小节锚点固定为 `### 2.1`–`### 2.4`：
  - 2.1 64 位 RISC 模型：10 个通用寄存器 r0–r9 与调用约定（r0 返回值；r1–r5 传参，多余参数走栈；r6–r9 callee-saved）+ r10 只读栈帧指针；为什么选 RISC——指令语义有限，verifier 才能静态分析（为第 3 章伏笔）。
  - 2.2 8 字节指令编码：`struct bpf_insn`（`opcode:8`、`dst_reg:4|src_reg:4`、`off:16`、`imm:32`，`include/uapi/linux/bpf.h`）；给一个手工解码示例（`r0 = 0` 的 64 位立即数加载：opcode `0xb7`、dst=0、imm=0），逐字段说明。
  - 2.3 常见指令类别：BPF_ALU/BPF_JMP/BPF_LDX/BPF_STX/原子类概览（各 2–3 句 + 1 条编码示例）；**不展开完整 ISA 表**——指向附录 A `book/appendix/a-ebpf-instruction-set.md`。
  - 2.4 观察法：`bpftool prog dump xlated name <prog>` 与 `dump jit` 的用途与读法；本机无特权不能加载程序，示例输出标注「预期输出示例 / 运行级待验证」；讲解 xlated 与 jit 视图差异。
- [x] **Step 4: 核对（硬性）**

Run: `grep -c "^## " book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "^### 1\." book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "^### 2\." book/chapters/02-ebpf-architecture-and-core-concepts.md && wc -m book/chapters/02-ebpf-architecture-and-core-concepts.md`
Expected: `4`（学习目标/导读/一/二）、`9`、`4`；`wc -m ≥ 12000`（本任务中段值参考；Task 8 终检 35–48k）。

- [x] **Step 5: 提交**

```bash
git add book/chapters/02-ebpf-architecture-and-core-concepts.md
git commit -m "docs(ch02): 学习目标/导读/架构总览/指令集基础"
```

---

### Task 4: 正文「三、映射类型全景」+「四、bpf() 系统调用与内核实现分析」

- [x] Task 4: 正文「三、映射类型全景」+「四、bpf() 系统调用与内核实现分析」

**tasks.md 条目**：「Explain map types including hash, array, LRU hash, per-CPU maps, perf event array, ring buffer, LPM trie, and sockmap where relevant.」＋「Add kernel implementation analysis for `bpf()` syscall object creation, fd ownership, reference counting, and pinning.」

**Files:**
- Modify: `book/chapters/02-ebpf-architecture-and-core-concepts.md`

**Interfaces:**
- Consumes: C7 标题；Task 3 成稿；1.7 事件通道演进线（3.5/3.6 两 card 呼应）；第 17 章（LPM）/第 19 章（sockmap）交叉引用；第 3 章（verifier）与第 5 章（子系统源码结构）交叉引用。
- Produces: 3.9 选型决策表（正文 5.2 与排错节引用）；四节的 fd/引用计数/pin 结论（可视化节对象关系图与检查点引用）。

- [x] **Step 1: 写「三、映射类型全景」（6–8k）**——每类一个 card（`### 3.1`–`### 3.8`），每个 card 固定四要素：**语义 / 典型场景 / 容量与并发特征 / 选型理由**：
  - 3.1 hash（`BPF_MAP_TYPE_HASH`）：全等 key O(1)；精确规则/流表；`max_entries` 固定、满则插入失败（与 3.3 LRU 对比伏笔）。
  - 3.2 array（`BPF_MAP_TYPE_ARRAY`）：下标 key、预分配、不可删除元素；统计下标/配置表；per-CPU 读回聚合语义预告（3.4）。
  - 3.3 LRU hash（`BPF_MAP_TYPE_LRU_HASH`）：满表**静默淘汰最久未用**、插入仍成功；观测上表现为「旧表项消失」；口径与第 17 章阶段 3 一致（交叉引用）。
  - 3.4 per-CPU 映射（`PERCPU_ARRAY`/`PERCPU_HASH`）：每 CPU 独立副本、数据面无锁；读回需聚合；「key 选 CPU + per-CPU 槽位」双层去竞争即本章 01-trace-stat 的做法（回指）。
  - 3.5 perf event array（`BPF_MAP_TYPE_PERF_EVENT_ARRAY`）：历史事件通道；per-CPU 环形缓冲、需 perf_event_open 逐 CPU 挂接、无 builtin 满表丢弃计数；为何新代码优先 ring buffer（与 1.7 演进线呼应）。
  - 3.6 ring buffer（`BPF_MAP_TYPE_RINGBUF`）：跨 CPU 共享环形缓冲；reserve/submit 两段式；8 字节对齐硬性要求；满则 reserve 失败（练习 1 预告）。
  - 3.7 LPM trie：最长前缀匹配；`BPF_F_NO_PREALLOC` 必需；**机制细节指向第 17 章**（交叉引用，不抢戏）。
  - 3.8 sockmap：socket 域映射与消息重定向加速；**机制细节指向第 19 章**（交叉引用）；本节只给「它是什么、何时想到它」。
  - 3.9 选型决策表与 bpftool 观察（`### 3.9`）：决策表（需求→推荐类型→理由，至少覆盖：精确查找/统计计数/容量受限字典/事件流/前缀匹配/socket 加速）；`bpftool map show` / `map dump` 观察法（预期输出示例 + 运行级待验证标注）；**教师视角**段（选型误用是最常见事故源，指向排错节）+ **学生动手**段（对 8 类各写一句「若用错类型会怎样」）+ 检查点（3 问 + 提示）。
- [x] **Step 2: 写「四、bpf() 系统调用与内核实现分析」（5–6k）**——小节锚点 `### 4.1`–`### 4.5`；纯讲解 + 内核路径引用，不要求内核源码级调试环境；对随版本演进的函数名用保守措辞（「以具体内核版本源码为准」）：
  - 4.1 `bpf(2)` 入口与命令集概览：`include/uapi/linux/bpf.h` 的 `enum bpf_cmd`（BPF_MAP_CREATE/BPF_OBJ_PIN/BPF_OBJ_GET/BPF_PROG_LOAD/BPF_PROG_ATTACH…）；`union bpf_attr` 按命令复用；`kernel/bpf/syscall.c` 是全部命令的入口（交叉引用第 5 章）。
  - 4.2 BPF_MAP_CREATE：`kernel/bpf/syscall.c` 中 map 创建路径概览（参数校验→按 `map_type` 找到注册的 `bpf_map_ops`→分配对象→绑定 fd）；fd 所有权语义：fd 从进程视角持有对象，`struct file` 关联 `bpf_map`。
  - 4.3 BPF_PROG_LOAD：加载路径概览（license 检查→**verifier**（指向第 3 章）→JIT（架构相关，保守措辞）→`bpf_prog` 对象与 fd）；`trace_stat` 的加载即走此路径（回指）。
  - 4.4 引用计数与生命周期钩子：`bpf_map`/`bpf_prog` 的原子引用计数；`bpf_map_put`/`bpf_prog_put` 在 fd 关闭、进程退出、最后一个引用释放时触发对象回收；attach 后 link/prog/map 的引用链（谁持有谁）；呼应 01-trace-stat「Ctrl-C 退出即卸载」。
  - 4.5 pin 与 bpffs：`BPF_OBJ_PIN`/`BPF_OBJ_GET`；pin 在 bpffs（`/sys/fs/bpf`）上创建持久引用，对象跨越进程生命周期存活；对照 01-trace-stat（不 pin，随进程消失）与第 17 章阶段 5（pin map/prog/link，Go 控制面跨进程接管）两种形态；**学生动手**段（画出 trace_stat 进程退出前后的对象引用变化）+ 检查点（3 问 + 提示，含「为什么 close(fd) 不一定立刻释放对象」）。
- [x] **Step 3: 核对（硬性）**

Run: `grep -c "^### 3\." book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "^### 4\." book/chapters/02-ebpf-architecture-and-core-concepts.md && wc -m book/chapters/02-ebpf-architecture-and-core-concepts.md`
Expected: `9`、`5`；`wc -m ≥ 23000`（中段值参考）。

- [x] **Step 4: 提交**

```bash
git add book/chapters/02-ebpf-architecture-and-core-concepts.md
git commit -m "docs(ch02): 映射类型全景与 bpf() 内核实现分析"
```

---

### Task 5: 正文「五、实战：第一个追踪统计示例」（五层环境紧凑版 + 代码走读 + 双视角 + 检查点）

- [x] Task 5: 正文「五、实战：第一个追踪统计示例」（五层环境紧凑版 + 代码走读 + 双视角 + 检查点）

**tasks.md 条目**：「Write full chapter prose explaining programs, maps, helpers, program types, attach points, context objects, event channels, and lifecycle.」（后半：事件通道与生命周期在实战中的落点）＋「Include an integrated example showing a simple tracing program with map interaction.」（正文呈现部分——代码块与 01-trace-stat 文件一一对应）

**Files:**
- Modify: `book/chapters/02-ebpf-architecture-and-core-concepts.md`

**Interfaces:**
- Consumes: Task 2 的 01-trace-stat 全部文件（代码块逐字对应）；`../00-env/check-env.sh` 三档结论；C6 验证状态三条；C7 标题。
- Produces: 5.1 的验证状态约定表（Task 8 审计依据）；「五」节末「独立示例」链接（C8）。

- [x] **Step 1: 写 `### 5.1 五层环境（紧凑版）`（3–4k）**——五层用 `#### 第一层：依赖环境` … `#### 第五层：运行效果` 定长小节，口径与第 17 章 1.1–1.5 一致但面向单机 tracepoint 场景：
  - 第一层 依赖环境：内核 5.10+ 与 `CONFIG_DEBUG_INFO_BTF` 要求；权限矩阵表（root / CAP_BPF+CAP_PERFMON(+CAP_SYS_ADMIN) / 非 root 三档 × 可编译/可加载/可观察三列）；工具清单与检查方法——引用 `bash book/examples/chapter-02/00-env/check-env.sh` 及其三档结论解读（本机 `unprivileged_bpf_disabled=2` 语境示例输出，标「本地验证」）。
  - 第二层 编译环境：clang/LLVM 18.x、libbpf-dev（`bpf_helpers.h`）、bpftool 7.x、make；各版本验证命令（本机实测输出标「本地验证」）；`apt install` 一行说明。
  - 第三层 部署环境：**无 veth/netns 拓扑**——单机 tracepoint，唯一前置是 tracefs 可见目标事件；**安全警告段**（全局约束 8 措辞，逐字）+ 与第 17 章「勿挂物理网卡」的差异说明（本章不存在该风险面）。
  - 第四层 运行环境：`sudo ./trace_stat` 启动、Ctrl-C 卸载；「最简控制面就是 loader 本身」——无规则下发、无 pin（对照 4.5）；`bpftool prog show` / `map dump` 观察命令（运行级待验证）。
  - 第五层 运行效果：各 CPU 计数增长、`total` 汇总、`last_next_pid` 变化的预期输出示例（标注运行级待验证）；**验证状态约定表**——C6 三条逐字引入（编译验证/待运行验证/预期输出示例的标注规则）。
- [x] **Step 2: 写 `### 5.2 代码走读`（6–7k）**——四段组织：
  - **教师视角引入**：为什么选 sched_switch + per-CPU array（八要素全部可见的最小闭环）；读者容易卡住的两处——「为什么读回要按槽位求和」「verifier 为什么直接放行 ctx 读取」。
  - 核心代码原理讲解：贴 `book/examples/chapter-02/01-trace-stat/trace_stat.bpf.c` 全文逐段讲解（SEC 推断、`cpu_stat` 字段语义、map 选型四问[为什么 per-CPU/为什么 array 而非 hash/为什么 key 是 CPU/容量为何 64]、`bpf_get_smp_processor_id` 选型、`MAX_CPUS` 防御定界与数组 map 运行时定界的关系、CO-RE ctx 读取）；**`bpf_probe_read_str` 语义讲解**——`prev_comm` 是定长数组可直接读，变长字符串才需要它（NUL 终止语义与截断行为，保守措辞注明版本差异）；贴 `trace_stat.c` 逐段讲解（open_and_load→attach→轮询→per-CPU 读回求和→destroy 即卸载）。
  - **学生动手**：`make`、`sudo ./trace_stat`、观察输出、Ctrl-C；`bpftool prog show` / `bpftool map dump name sched_counts`（预期输出示例 + 运行级待验证）；动手清单用 checkbox。
  - 检查点：3 问 + 提示——(1) 为什么 key 用 CPU 下标还要用 per-CPU map？（提示：两层去竞争各自挡住什么）(2) loader 读回的 `buf` 里为什么只有下标 `cpu` 的槽位非零？(3) Ctrl-C 之后程序去了哪里？（提示：link 随进程销毁、引用计数归零——衔接 4.4）。
- [x] **Step 3: 代码块对应关系核对（硬性）**

Run: `grep -c "examples/chapter-02/01-trace-stat/" book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "教师视角" book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "学生动手" book/chapters/02-ebpf-architecture-and-core-concepts.md && wc -m book/chapters/02-ebpf-architecture-and-core-concepts.md`
Expected: `≥ 2`（两份源文件路径标注各至少一次）；`≥ 3`（一/三/五三节均有）；`≥ 3`；`wc -m ≥ 31000`（中段值参考）。

- [x] **Step 4: 提交**

```bash
git add book/chapters/02-ebpf-architecture-and-core-concepts.md
git commit -m "docs(ch02): 五层环境紧凑版与实战代码走读"
```

---

### Task 6: 正文「可视化/常见问题与排错/课后扩展任务/总结/本章参考来源」

- [ ] Task 6: 正文「可视化/常见问题与排错/课后扩展任务/总结/本章参考来源」

**tasks.md 条目**：「Add text visualization showing programs, maps, hooks, loader, and user-space control plane.」＋「Add troubleshooting, summary, and references.」

**Files:**
- Modify: `book/chapters/02-ebpf-architecture-and-core-concepts.md`

**Interfaces:**
- Consumes: Task 3–5 成稿；四节 fd/引用计数/pin 结论（可视化图 2 素材）；3.9 决策表与练习 1（排错节「轮询 vs 事件流」呼应）；Task 7 未落地前先写好目录约定（练习表指向 exercises/）。
- Produces: 全部 12 个 `##` 一级小节齐备（C7 收齐）；课后任务表（Task 7 README 总览表与其逐字一致）。

- [ ] **Step 1: 写「可视化：eBPF 程序生命周期与数据通路」（1.5–2.5k）**——两张 text 图（` ```text ` 代码块，格式遵循 `book/STYLE.md` 可视化节）：
  - 图 1 生命周期全链路：loader → `bpf(BPF_PROG_LOAD)` → verifier → JIT → 挂载 tracepoint → 事件触发 → `sched_counts` 累计 → loader 每秒轮询 → Ctrl-C → 引用释放卸载（每步标注对应小节号）。
  - 图 2 对象/fd/pin 关系：`bpf_map`/`bpf_prog` 内核对象、进程 fd 表、bpffs pin 三者引用关系；非 pin 时「进程退出→引用归零→对象释放」与 pin 时「bpffs 持有活引用」两条路径（呼应 4.5 与第 17 章阶段 5）。
  - 图后各配 2–3 段「怎么读这张图」的讲解（禁止裸图）。
- [ ] **Step 2: 写「常见问题与排错」（3–4k）**——四组，每组「现象/原因与处理」表：
  - load 失败权限表：`-EPERM`（非 root、`unprivileged_bpf_disabled=2`）、`-EACCES`（verifier 拒绝/license 不兼容）、verifier 日志读法示意（详析指向第 3 章）。
  - map 选型误用：hash 当计数器（锁竞争）、array 当字典（越界返回 NULL 被当成功）、LRU 满表静默淘汰被误解为「插入失败」、per-CPU 读回不聚合（漏算 nr-1 份）。
  - tracepoint 上下文字段版本差异：字段布局随内核版本变化、CO-RE 重定位机制、`prev_comm` 数组长度差异的保守措辞、字段缺失时的编译期表现。
  - 轮询 vs 事件流取舍：延迟/开销/丢失语义/实现复杂度四列对照表——与练习 1 呼应。
- [ ] **Step 3: 写「课后扩展任务」（表格 + 防剧透说明段）**——固定表格（Task 7 的 exercises/README.md 总览表须与此逐字一致）：

```markdown
| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 事件通道 | 把 01-trace-stat 的轮询改为 ring buffer 事件流（reserve/submit + `ring_buffer__poll`） | `book/examples/chapter-02/exercises/01-ringbuf-events/` |
| 2 | 映射类型 | 用 LRU hash 记录每进程被调度次数并打印 top-N（体验容量与静默淘汰） | `book/examples/chapter-02/exercises/02-lru-top-proc/` |
```

  加防剧透说明段：先自行实现再看参考；README 结构（任务在前、思路独立成节放后部）。
- [ ] **Step 4: 写「总结」（0.8–1.2k）**：八要素回顾（每要素一句话 + 回指小节号）；通往第 3 章（verifier：为什么 5.2 的每条注释都在「喂」verifier）的桥。
- [ ] **Step 5: 写「本章参考来源」**：按 `book/STYLE.md` 格式列——Linux kernel documentation（BPF subsystem / instruction set / maps / helpers / BTF）、ebpf.io、Andrii Nakryiko（BTF/CO-RE 文章）、libbpf 与 libbpf-bootstrap 文档、ebpf.top BPF 入门系列。
- [ ] **Step 6: 结构收齐核对（硬性）**

Run: `grep -c "^## " book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "计划\|建议\|后续补齐\|占位" book/chapters/02-ebpf-architecture-and-core-concepts.md | cat`
Expected: `12`（C7 全部一级小节齐备）；占位式表述 0 命中（「预期输出」含「预期」不算——grep 词表不含「预期」）。

- [ ] **Step 7: 提交**

```bash
git add book/chapters/02-ebpf-architecture-and-core-concepts.md
git commit -m "docs(ch02): 可视化/排错/课后任务/总结/参考来源"
```

---

### Task 7: 练习参考实现（01-ringbuf-events、02-lru-top-proc）与 exercises 总览

- [ ] Task 7: 练习参考实现（01-ringbuf-events、02-lru-top-proc）与 exercises 总览

**tasks.md 条目**：「Create standalone Chapter 2 examples under `book/examples/chapter-02/` if runnable code is included.」（exercises 部分）；同时落实 `book/STYLE.md` 实战模板规则 4（完整可编译参考实现 + 防剧透 README + 验证状态标注）。

**Files:**
- Create: `book/examples/chapter-02/exercises/README.md`
- Create: `book/examples/chapter-02/exercises/01-ringbuf-events/trace_events.bpf.c`
- Create: `book/examples/chapter-02/exercises/01-ringbuf-events/trace_events.c`
- Create: `book/examples/chapter-02/exercises/01-ringbuf-events/Makefile`
- Create: `book/examples/chapter-02/exercises/01-ringbuf-events/README.md`
- Create: `book/examples/chapter-02/exercises/02-lru-top-proc/top_proc.bpf.c`
- Create: `book/examples/chapter-02/exercises/02-lru-top-proc/top_proc.c`
- Create: `book/examples/chapter-02/exercises/02-lru-top-proc/Makefile`
- Create: `book/examples/chapter-02/exercises/02-lru-top-proc/README.md`
- Modify: `.gitignore`（追加 `book/examples/chapter-02/exercises/01-ringbuf-events/trace_events` 与 `book/examples/chapter-02/exercises/02-lru-top-proc/top_proc`）

**Interfaces:**
- Consumes: C2/C3 契约；C4 Makefile 替换表（`trace_events`/`top_proc`）；C5 练习 README 防剧透七段；C6 验证状态；Task 6 课后任务表（总览表逐字一致）。
- Produces: verify-build.sh 的两个练习编译项；练习 README 的「参考实现思路」节与正文排错节「轮询 vs 事件流」呼应。

- [ ] **Step 1: 创建 `exercises/01-ringbuf-events/trace_events.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 参考实现：sched_switch 事件流（ring buffer 推送）。
 * 与 01-trace-stat 的对照：轮询（loader 定时拉）vs 事件流（数据面有事件就推）。
 * 失败模式：ringbuf 满时 reserve 返回 NULL——观测程序必须静默丢弃，
 * 不得阻塞内核路径反压（教学取舍讲解见 README）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* 事件结构必须 8 字节对齐（ringbuf 硬性要求），24 字节 = 8 的倍数 */
struct sched_event {
    __u64 ts_ns;     /* bpf_ktime_get_ns：内核单调时钟（非墙钟） */
    __u32 prev_pid;  /* 让出 CPU 的进程 */
    __u32 next_pid;  /* 接管 CPU 的进程 */
    __u32 cpu;       /* 事件发生的 CPU（bpf_get_smp_processor_id） */
    __u32 pad;       /* 对齐填充：保持整体 8 字节对齐 */
};

/* 选 ringbuf：跨 CPU 共享的高吞吐事件通道，用户态轮询消费；16MB 教学容量（满则丢事件） */
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int trace_events(struct trace_event_raw_sched_switch *ctx)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return 0;   /* 满表丢弃：观测不得阻塞调度路径（与轮询取舍讲解呼应） */
    e->ts_ns = bpf_ktime_get_ns();
    e->prev_pid = ctx->prev_pid;
    e->next_pid = ctx->next_pid;
    e->cpu = bpf_get_smp_processor_id();
    e->pad = 0;
    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 2: 创建 `exercises/01-ringbuf-events/trace_events.c`**（逐字如下；`struct sched_event` 为 loader 侧重新定义，见 C2）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 用户态 loader：ring_buffer__poll 事件循环。
 * 用法: sudo ./trace_events [打印条数上限，缺省 50]
 * 职责: open_and_load -> attach（SEC 推断）-> ring_buffer 轮询消费事件。
 * 注意: sched/sched_switch 事件速率极高，16MB 缓冲打满后新事件被静默丢弃，
 *       属预期行为（取舍讲解见 README）。
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "trace_events.skel.h"

/* 与 trace_events.bpf.c 中的定义逐字段一致（skeleton 不导出自定义 map value 类型） */
struct sched_event {
    __u64 ts_ns;
    __u32 prev_pid;
    __u32 next_pid;
    __u32 cpu;
    __u32 pad;
};

static volatile sig_atomic_t exiting = 0;
static long printed = 0;
static long limit = 50;

static void handle_sig(int sig) { exiting = 1; }

/* 事件回调：data 指向 ringbuf 租借给用户态的内存，返回后由框架自动归还 */
static int on_event(void *ctx, void *data, size_t len)
{
    if (printed >= limit)      /* 已达打印上限：丢弃积压事件，主循环随即退出 */
        return 0;
    const struct sched_event *e = data;
    printf("cpu%-2u %llu ns: prev=%u next=%u\n",
           e->cpu, (unsigned long long)e->ts_ns, e->prev_pid, e->next_pid);
    printed++;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        limit = atol(argv[1]);
    struct trace_events_bpf *skel = trace_events_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }
    if (trace_events_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 见 ../../00-env/check-env.sh）\n");
        trace_events_bpf__destroy(skel);
        return 1;
    }
    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.events),
                                              on_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "ring_buffer__new 失败\n");
        trace_events_bpf__destroy(skel);
        return 1;
    }
    signal(SIGINT, handle_sig);
    while (!exiting && printed < limit) {
        int n = ring_buffer__poll(rb, 100);  /* 100ms 超时轮询：保证 Ctrl-C 及时生效 */
        if (n < 0 && n != -EINTR)
            break;
    }
    ring_buffer__free(rb);
    trace_events_bpf__destroy(skel);
    return 0;
}
```

- [ ] **Step 3: 创建 `exercises/02-lru-top-proc/top_proc.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 参考实现：LRU hash 记录每进程被调度次数（pid -> 计数）。
 * LRU 容量语义：满表时内核静默淘汰最久未用表项，插入仍成功——
 * 观测上表现为「旧进程从榜单消失」（口径与第 17 章阶段 3 一致）。
 * 失败模式：BPF_NOEXIST 并发竞争失败（-EEXIST）时本次调度不计数（教学近似，见注释）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define PROC_MAX 4096   /* LRU 容量：教学机典型进程远少于此；调小可观察淘汰行为 */

/* 选 LRU hash：进程集合不可预知且规模有上界诉求，满表自动淘汰而非插入失败 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, PROC_MAX);
    __type(key, __u32);      /* pid：被调度切入的进程 */
    __type(value, __u64);    /* 被调度次数 */
} proc_counts SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int top_proc(struct trace_event_raw_sched_switch *ctx)
{
    __u32 pid = ctx->next_pid;
    if (!pid)
        return 0;   /* pid 0 = idle/swapper：不入榜，否则榜单永远被刷屏 */
    __u64 *c = bpf_map_lookup_elem(&proc_counts, &pid);
    if (c) {
        (*c)++;
        return 0;
    }
    __u64 one = 1;
    bpf_map_update_elem(&proc_counts, &pid, &one, BPF_NOEXIST);
    /* -EEXIST = 另一 CPU 先插入：本事件不累计（近似）。LRU 满表不走此失败分支——
       内核静默淘汰最久未用项后插入仍成功，只能靠观察旧表项消失来发现。 */
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 4: 创建 `exercises/02-lru-top-proc/top_proc.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 用户态 loader：每 2 秒遍历 LRU 表、排序、打印被调度次数 top-N。
 * 用法: sudo ./top_proc [N]（N 缺省 10，教学上限 64）
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "top_proc.skel.h"

#define TOP_MAX 64

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

struct entry { __u32 pid; __u64 count; };

static int cmp_desc(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->count < y->count)
        return 1;
    if (x->count > y->count)
        return -1;
    return 0;
}

/* 弱遍历：LRU 表项可能在遍历期间被静默淘汰（与第 17 章阶段 3 口径一致），读到什么算什么 */
static int snapshot(int fd, struct entry *out, int max)
{
    __u32 cur = 0, next;          /* pid 0 永不入榜，可作遍历起点哨兵 */
    __u64 v;
    int n = 0;
    while (n < max && bpf_map_get_next_key(fd, &cur, &next) == 0) {
        if (bpf_map_lookup_elem(fd, &next, &v) == 0 && v > 0) {
            out[n].pid = next;
            out[n].count = v;
            n++;
        }
        cur = next;
    }
    return n;
}

int main(int argc, char **argv)
{
    int top_n = (argc > 1) ? atoi(argv[1]) : 10;
    if (top_n < 1 || top_n > TOP_MAX)
        top_n = 10;

    struct top_proc_bpf *skel = top_proc_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }
    if (top_proc_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 见 ../../00-env/check-env.sh）\n");
        top_proc_bpf__destroy(skel);
        return 1;
    }
    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.proc_counts);

    while (!exiting) {
        struct entry e[TOP_MAX];
        int n = snapshot(fd, e, TOP_MAX);
        qsort(e, n, sizeof(e[0]), cmp_desc);
        printf("--- 每进程被调度次数 top-%d ---\n", top_n);
        for (int i = 0; i < n && i < top_n; i++)
            printf("%-7d %llu\n", e[i].pid, (unsigned long long)e[i].count);
        sleep(2);
    }

    top_proc_bpf__destroy(skel);
    return 0;
}
```

- [ ] **Step 5: 创建两个 `Makefile`** —— C4 模板替换：练习 1 `<名>=trace_events`、目标 `trace_events`；练习 2 `<名>=top_proc`、目标 `top_proc`；`.bpf.o` 依赖均仅 `<名>.bpf.c vmlinux.h`。

- [ ] **Step 6: 创建两个练习 `README.md`** —— 按 C5 防剧透七段结构。关键口径：
  - 练习 1 `## 提示`：涉及 `BPF_MAP_TYPE_RINGBUF`、`bpf_ringbuf_reserve`/`bpf_ringbuf_submit`、`ring_buffer__new`/`ring_buffer__poll`；事件结构 8 字节对齐；满表丢弃语义。`## 任务` 验收标准：加载后能看到带 CPU 号/时间戳/pid 对的事件流；能说明与轮询的取舍。`## 参考实现思路`（分界线后）：reserve→填字段→submit 三步、`ring_buffer__poll` 超时轮询保证 Ctrl-C、与 01-trace-stat 的差异点对照表。
  - 练习 2 `## 提示`：涉及 `BPF_MAP_TYPE_LRU_HASH`、`bpf_map_get_next_key` 遍历、`qsort`；LRU 静默淘汰；pid 0 过滤。`## 任务` 验收标准：top-N 榜单随系统负载变化；能解释「旧 pid 从榜单消失」的两种可能（进程退出 / LRU 淘汰）。`## 参考实现思路`：先查后插与 `-EEXIST` 竞争近似、弱遍历语义、`PROC_MAX` 调小的观察实验设计。
  - 两者 `## 预期输出示例`（运行级待验证）：练习 1 `cpu0  123456789 ns: prev=1823 next=910`；练习 2 榜单 `910 5231` / `1823 3120` 等。`## 验证状态` 按 C6。

- [ ] **Step 7: 创建 `exercises/README.md`**：两项任务总览表（与 Task 6 章末表格逐字一致）+ 防剧透阅读法（先做后看、思路节在分界线后）+ 各练习构建命令（各目录 `make`）+ 验证状态汇总表（编译级/运行级，按 C6）。

- [ ] **Step 8: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-02/exercises/01-ringbuf-events && make -C book/examples/chapter-02/exercises/02-lru-top-proc`
Expected: 两个目录退出码均 0，生成 `trace_events` 与 `top_proc`。失败即设计缺陷：修代码后重跑，直至通过。

- [ ] **Step 9: 追加 `.gitignore` 并提交**

```bash
printf '%s\n' 'book/examples/chapter-02/exercises/01-ringbuf-events/trace_events' \
              'book/examples/chapter-02/exercises/02-lru-top-proc/top_proc' >> .gitignore
git add book/examples/chapter-02/exercises/ .gitignore
git commit -m "feat(ch02): 课后练习参考实现（ringbuf 事件流与 LRU top 进程）"
```

---

### Task 8: scripts/verify-build.sh、全量编译验证、正文交叉链接与篇幅终检

- [ ] Task 8: scripts/verify-build.sh、全量编译验证、正文交叉链接与篇幅终检

**tasks.md 条目**：「Create standalone Chapter 2 examples under `book/examples/chapter-02/` if runnable code is included.」（scripts 部分）＋「Add troubleshooting, summary, and references.」（验证状态全量标注收口）

**Files:**
- Create: `book/examples/chapter-02/scripts/verify-build.sh`（可执行，`chmod +x`）
- Modify: `book/chapters/02-ebpf-architecture-and-core-concepts.md`（交叉链接）
- Modify: `book/examples/chapter-02/README.md`（回填验证摘要）

**Interfaces:**
- Consumes: Task 1–7 全部产物；C6/C8。
- Produces: 全绿 verify-build 记录（章级 README 验证状态说明区）；Task 9 主审依据。

- [ ] **Step 1: 创建 `scripts/verify-build.sh`**（完整内容如下）

```bash
#!/usr/bin/env bash
# 第 2 章编译级验证：00-env 自检 + 01-trace-stat + exercises 逐目录 make 汇总。
# 无特权可执行；退出码 0=全绿，1=存在 FAIL。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-02
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
pass=0; fail=0

run() {
    local name="$1"; shift
    if "$@" >"$LOG" 2>&1; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name"; sed -n '1,30p' "$LOG"; fail=$((fail+1))
    fi
}

echo "== BTF 前置 =="
if bpftool btf dump file /sys/kernel/btf/vmlinux format c >/dev/null 2>&1; then
    echo "OK    vmlinux BTF 可读"
else
    echo "FAIL  BTF 不可读：全部示例无法编译"; exit 1
fi

echo "== 环境自检 =="
run "00-env" bash 00-env/check-env.sh

echo "== 示例编译 =="
run "01-trace-stat" make -C 01-trace-stat clean all

echo "== 练习编译 =="
run "exercises/01-ringbuf-events" make -C exercises/01-ringbuf-events clean all
run "exercises/02-lru-top-proc"   make -C exercises/02-lru-top-proc clean all

echo
echo "== 汇总: PASS=$pass FAIL=$fail =="
[ "$fail" -eq 0 ]
```

- [ ] **Step 2: 首次全量运行（硬性验收）**

Run: `bash book/examples/chapter-02/scripts/verify-build.sh`
Expected: `PASS 00-env`、`PASS 01-trace-stat`、`PASS exercises/01-ringbuf-events`、`PASS exercises/02-lru-top-proc`；汇总 `FAIL=0`，退出码 0。任何 FAIL 回到对应任务修复后重跑。
记录：把本次运行摘要（PASS/FAIL 数与工具版本）替换进 `book/examples/chapter-02/README.md` 验证状态说明区的占位句。

- [ ] **Step 3: 正文交叉链接**（C8 惯例）：章首「本章独立示例已全部落地到 [`book/examples/chapter-02/`](../examples/chapter-02/)」与目录总览；5.2 每个代码块前后标注对应文件路径；「一/二/三/五」各节末尾「**独立示例**」链接（二节末为附录 A 链接 `../appendix/a-ebpf-instruction-set.md`）；课后扩展任务节链接 exercises/ 两目录与总览 README。
- [ ] **Step 4: 交叉链接核对（硬性）**

Run: `grep -c "](../examples/chapter-02/" book/chapters/02-ebpf-architecture-and-core-concepts.md && grep -c "](../appendix/" book/chapters/02-ebpf-architecture-and-core-concepts.md`
Expected: `≥ 5`（01-trace-stat + exercises 两目录 + exercises README + 顶层 README）；`≥ 1`。

- [ ] **Step 5: 篇幅终检（硬性）**

Run: `wc -m book/chapters/02-ebpf-architecture-and-core-concepts.md`
Expected: 35000–48000（2026-09-26 控制裁定上调上限，原 35000–45000）。低于下限：优先补原理讲解而非注水（写作纪律——每个概念钉回读者已见过的示例）；高于上限：压缩重复叙述，不删原理。

- [ ] **Step 6: 提交**

```bash
git add book/examples/chapter-02/ book/chapters/02-ebpf-architecture-and-core-concepts.md
git commit -m "docs(ch02): 交叉链接示例资产并完成全量编译级验证标注"
```

---

### Task 9: 主审（teaching completeness）与 tasks.md 勾选（收口）

- [ ] Task 9: 主审（teaching completeness）与 tasks.md 勾选（收口）

**tasks.md 条目**：「Main-agent review against style, examples, terminology, and references.」；同时统一勾选「Chapter 2」全部 9 条。

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`（勾选）
- 产出：审查结论进入最终回复（不创建报告文件）

- [ ] **Step 1: 逐项审计并记录结论**（发现问题回对应任务修复后复验；全绿才进入 Step 2）：
  1. 教学完整性四条（STYLE.md 实战模板）：五层环境（5.1 五个 `####` 层，每层落到命令/脚本/表格）；核心代码原理注释（抽查 `trace_stat.bpf.c`、`trace_events.bpf.c`、`top_proc.bpf.c`：字段语义/边界检查原因/helper 选型/map 选型/失败模式五类注释在场）；双视角（一/三/五三节「教师视角」「学生动手」+ 检查点齐备）；课后练习（2 项目录、防剧透七段 README、参考实现可编译、验证状态标注）。
  2. 结构级（设计 §8）：C7 全部一级小节齐备；5.2 代码块与示例文件一一对应（抽查 3 处路径）；可视化两张 text 图均有解读段。
  3. 术语与保守措辞：映射/辅助函数/程序类型/挂载点译名一致；「英文（中文解释）」首次出现格式；5.10+/版本相关细节保守措辞；全文零「已验证通过」式运行级表述（grep 复核）。
  4. 矩阵专项核对：八要素全覆盖（一节 1.1–1.8 + 5.2 落点）；指令集四要素（RISC/寄存器/调用约定/编码，2.1–2.3）；八类映射（3.1–3.8）；集成示例 + map 交互（Task 2）；独立示例资产（chapter-02 全目录）；bpf() 内核分析四要素（对象创建/fd 所有权/引用计数/pinning，4.2–4.5）；可视化（programs/maps/hooks/loader/控制面，图 1/图 2）；排错/总结/参考（Task 6）。
- [ ] **Step 2: 勾选 tasks.md**：将「Chapter 2: eBPF Architecture and Core Concepts」9 条全部置为 `[x]`（仅当对应交付物真实存在且验收通过）。
- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch02): 勾选 Chapter 2 矩阵任务（build 阶段完成）"
```

按变更工作流在回复中列出：变更文件清单、参考来源、已执行验证（verify-build 全绿记录）、已知限制（运行级待验证清单）。

---

## 计划自查记录（writing-plans Self-Review）

1. **Spec 覆盖**：设计 §2 示例资产 → Task 1（README/00-env/.gitignore）、Task 2（01-trace-stat）、Task 7（exercises 全目录）、Task 8（verify-build.sh）；设计 §3 正文 → Task 3（学习目标/导读/一/二）、Task 4（三/四）、Task 5（五：五层 + 走读 + 双视角 + 检查点）、Task 6（可视化/排错/课后/总结/参考）；设计 §4 练习 → Task 7；设计 §5 验证策略 → Task 2/7（单目录硬性）与 Task 8（全量硬性）；设计 §6 风险 → 各任务缓解（prev_comm 定长数组实测字段入 5.2/排错、篇幅分节预算入全局约束 4、sockmap/LPM 交叉引用入 3.7/3.8、权限矩阵入 5.1）；tasks.md 9 条 → 条 1（Task 3 前半 + Task 5 后半）、条 2（Task 3）、条 3（Task 4）、条 4（Task 2 + Task 5）、条 5（Task 1/2/7/8）、条 6（Task 4）、条 7（Task 6）、条 8（Task 6 + Task 8）、条 9（Task 9）。无遗漏条目；Chapter 1 遗留任务按设计 §7 明确排除在范围外。
2. **占位符扫描**：全部任务给出实际内容——三份 `.bpf.c`、三份 loader、check-env.sh、verify-build.sh 逐字全文；Makefile 经 C4 模板 + 替换表定死；README 给出结构 + 关键口径；正文任务给出节标题锚点、讲解要点清单与篇幅锚点；无「TBD/TODO/类似 Task N」表述；正文小节标题经 C7 逐字固定。
3. **类型一致性**：`MAX_CPUS=64`/`struct cpu_stat` 两侧一致（C1，skeleton 不导出自定义类型已冒烟测试验证）；`struct sched_event`/`events`/`(1<<24)`（C2）；`PROC_MAX=4096`/`proc_counts`（C3）；SEC/函数名与 skeleton 前缀一一对应（`trace_stat`/`trace_events`/`top_proc` ↔ `trace_stat_bpf`/`trace_events_bpf`/`top_proc_bpf`）；Makefile 替换表（C4）与目录/目标名一致；章末课后任务表（Task 6）与 exercises/README.md 总览表（Task 7）逐字一致；loader 侧含 `<bpf/bpf.h>`（冒烟测试发现隐式声明告警后修正）。
4. **硬性验收**：编译级验证在 Task 2/7（单目录）与 Task 8（全量）均为显式步骤，退出码 0 为通过标准；正文核对 grep（标题计数/占位句扫描/链接计数/篇幅区间）在 Task 3/4/5/6/8 均给出确切命令与预期值；运行级全部「预期输出示例 + 运行级待验证」标注，零「已验证通过」表述。

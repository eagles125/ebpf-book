# 第 2 章课后扩展任务总览

本章两项课后扩展任务的完整参考实现，分别向两个方向各推一步：事件通道换形态（练习 1：ring buffer 事件流）、映射类型换选型（练习 2：LRU hash top 进程）。两项都基于 01-trace-stat 的构建体系（Makefile 同构，各目录独立 `make`），对应正文「五、实战」的基线示例与「课后扩展任务」一节。

## 任务总览

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 事件通道 | 把 01-trace-stat 的轮询改为 ring buffer 事件流（reserve/submit + `ring_buffer__poll`） | `book/examples/chapter-02/exercises/01-ringbuf-events/` |
| 2 | 映射类型 | 用 LRU hash 记录每进程被调度次数并打印 top-N（体验容量与静默淘汰） | `book/examples/chapter-02/exercises/02-lru-top-proc/` |

验收标准（与各练习 README「任务」节同口径）：练习 1 加载后能看到带 CPU 号、时间戳与 pid 对的事件流，并能说明它与轮询的取舍；练习 2 的 top-N 榜单随系统负载变化，且能解释「旧 pid 从榜单消失」的两种可能——进程退出，或 LRU 满表静默淘汰。

## 防剧透阅读法

每项练习的 README 都按「**任务在前、参考实现思路独立成节放在后部**」组织，以一条 `---` 分界线隔开：

1. **先只读分界线之前**：任务（含验收标准）、提示（只给「会用到什么」）、环境前提与构建、预期输出示例——据此自行设计并实现；
2. **做完再对照分界线之后**：参考实现思路逐条解释本目录内参考代码的取舍与理由，用作自查与进阶讨论的起点。

直接阅读参考实现等于放弃练习价值——两项任务的验收标准都写在对应 README 的「任务」一节，可先按标准自测。

## 构建命令

各练习在其目录内 `make` 即可（与 01-trace-stat 同构：vmlinux.h → BPF 对象 → skeleton → C loader）：

| 练习 | 基线 | 语言栈 | 构建 |
|---|---|---|---|
| `01-ringbuf-events/` | 01-trace-stat | C / libbpf skeleton | `cd 01-ringbuf-events && make` |
| `02-lru-top-proc/` | 01-trace-stat | C / libbpf skeleton | `cd 02-lru-top-proc && make` |

运行任何练习都需要 root 或 `CAP_BPF`（+ `CAP_PERFMON`/`CAP_SYS_ADMIN`）；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。安全警告：tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

一键全量编译级验证：`bash ../scripts/verify-build.sh`（00-env 自检 + 01-trace-stat + 两项练习逐目录 `make`）。

## 验证状态汇总

| 练习 | 编译级 | 运行级 |
|---|---|---|
| 01-ringbuf-events | 已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过 | 待验证 |
| 02-lru-top-proc | 已在编写环境（同上）`make` 通过 | 待验证 |

统一口径：编写环境为非 root（`unprivileged_bpf_disabled=2`），两项练习均未执行加载、挂载与运行，各 README 的「预期输出示例」均为教学示例（验证状态约定见正文 5.1）。

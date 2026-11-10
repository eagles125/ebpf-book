# 第 9 章课后扩展任务总览

本章三项课后扩展任务的完整参考实现，按「改写 → 新增 → 综合」由浅入深编号（任务编号即目录编号）。各练习以本章对应示例为基线，复用同一构建体系（Makefile 同构，各目录独立 `make`），分别落在过滤维度、事件结构与输出语义、事件通道三个扩展方向上。

## 任务总览

| 任务 | 扩展维度 | 任务描述 | 基线示例 | 参考实现 |
|---|---|---|---|---|
| 1 | 过滤维度 | 按 comm 过滤 execve：ARRAY map 存 16 字节进程名，内核态补零约定下逐字节比较 | 02-pid-filter | `01-comm-filter/` |
| 2 | 事件结构 | 两级事件 opensnoop：成功路径 minimal 信息，失败路径带 -errno 并映射 errno 名 | 05-open-file-kprobe | `02-minimal-failed/` |
| 3 | 事件通道 | ring buffer → perf buffer 迁移：同一 execve 事件结构与输出列换通道 | 04-ring-buffer | `03-ringbuf-to-perf/` |

三项练习与既有示例的口径区分（各练习 README 内有展开）：练习 1 与 02 的差别只在「按什么比」（4 字节整数 → 16 字节补零字符串），输出与生命周期不变；练习 2 的 enter/exit 关联机制与 07 同源，但定位是事件结构演化与两级输出——07 每个 open 输出一条带 RET 列的完整事件，本练习成功行保持 minimal、失败行才带诊断信息；练习 3 与 06 的区别在基座——06 用 opensnoop 事件演示通道差异，本练习把 04 的 execve 事件原样迁移，diff 出来的差异恰为「换通道的那一对调用」。

## 防剧透阅读法

每项练习的 README 都按「**任务在前、参考实现思路独立成节放在后部**」组织，以一条 `---` 分界线隔开：

1. **先只读分界线之前**：任务（含验收标准）、提示（只给「会用到什么」）、环境前提与构建、预期输出示例——据此自行设计并实现；
2. **做完再对照分界线之后**：参考实现思路逐条解释本目录内参考代码的取舍与理由，用作自查与进阶讨论的起点。

直接阅读参考实现等于放弃练习价值——三项任务的验收标准都写在对应 README 的「任务」一节，可先按标准自测。

## 构建命令

各练习在其目录内 `make` 即可（与本章示例同构：vmlinux.h → BPF 对象 → skeleton → C loader）：

| 练习 | 基线 | 语言栈 | 构建 |
|---|---|---|---|
| `01-comm-filter/` | 02-pid-filter | C / libbpf skeleton | `cd 01-comm-filter && make` |
| `02-minimal-failed/` | 05-open-file-kprobe | C / libbpf skeleton | `cd 02-minimal-failed && make` |
| `03-ringbuf-to-perf/` | 04-ring-buffer（机制参照 06） | C / libbpf skeleton | `cd 03-ringbuf-to-perf && make` |

运行任何练习都需要 root 或 `CAP_BPF`（+ `CAP_PERFMON`/`CAP_SYS_ADMIN`）；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。安全警告：本节练习均为只读观测（tracepoint/kprobe，不修改内核行为）；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

一键全量编译级验证：`bash ../scripts/verify-build.sh`（00-env 自检 + 示例 01–08 逐项 `make` + 09-netfilter-go 内核 6.6+ 门禁 + 三项练习逐项 `make`）。

## 验证状态汇总

| 练习 | 编译级 | 运行级 |
|---|---|---|
| 01-comm-filter | 已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过 | 待验证 |
| 02-minimal-failed | 已在编写环境（同上）`make` 通过 | 待验证 |
| 03-ringbuf-to-perf | 已在编写环境（同上）`make` 通过 | 待验证 |

统一口径：编写环境为非 root（`unprivileged_bpf_disabled=2`），三项练习均未执行加载、挂载与运行，各 README 的「预期输出示例」均为教学示例；编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。

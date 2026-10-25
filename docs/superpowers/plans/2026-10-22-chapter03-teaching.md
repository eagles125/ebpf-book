---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-10-22-chapter03-teaching-design.md
base-ref: ff8882993b9235ccd224e23a7293e16f6ed33216
---

# 第 3 章完整教学章节（build 阶段）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 3 章《验证器与安全保障》从零写为 35–48k 字符的完整教学章节（机制章 + 失败画廊实战化），并落地 `book/examples/chapter-03/` 全部独立示例资产（00-env + 01-pkt-parse 正面锚点 + 02-fail-gallery 七对 bad/fixed + scripts 编译级验证）与 2 项课后练习的完整可编译参考实现（含防剧透 README）。

**架构:** 01-pkt-parse（XDP 报文解析，libbpf-bootstrap C loader 模式）承载「验证通过」路径，02-fail-gallery 七对 bad/fixed 承载「预期被拒」路径——bad 程序在 clang 层面全部合法（拒绝发生在加载期），把第 2 章「编译成功≠能加载」的教学点变成 verify-build 的机械验收事实；正文按「完整章节模板」组织为 安全模型→验证流水线内核分析→失败画廊→实战读日志 四段；课后练习（修复挑战、日志诊断）各带完整参考实现；`scripts/verify-build.sh` 做单章编译级汇总验证。

**技术栈:** C（vmlinux.h + clang -target bpf + bpftool gen skeleton + libbpf loader；画廊与练习为 compile-only，无 loader）、bash（环境自检/编译级验证脚本）、Markdown（中文正文）。本章无 Go。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md`「Chapter 3: Verifier and Safety Guarantees」8 条。
- delta spec 无需变更（无 Spec Patch），不修改 `openspec/changes/generate-ebpf-book/specs/`。
- **第 1 章两条遗留任务、第 9 章教学改造项不在本计划范围**，不要触碰 `book/chapters/01-*.md`、`book/chapters/09-*.md`。
- 不触碰 `book/chapters/02-*.md`、`book/chapters/17-*.md`、`book/examples/chapter-02/`、`book/examples/chapter-09/`、`book/examples/chapter-17/`。
- 只修改：`book/chapters/03-verifier-and-safety-guarantees.md`（新建）、`book/examples/chapter-03/**`（新建）、`openspec/changes/generate-ebpf-book/tasks.md`（勾选）、`.gitignore`（追加产物条目）。

## 全局约束

以下约束适用于所有任务（复制自设计文档与 `book/STYLE.md`，逐字为准）：

1. **语言**：全书与全部代码注释使用中文（`Language: zh-CN`）；术语遵循 `book/STYLE.md`（map=映射、helper=辅助函数、program type=程序类型、attachment point=挂载点）；术语首次出现用「英文（中文解释）」或「中文（English）」格式。
2. **验证策略（硬性验收）**：编译级真实验证在本机执行——clang 18.1.3、bpftool 7.7.0（libbpf 1.3 系统头）、gcc、BTF（`/sys/kernel/btf/vmlinux` 存在）齐备，本章无 Go、无网络依赖；**无 root，`kernel.unprivileged_bpf_disabled=2`**。凡涉及加载/运行的效果一律写「预期输出示例」并标注「运行级待验证」，不得写成「已验证通过」。
3. **bad/fixed 编译口径（本章特有硬性验收）**：画廊七对与练习 broken 程序**必须全部 clang 编译通过**——它们不是语法错误，是语义被拒；verify-build 对它们的验收就是「编译成功」。加载级行为（通过/拒绝的实际 verifier 日志）本机不可实测，预期日志一律标注「预期输出示例 / 运行级待验证」。
4. **篇幅**：章节正文目标 35–48k 字符（`wc -m` 口径）；分节预算按设计 §3.3：目标+导读 2–2.5k、一（安全模型）3.5–4.5k、二（内核分析）8–11k、三（画廊走读）9–12k、四（实战含五层）6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后任务与总结/参考 2.5–3.5k。
5. **叙事**：双视角落「一（安全模型）、三（失败画廊）、四（实战）」三节——各节内用「**教师视角**」「**学生动手**」加粗引导段，检查点用编号列表（2–4 个自测问题 + 答案提示）。本章非渐进阶段章节，「四段式阶段教学节奏」不约束一/二/三节；「四、实战」5.2 内以教师视角引入/核心代码原理讲解/学生动手/检查点四段组织（设计 §3.2）。
6. **代码注释**：所有 `.bpf.c` 与 loader C 源文件中文注释，覆盖五类要点：数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。画廊 bad 文件的注释必须讲清「verifier 为什么会拒绝」。
7. **内核兼容**：helper/特性保守选择，5.10+ 兼容优先；verifier 日志文本随内核版本漂移——预期日志以 5.10+ 稳定格式与 `Documentation/bpf/verifier.rst` 措辞为基准，全文统一标注「日志行随内核版本略有差异」；版本演进点（有界循环 5.3、`bpf_probe_read_user_str` 5.5、`bpf_loop` 5.17）保守措辞并注明。本机内核 7.0.0-38 仅作编译口径参考。
8. **安全**：锚点为 XDP 程序，直接作用于网卡收包路径——安全警告统一措辞：「XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。」画廊程序与编译脚本不加载、无此风险，脚本头部警告写明「本脚本只编译不加载」。
9. **构建产物不入库**：`.gitignore` 全局规则已排除 `book/examples/**/vmlinux.h`、`*.skel.h`、`*.o`；新增二进制产物按任务追加条目（Task 1：pkt_parse）。
10. **任务对应**：每个任务必须引用 tasks.md「Chapter 3」矩阵的唯一任务文本片段；全部任务完成后由 Task 11 统一勾选 tasks.md 的 8 条。
11. **提交纪律**：每个任务以一次 git 提交结束；提交信息格式 `feat(ch03): ...` / `docs(ch03): ...` / `chore(ch03): ...`；从仓库根 `/work/ai/ebpf-book` 执行命令。
12. **工作区**：开始前确认 `git rev-parse HEAD == ff8882993b9235ccd224e23a7293e16f6ed33216`（或基于该提交的工作树）。工作区允许存在 Comet 编排产物（`openspec/changes/generate-ebpf-book/.comet/*`、`.zcodeignore`）的未提交改动——它们**不属于任何 ch03 提交**，每次 `git add` 只加当前任务 Files 列出的路径。

## 现状基线（编写计划时已核实）

| 事实 | 值 |
|---|---|
| HEAD | `ff8882993b9235ccd224e23a7293e16f6ed33216`（工作区含 .comet 编排产物改动，见全局约束 12） |
| 章节现状 | 第 3 章正文**不存在**（`book/chapters/` 仅 01/02/09/17）；`book/examples/chapter-03/` **不存在** |
| 第 2 章预告回收清单 | ①L37「编译成功≠能加载，中间隔着第 3 章静态验证」→ 一节展开；②L60「helper 白名单加载时拒绝」→ 三节 card ⑥；③L131「验证是第 3 章主题」（生命周期六步）→ 一节定位；④L160/L164「指令集越小 verifier 能证明的越多」→ 二节安全模型前提；⑤L239「跳转偏移必须落在程序内、必须可证明会终止」→ 二节终止性与 card ⑤；⑥L443「逐指令模拟执行，证明内存安全/可终止/权限合法，拒绝返回 -EACCES 并给出日志」→ 二节流水线主线；⑦L652「ctx 申报访问 vs 任意内核指针 probe 界限，证明过程是第 3 章正题」「数组 map 越界 helper 内返回 NULL（运行时定界）」→ 二节 helper 约束 + card ①/card ⑦；⑧L688「-EPERM/-EACCES 分别指向权限档位与 verifier 日志」→ 四节 5.1 权限矩阵 + 排错节；⑨L755「第 3 章将重读同一程序的另一端——verifier 如何证明它安全」→ 总结勾稽表 |
| 附录 | `book/appendix/b-tool-command-index.md`（命令索引 stub）、`book/appendix/c-distro-btf-build-guide.md`（BTF 指南 stub）——正文可交叉引用 |
| 本机工具链 | clang 18.1.3、bpftool 7.7.0、gcc、libbpf-dev 1.3.0（`/usr/include/bpf/bpf_helpers.h` 存在）、内核 7.0.0-38 |
| 权限 | 非 root；`/proc/sys/kernel/unprivileged_bpf_disabled` = 2（非特权不可加载） |
| BTF | `/sys/kernel/btf/vmlinux` 存在；`struct xdp_md`、`struct trace_event_raw_sys_enter { struct trace_entry ent; long id; unsigned long args[6]; }` 均已在本机 vmlinux.h 核实 |
| 冒烟测试（编写计划时已完成） | ①锚点 `pkt_parse.bpf.c` 本机 `clang -target bpf` 编译、`bpftool gen skeleton`、gcc 全链链接通过；②画廊七对 bad/fixed 共 14 个 `.bpf.c` 全部编译通过；③练习 broken/fixed/reproduce 3 个文件全部编译通过；④**uapi 常量不在 vmlinux.h**——`ETH_P_IP`/`IPPROTO_UDP` 需在源文件头部手工定义；⑤loader 侧 `if_nametoindex` 需 `<net/if.h>`、`bpf_map_lookup_elem` 需 `<bpf/bpf.h>`（否则隐式声明告警）；⑥`bpf_program__attach_xdp` 在 libbpf 1.3 头文件中存在 |
| 可复用模式 | 第 2 章计划「共享接口契约」的 C4（全链 Makefile）/C5（README 结构）/C6（验证状态）/C7（标题固定）/C8（交叉链接）——本计划以同字母契约引用并给出本章替换与新增（见下） |

## 共享接口契约（跨任务定义，类型一致性以此为准）

### C1. 01-pkt-parse 锚点契约

```c
#define ETH_P_IP 0x0800    /* uapi 常量不在 vmlinux.h，手工定义（冒烟测试结论④） */
#define IPPROTO_UDP 17
```

- map：`udp_counts`，`BPF_MAP_TYPE_ARRAY`，key `__u32`（恒为 0），value `__u64`（UDP 报文计数），`max_entries 1`；计数用 `__sync_fetch_and_add`（原子递增，verifier 天然放行）。
- 程序与 SEC：`SEC("xdp") int pkt_parse(struct xdp_md *ctx)`；skeleton 符号前缀 `pkt_parse_bpf`（`skel->progs.pkt_parse`、`skel->maps.udp_counts`）。
- 解析链：eth→ipv4→udp 三层，**每层解引用前先 `data + sizeof(*层) > data_end` 边界检查**（本章教学核心）；`bpf_htons` 来自 `<bpf/bpf_endian.h>`。
- loader：`pkt_parse.c`，用法 `sudo ./pkt_parse <网卡名>`（建议 `lo` 或 veth）；`if_nametoindex`（`<net/if.h>`）转 ifindex；`bpf_program__attach_xdp(skel->progs.pkt_parse, ifindex)` 挂载；每秒读 map 打印计数；Ctrl-C 退出即 `bpf_link__destroy` + `__destroy` 卸载。
- 挂载模式措辞（README 与正文统一）：「flags=0 挂载时若驱动不支持原生 XDP，内核一般会自动回退到 generic（SKB）模式——具体行为随内核版本，以实测为准。」

### C2. 画廊程序公共骨架与 compile-only Makefile 契约

**公共 include 块**（每个画廊/练习 `.bpf.c` 的固定开头，XDP 类程序加常量定义，tracepoint 类不加）：

```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
```

**通用 stats map 块**（仅 card ①、④ 与练习 1 需要的 map 定义，逐字）：

```c
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");
```

**license 行**（每个文件固定结尾，逐字）：

```c
char LICENSE[] SEC("license") = "GPL";
```

**compile-only Makefile 模板**（画廊七对与练习目录共用；`<SRCS>` 按目录替换；规则行用制表符缩进）：

```make
# 第 3 章「失败画廊」编译脚本（compile-only：拒绝发生在加载期，clang 层面必须全部通过）
# 安全警告：本脚本只编译不加载（无 XDP 挂载风险）——bad 产物预期在加载期被验证器拒绝
BPF_SRCS := <SRCS>
# 目标名替换用 .c=.o：源文件名已是 *.bpf.c，直接得到 *.bpf.o
# （用 .c=.bpf.o 会产生 bad.bpf.bpf.o 双重扩展名——Task 3 实现期实测修正）
BPF_OBJS := $(BPF_SRCS:.c=.o)

all: $(BPF_OBJS)

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

%.bpf.o: %.bpf.c vmlinux.h
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -c $< -o $@

clean:
	rm -f $(BPF_OBJS) vmlinux.h

.PHONY: all clean
```

各目录 `<SRCS>` 替换表：

| 目录 | `<SRCS>` |
|---|---|
| `02-fail-gallery/<七对目录>/` | `bad.bpf.c fixed.bpf.c` |
| `exercises/01-fix-challenge/` | `broken.bpf.c fixed.bpf.c` |
| `exercises/02-log-diagnosis/` | `reproduce.bpf.c` |

### C3. 画廊七对契约（bad 语义 / fixed 修复 / 预期 verifier 日志关键行）

每对目录含 `bad.bpf.c`、`fixed.bpf.c`、`README.md`；bad 与 fixed **仅差一处语义错误**；bad 与 fixed 函数名分别固定为 `probe_bad` / `probe_fixed`（card ⑦ 同）；程序骨架按 C2 组装。预期日志关键行以 5.10+ 稳定措辞为基准，**全部标注「预期输出示例 / 运行级待验证」并注明「日志行随内核版本略有差异」**（全局约束 7）。

| 对 | bad 语义错误 | fixed 修复 | 预期 verifier 日志关键行 |
|---|---|---|---|
| 01-map-null | `bpf_map_lookup_elem` 返回值未检查即 `*v += 1`（map_value_or_null 直接解引用） | 增加 `if (!v) return XDP_PASS;` | `invalid mem access 'map_value_or_null'` |
| 02-pkt-bounds | eth/ip 指针推进后无任何 `data_end` 检查就读字段（首个越界读在 `eth->h_proto`） | 两层各加 `(void *)(指针 + 1) > data_end` 检查 | `R2 offset is outside of the packet`（或 `invalid packet access`） |
| 03-ptr-math | 对 pkt_end 指针做算术且结果可观测（防 clang 死代码消除）：`data_end += 4` 后参与 `data > data_end` 比较——两侧偏移 0 对 4 不等，编译器无法折叠公共偏移（`data + 4 > data_end` 形态会被 InstCombine 折叠掉算术指令，实测教训） | 去掉对 pkt_end 的算术，比较只保留 `data + 4 > data_end`（算术放在 packet 指针上） | `R2 pointer arithmetic on pkt_end PROHIBITED` |
| 04-stack-uninit | `volatile __u64 verdict;` 仅在 `if (v)` 分支赋值，分支外条件使用（volatile 使未初始化栈槽读取落到真实指令——裸形式会被 clang -O2 UB 折叠消除，实测教训） | 声明即初始化 `volatile __u64 verdict = 0;`（两侧同带 volatile，diff 仅剩 `= 0` 一处） | `R2 !read_ok`（真实形态两种：针对 NOT_INIT 寄存器的 `R%d !read_ok` 与无寄存器号的 `invalid read from stack off %d+%d size %d`，无任何版本打印拼接形态；**特权 CAP_PERFMON 加载放行未初始化栈读**——「预期被拒」仅在允许非特权 BPF 的配置成立，README 必须带豁免说明） |
| 05-loop | 循环上界 `n` 直接取自报文且无掩码（不可证明终止）；累加用 `sum ^= i`（线性 `sum += i` 会被 -indvars 折叠成闭式、循环整条消除，实测教训） | `n & 0xF` 掩码定界（n 收窄到 [0, 15]，迭代至多 15 次）；两侧同用 `sum ^= i`，diff 仅剩掩码一处 | `infinite loop detected at insn ...`（特权路径主形态——5.10+ 特权加载容忍回边、拒绝来自状态探索不收敛；`back-edge from insn ... to ...` 为非特权路径形态） |
| 06-helper-misuse | XDP 程序调用 `bpf_get_current_pid_tgid()`（tracing 类专用，不在 XDP 白名单） | 改用 `bpf_get_smp_processor_id()` | `unknown func bpf_get_current_pid_tgid#14`（5.10+ 白名单 miss 的真实形态——旧「cannot use helper」/「not allowed for program type」措辞已不存在于内核源码） |
| 07-kernel-ptr | tracepoint 程序把 ctx 标量字段强转指针直接解引用：`char *fn = (char *)ctx->args[1]; fn[0]` | `bpf_probe_read_user_str()` 读入栈缓冲后再判读（5.5+；旧版 `bpf_probe_read_str` 兼容行为注明） | `invalid mem access 'scalar'` |

- card ⑦ 程序类型为 tracepoint：`SEC("tracepoint/syscalls/sys_enter_openat") int probe_bad/fixed(struct trace_event_raw_sys_enter *ctx)`（布局见现状基线），无需 stats map。
- 每对 README.md 固定六段：**任务一句话 / bad 与 fixed 差异 / 预期 verifier 日志关键行（标注口径）/ 为什么拒绝（原理讲解）/ 修复原则 / 验证状态（按 C6）**；bad 文件头部注释必须含「预期被拒」字样与拒绝原因一句话。

### C4. 全链 Makefile 模板（沿用第 2 章计划 C4 = 第 17 章计划 C7 原文，仅替换目标名）

仅 `01-pkt-parse/` 使用全链模板：`vmlinux.h`（bpftool 由 /sys/kernel/btf/vmlinux 生成）→ `pkt_parse.bpf.o`（clang -target bpf）→ `pkt_parse.skel.h`（bpftool gen skeleton）→ 可执行 `pkt_parse`（gcc -lbpf -lelf -lz）。`.bpf.o` 规则依赖 `pkt_parse.bpf.c vmlinux.h`。模板首行注释统一为「第 3 章示例构建脚本（libbpf-bootstrap 模式，与第 2/17 章示例一致）」，并加注释行安全警告（全局约束 8 措辞）。

### C5. README 结构（沿用第 2 章计划 C5 原文）

- **示例/章级 README 十段结构**：1 标题 + 一句话定位（对应章节小节）　2 演示目标　3 文件说明（表）　4 环境前提　5 编译与运行（命令）　6 预期输出（标注「预期输出示例 / 运行级待验证」）　7 代码讲解要点（中文注释呼应）　8 常见失败点（表：现象/原因与处理）　9 验证状态（按 C6）　10 参考来源。
- **画廊 README 六段结构**（每对，见 C3）。
- **练习 README 防剧透七段结构**：1 `## 任务`（面向学生的任务描述与验收标准）　2 `## 提示`（涉及哪些机制/错误信号，不给答案）　3 `## 环境前提与构建`　4 `## 预期输出示例`（运行级待验证）　5 `## 参考实现思路`（独立成节，防剧透分界线 `---` 在第 4 节之后）　6 `## 验证状态`　7 `## 参考来源`。

### C6. 验证状态约定（沿用第 2 章计划 C6 原文）

- **编译验证**：写明「已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过」。
- **待运行验证**：写明「编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例」。
- **本章补充**：bad 程序与 broken 练习的验证状态为「编译级已通过（预期如此——拒绝发生在加载期）；加载级『预期被拒』待特权环境验证」。

### C7. 章节正文标题（逐字固定，所有正文任务不得改动一二级标题）

```text
# 第 3 章 验证器与安全保障
## 学习目标
## 本章导读
## 一、验证器的目标与安全模型
## 二、验证流水线内核实现分析
## 三、失败画廊：七类典型拒绝
## 四、实战：读懂你的第一条 verifier 日志
## 可视化：验证器状态探索与剪枝
## 常见问题与排错
## 课后扩展任务
## 总结
## 本章参考来源
```

### C8. 交叉链接惯例（沿用第 2 章计划 C8 惯例）

相对路径链接 + 仓库根路径文本：章首「本章独立示例已全部落地到 [`book/examples/chapter-03/`](../examples/chapter-03/)」；每个正文代码块前后标注对应文件路径（如 `book/examples/chapter-03/01-pkt-parse/pkt_parse.bpf.c`）；「一/二/三/四」各节末尾「**独立示例**：`../examples/chapter-03/<目录>/`」链接（二节末为附录 B/C 链接 `../appendix/b-tool-command-index.md`、`../appendix/c-distro-btf-build-guide.md`）；三节每个 card 链接对应画廊目录；课后任务节链接 exercises/ 各目录。

### C9. 章末课后任务表（Task 8 与 Task 9 的 exercises/README.md 总览表逐字一致）

```markdown
| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 综合修复 | 修复挑战：`broken.bpf.c` 含报文边界缺失、未检查 lookup、未初始化栈变量三处组合错误——修到编译通过，并在满足权限的内核上能加载成功 | `book/examples/chapter-03/exercises/01-fix-challenge/` |
| 2 | 日志诊断 | 按预期 verifier 日志片段「R2 min value is negative, either use unsigned or 'var &= const'」反推根因，写程序复现并逐行解读日志 | `book/examples/chapter-03/exercises/02-log-diagnosis/` |
```

---

### Task 1: 章级 README、00-env 环境自检与 .gitignore 追加

- [x] Task 1: 章级 README、00-env 环境自检与 .gitignore 追加

**tasks.md 条目**：「Create standalone Chapter 3 verifier examples under `book/examples/chapter-03/`.」（章级资产部分）

**Files:**
- Create: `book/examples/chapter-03/README.md`
- Create: `book/examples/chapter-03/00-env/check-env.sh`（可执行，`chmod +x`）
- Modify: `.gitignore`（追加一行 `book/examples/chapter-03/01-pkt-parse/pkt_parse`）

**Interfaces:**
- Consumes: C5（章级 README 十段）、C6（验证状态约定）、全局约束 3/8（bad 编译口径与 XDP 安全警告措辞）。
- Produces: `check-env.sh` 三档结论（编译级/加载级/运行级），供 01-pkt-parse README「环境前提」与正文 5.1 第一层引用；章级 README 的示例总览表（Task 3/4/9 的目录命名依据）；`00-env/` 无 Makefile（纯脚本）。

- [x] **Step 1: 创建 `book/examples/chapter-03/README.md`**（十段结构，内容口径如下）

标题「# 第 3 章独立示例资产」+ 定位句（对应正文「三、失败画廊」与「四、实战」）。目录结构表：

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | 内核/BTF/工具链/权限分级诊断（无 root 可跑） |
| `01-pkt-parse/` | 正面锚点 | 能过验证的最小 XDP 报文解析：验证「通过」路径的实物 |
| `02-fail-gallery/` | 失败画廊 | 七类典型拒绝的 bad/fixed 对：验证「拒绝」路径的实物 |
| `scripts/` | 编译级验证 | `verify-build.sh` 汇总本目录全部 make |
| `exercises/` | 课后扩展任务 | 2 项完整参考实现 + 防剧透 README |

其余段落：演示目标（正反两条路径各自落到哪个文件）；学习路径（check-env → 01-pkt-parse → 画廊逐对 → 练习）；**安全警告**（全局约束 8 措辞，逐字）；统一构建模式（C2 compile-only + C4 全链两种，说明「画廊与练习只编译、锚点全链」的原因=全局约束 3）；验证状态说明（C6 三条 + 「全量编译结果由 `scripts/verify-build.sh` 生成，摘要见 Task 10 回填」占位句——Task 10 会替换为实际摘要）。

- [x] **Step 2: 创建 `book/examples/chapter-03/00-env/check-env.sh`**（完整内容如下——以第 2 章已验证脚本为基底，改章号注释并追加 XDP 挂载目标检查）

```bash
#!/usr/bin/env bash
# 第 3 章示例环境自检（分级诊断）。
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
    echo 'OK    当前为 root：可加载、可挂载 XDP 与 tracepoint'
elif capsh --print 2>/dev/null | grep -q 'cap_bpf'; then
    echo 'OK    具备 CAP_BPF：可加载程序，挂载 XDP 还需 CAP_NET_ADMIN'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo 未知)"
    echo "WARN  非 root 且无 CAP_BPF（unprivileged_bpf_disabled=${ubd}）："
    echo '      - 可以完成：编译级验证（make / scripts/verify-build.sh）'
    echo '      - 不能完成：加载与挂载 XDP/tracepoint、bpftool prog/map 观察（需 sudo）'
fi
if ip link show lo >/dev/null 2>&1; then
    echo 'OK    lo 接口存在（XDP 锚点程序的推荐挂载目标）'
else
    echo 'WARN  lo 接口不可见（XDP 挂载目标缺失，编译不受影响）'
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

Run: `chmod +x book/examples/chapter-03/00-env/check-env.sh && bash book/examples/chapter-03/00-env/check-env.sh`
Expected: 退出码 0；工具链各行 OK；权限分级输出 WARN 段（本机非 root）并明示「可以完成编译级验证」；lo 接口行 OK。

- [x] **Step 4: 追加 `.gitignore` 并提交**

```bash
printf '%s\n' 'book/examples/chapter-03/01-pkt-parse/pkt_parse' >> .gitignore
git add book/examples/chapter-03/README.md book/examples/chapter-03/00-env/ .gitignore
git commit -m "feat(ch03): 章级 README 与 00-env 分级环境自检"
```

---

### Task 2: 01-pkt-parse 正面锚点示例（pkt_parse.bpf.c + pkt_parse.c + Makefile + README）

- [x] Task 2: 01-pkt-parse 正面锚点示例（pkt_parse.bpf.c + pkt_parse.c + Makefile + README）

**tasks.md 条目**：「Create standalone Chapter 3 verifier examples under `book/examples/chapter-03/`.」（锚点部分）

**Files:**
- Create: `book/examples/chapter-03/01-pkt-parse/pkt_parse.bpf.c`
- Create: `book/examples/chapter-03/01-pkt-parse/pkt_parse.c`
- Create: `book/examples/chapter-03/01-pkt-parse/Makefile`
- Create: `book/examples/chapter-03/01-pkt-parse/README.md`

**Interfaces:**
- Consumes: C1（map/程序/SEC/loader 契约）、C4（全链 Makefile 模板）、C5（README 十段）、C6（验证状态）、全局约束 8（安全警告）。
- Produces: 正文 Task 7「四、实战」5.2 走读的唯一「通过路径」代码对象；verify-build.sh 的全链编译项。

- [x] **Step 1: 创建 `pkt_parse.bpf.c`**（逐字如下，冒烟测试已验证编译）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-pkt-parse：能通过验证的最小 XDP 报文解析程序（本章「通过路径」锚点）。
 * 教学落点：packet 指针的边界检查是 verifier 证明内存安全的核心——每层协议头
 *   解引用前先做 data + sizeof(层) > data_end 检查（缺一层就会被拒，见画廊 02）。
 * 失败模式：无 root 时 open_and_load 失败（预期，见 README）；被拒形态见画廊。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），按 libbpf-bootstrap 惯例手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

/* 选 array map：单槽全局计数器，key 恒为 0——演示「map 交互」这一最小闭环；
 * 多 CPU 并发计数用原子递增（verifier 天然放行），避免 per-CPU 读回聚合的额外讲解 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} udp_counts SEC(".maps");

SEC("xdp")
int pkt_parse(struct xdp_md *ctx)
{
    /* xdp_md 的 data/data_end 是寄存器宽度裸指针，经 (long) 中转标准转换 */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* 第一层：以太网头——解引用前先证明 [data, data+14) 在报文内 */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;                       /* 截断报文：放行交协议栈处理 */
    if (eth->h_proto != bpf_htons(ETH_P_IP))   /* h_proto 为网络字节序 */
        return XDP_PASS;

    /* 第二层：IPv4 头——边界检查以「上一层尾=这一层头」推进 */
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    if (ip->protocol != IPPROTO_UDP)
        return XDP_PASS;

    /* 第三层：UDP 头——同一模式再走一遍；不读 payload，无需更深的检查 */
    struct udphdr *udp = (void *)ip + sizeof(*ip);
    if ((void *)(udp + 1) > data_end)
        return XDP_PASS;

    __u32 key = 0;
    __u64 *count = bpf_map_lookup_elem(&udp_counts, &key);
    if (count)                                  /* array map 单槽恒在，防御式检查 */
        __sync_fetch_and_add(count, 1);         /* 原子递增：多网卡队列并发安全 */

    return XDP_PASS;                            /* 只观测不干预，一律放行 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 2: 创建 `pkt_parse.c`**（逐字如下；`<net/if.h>` 与 `<bpf/bpf.h>` 为冒烟测试结论⑤的必需头文件）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-pkt-parse 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./pkt_parse <网卡名>   （建议 lo 或 veth，勿挂生产物理网卡）
 * 职责: open_and_load（打开对象 -> verifier 校验 -> JIT）-> attach XDP ->
 *       每秒打印 UDP 计数 -> Ctrl-C 退出即卸载。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "pkt_parse.skel.h"

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: sudo %s <网卡名>（建议 lo 或 veth）\n", argv[0]);
        return 1;
    }

    /* 加载失败常见原因：权限不足（-EPERM）、verifier 拒绝（-EACCES，见正文排错节） */
    struct pkt_parse_bpf *skel = pkt_parse_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    int ifindex = if_nametoindex(argv[1]);
    if (!ifindex) {
        fprintf(stderr, "网卡 %s 不存在\n", argv[1]);
        pkt_parse_bpf__destroy(skel);
        return 1;
    }

    /* XDP 挂载点必须显式指定接口（与 tracepoint 的 SEC 推断不同，见第 2 章 1.5）；
     * flags=0 时驱动不支持原生 XDP 一般会回退 generic 模式（随内核版本，以实测为准） */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.pkt_parse, ifindex);
    if (!link) {
        fprintf(stderr, "attach XDP 失败（需 root/CAP_NET_ADMIN；勿挂生产物理网卡）\n");
        pkt_parse_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 已挂载到 %s，每秒打印 UDP 计数，Ctrl-C 退出。\n", argv[1]);

    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.udp_counts);
    while (!exiting) {
        __u32 key = 0;
        __u64 v = 0;
        if (!bpf_map_lookup_elem(fd, &key, &v))
            printf("udp_packets=%llu\n", (unsigned long long)v);
        sleep(1);
    }

    bpf_link__destroy(link);        /* link 释放 + skeleton fd 关闭：引用归零即卸载 */
    pkt_parse_bpf__destroy(skel);
    return 0;
}
```

- [x] **Step 3: 创建 `Makefile`** —— 使用 C4 全链模板，目标 `pkt_parse`；`.bpf.o` 依赖 `pkt_parse.bpf.c vmlinux.h`。

- [x] **Step 4: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-03/01-pkt-parse`
Expected: 退出码 0，生成 `pkt_parse`；`clang -target bpf` 与 `bpftool gen skeleton` 无 error（vmlinux.h 的 `-Wmissing-declarations` warning 属正常）。失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 5: 创建 `README.md`** —— 按 C5 十段结构。关键口径：
  - 演示目标：「验证通过」路径的实物——报文解析三层边界检查 + map 计数最小闭环；与画廊 bad 程序的对照关系。
  - 文件说明表：`pkt_parse.bpf.c` / `pkt_parse.c` / `Makefile`。
  - 环境前提：指向 `../00-env/check-env.sh` 三档结论。
  - 编译与运行：`make`；`sudo ./pkt_parse lo`；触发：`ping 127.0.0.1` 或任意本机 UDP 流量；Ctrl-C 退出即卸载；C1 挂载模式措辞（generic 回退）。
  - 预期输出示例（标注运行级待验证）：

```text
XDP 已挂载到 lo，每秒打印 UDP 计数，Ctrl-C 退出。
udp_packets=42
udp_packets=57
```

  - 代码讲解要点：三层边界检查的推进模式（上一层尾=这一层头）、字节序转换、原子计数选型、XDP 显式挂载与 tracepoint SEC 推断的对比。
  - 常见失败点表：非 root 加载失败（预期，指向 check-env.sh）；挂到物理网卡的警告；`vmlinux.h` 缺失（make 自动生成失败=无 BTF）。
  - 验证状态：按 C6。
  - 参考来源：内核文档 XDP、libbpf-bootstrap、`bpf_helpers.h`。

- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-03/01-pkt-parse/
git commit -m "feat(ch03): 01-pkt-parse 正面锚点示例（BPF+loader+Makefile+README）"
```

---

### Task 3: 失败画廊前三对（01-map-null、02-pkt-bounds、03-ptr-math）

- [x] Task 3: 失败画廊前三对（01-map-null、02-pkt-bounds、03-ptr-math）

**tasks.md 条目**：「Include integrated examples that intentionally fail verification, then show corrected versions.」（前三对）＋「Explain common errors involving unchecked map lookup, packet bounds, invalid pointer arithmetic, stack initialization, loops, and helper misuse.」（前三类）

**Files:**
- Create: `book/examples/chapter-03/02-fail-gallery/01-map-null/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/02-pkt-bounds/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/03-ptr-math/{bad,fixed}.bpf.c`、`Makefile`、`README.md`

**Interfaces:**
- Consumes: C2（公共骨架 + compile-only Makefile + `<SRCS>` 替换表）、C3（三对契约）、C5（画廊 README 六段）、C6（含本章补充条）。
- Produces: 正文 Task 6 三个 card 的走读对象；gallery 总览表前三行（Task 4 汇总）；verify-build.sh 的三个编译项。

- [x] **Step 1: 创建 `01-map-null/`**（bad 与 fixed 按 C3 第 1 行，骨架按 C2 组装；bad 头部注释含「预期被拒：map_value_or_null 直接解引用」）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    *v += 1;                       /* 预期被拒：返回值可能是 NULL，直接解引用 */
    return XDP_PASS;
}
```

fixed 函数体（逐字，仅插入一行检查）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (!v)
        return XDP_PASS;           /* 修复：NULL 分支让类型从 map_value_or_null 定型为 map_value */
    *v += 1;
    return XDP_PASS;
}
```

- [x] **Step 2: 创建 `02-pkt-bounds/`**（bad 与 fixed 按 C3 第 2 行；XDP 类文件头需 C2 include 块 + `ETH_P_IP`/`IPPROTO_UDP` 常量定义）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    struct iphdr *ip = data + sizeof(*eth);
    if (eth->h_proto == bpf_htons(ETH_P_IP) && ip->protocol == IPPROTO_UDP)
        return XDP_DROP;           /* 预期被拒：首个越界读在 eth->h_proto（无任何 data_end 检查） */
    return XDP_PASS;
}
```

fixed 函数体（逐字，仅插入两层检查）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    if (eth->h_proto == bpf_htons(ETH_P_IP) && ip_check(data, data_end))
        return XDP_DROP;
    return XDP_PASS;
}
```

fixed 的 `ip_check` 不引入新函数——按锚点 C1 的两层平铺检查展开（实现者把 `ip_check(...)` 替换为「`struct iphdr *ip = data + sizeof(*eth); if ((void *)(ip + 1) > data_end) return XDP_PASS; if (ip->protocol == IPPROTO_UDP)`」的三行平铺形式；fixed 最终代码形态与 `01-pkt-parse/pkt_parse.bpf.c` 一、二层一致，仅返回值保留 `XDP_DROP`）。

- [x] **Step 3: 创建 `03-ptr-math/`**（bad 与 fixed 按 C3 第 3 行）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    data_end += 4;                 /* 预期被拒：pkt_end 指针禁止算术（内核以此保住报文尾界的可信性） */
    if (data > data_end)           /* 算术结果参与比较：防止 clang -O2 死代码消除掉 offending 指令；
                                    * 写 data > data_end（0 对 4 偏移不等）而非 data + 4 > data_end，
                                    * 后者会被 InstCombine 折叠公共偏移、算术指令被消除（实测教训） */
        return XDP_DROP;
    return XDP_PASS;
}
```

fixed 函数体（逐字）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)       /* 修复：算术放在 packet 指针上，且立刻用界检查收口 */
        return XDP_PASS;
    return XDP_PASS;
}
```

- [x] **Step 4: 为三对各创建 `Makefile`（C2 模板，`<SRCS>` 按替换表）与 `README.md`（C5 画廊六段结构，C3 表的「为什么拒绝/修复原则」展开为完整段落）**

- [x] **Step 5: 编译验证（本机真实执行，硬性）**

Run: `for d in 01-map-null 02-pkt-bounds 03-ptr-math; do make -C book/examples/chapter-03/02-fail-gallery/$d || break; done`
Expected: 三个目录退出码均 0，各生成 `bad.bpf.o` 与 `fixed.bpf.o`（全局约束 3：bad 也必须编译通过）。失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-03/02-fail-gallery/01-map-null/ \
        book/examples/chapter-03/02-fail-gallery/02-pkt-bounds/ \
        book/examples/chapter-03/02-fail-gallery/03-ptr-math/
git commit -m "feat(ch03): 失败画廊前三对（map-null/pkt-bounds/ptr-math）"
```

---

### Task 4: 失败画廊后四对（04-stack-uninit、05-loop、06-helper-misuse、07-kernel-ptr）与画廊总览 README

- [x] Task 4: 失败画廊后四对（04-stack-uninit、05-loop、06-helper-misuse、07-kernel-ptr）与画廊总览 README

**tasks.md 条目**：「Include integrated examples that intentionally fail verification, then show corrected versions.」（后四对）＋「Explain common errors involving unchecked map lookup, packet bounds, invalid pointer arithmetic, stack initialization, loops, and helper misuse.」（后三类）

**Files:**
- Create: `book/examples/chapter-03/02-fail-gallery/04-stack-uninit/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/05-loop/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/06-helper-misuse/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/07-kernel-ptr/{bad,fixed}.bpf.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-03/02-fail-gallery/README.md`

**Interfaces:**
- Consumes: C2/C3/C5/C6（同 Task 3）；Task 3 三对（总览表前七行合并）。
- Produces: 正文 Task 6 四个 card 的走读对象；`02-fail-gallery/README.md` 总览表（七对全量，正文三节引用）；verify-build.sh 的四个编译项。

- [x] **Step 1: 创建 `04-stack-uninit/`**（C3 第 4 行；含 C2 stats map 块）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    __u32 key = 0;
    volatile __u64 verdict;        /* 预期被拒：仅在 if (v) 分支赋值，分支外使用可能未初始化（volatile 抑制编译器 UB 折叠，见头注释） */
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (v)
        verdict = *v;
    if (verdict > 10)
        return XDP_DROP;
    return XDP_PASS;
}
```

fixed 函数体（逐字，仅初始化一处；两侧同带 volatile——那是可观测性手段而非修复内容，diff 仅剩 `= 0`）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    __u32 key = 0;
    volatile __u64 verdict = 0;    /* 修复：声明即初始化，所有路径上都有确定值 */
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (v)
        verdict = *v;
    if (verdict > 10)
        return XDP_DROP;
    return XDP_PASS;
}
```

- [x] **Step 2: 创建 `05-loop/`**（C3 第 5 行）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)
        return XDP_PASS;
    __u32 n = *(__u32 *)data;      /* 预期被拒：上界取自报文且无掩码，迭代次数不可证明 */
    __u64 sum = 0;
    for (__u32 i = 0; i < n; i++)
        sum ^= i;                  /* XOR 而非 +=：线性递推会被 -indvars 折叠成闭式、循环整条消除（见头注释可观测性说明） */
    if (sum == 0xdeadbeef)         /* 消费 sum，防止被优化删除 */
        return XDP_DROP;
    return XDP_PASS;
}
```

fixed 函数体（逐字，仅掩码一处；两侧同用 `sum ^= i`——那是可观测性手段而非修复内容，diff 仅剩掩码）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)
        return XDP_PASS;
    __u32 n = *(__u32 *)data & 0xF;   /* 修复：掩码定界，上界收窄到 [0, 15]（迭代至多 15 次），循环可证明终止 */
    __u64 sum = 0;
    for (__u32 i = 0; i < n; i++)
        sum ^= i;                  /* 与 bad 相同：XOR 防止归纳变量优化把循环折叠成闭式 */
    if (sum == 0xdeadbeef)
        return XDP_DROP;
    return XDP_PASS;
}
```

- [x] **Step 3: 创建 `06-helper-misuse/`**（C3 第 6 行；无 map 块）

bad 函数体（逐字）：

```c
SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    __u32 pid = (__u32)bpf_get_current_pid_tgid();  /* 预期被拒：tracing 类专用 helper，不在 XDP 白名单 */
    if (pid)
        return XDP_DROP;
    return XDP_PASS;
}
```

fixed 函数体（逐字）：

```c
SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    __u32 cpu = bpf_get_smp_processor_id();         /* 修复：改用 XDP 白名单内的等价能力 */
    if (cpu)
        return XDP_DROP;
    return XDP_PASS;
}
```

- [x] **Step 4: 创建 `07-kernel-ptr/`**（C3 第 7 行；tracepoint 类型，无 map 块）

bad 函数体（逐字）：

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int probe_bad(struct trace_event_raw_sys_enter *ctx)
{
    char *fn = (char *)ctx->args[1];   /* 预期被拒：ctx 标量字段强转指针后直接解引用 */
    if (fn[0] == '/')                  /* 任意内核/用户地址必须走 probe 系 helper（第 2 章预告） */
        return 1;
    return 0;
}
```

fixed 函数体（逐字）：

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int probe_fixed(struct trace_event_raw_sys_enter *ctx)
{
    char fn[64] = {};                  /* 修复：probe 系 helper 受控读取到栈缓冲 */
    long n = bpf_probe_read_user_str(fn, sizeof(fn), (const void *)ctx->args[1]);
    if (n > 0 && fn[0] == '/')         /* 5.5+ 用 user 变体；旧版 bpf_probe_read_str 行为随架构，保守注明 */
        return 1;
    return 0;
}
```

- [x] **Step 5: 为四对各创建 `Makefile`（C2 模板）与 `README.md`（C5 画廊六段）**

- [x] **Step 6: 创建 `02-fail-gallery/README.md`**：画廊总览——学习路径（建议按 01→07 顺序逐对 diff）；七对总览表（列：目录 / 失败信号 / 预期日志关键行 / 修复原则一句话，与 C3 表一致）；「bad 也必须编译通过」的验证口径说明（全局约束 3 + C6 本章补充条）；安全警告（本画廊只编译不加载）。

- [x] **Step 7: 编译验证（本机真实执行，硬性）**

Run: `for d in 04-stack-uninit 05-loop 06-helper-misuse 07-kernel-ptr; do make -C book/examples/chapter-03/02-fail-gallery/$d || break; done`
Expected: 四个目录退出码均 0，各生成两个 `.bpf.o`。失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 8: 提交**

```bash
git add book/examples/chapter-03/02-fail-gallery/
git commit -m "feat(ch03): 失败画廊后四对与画廊总览（stack/loop/helper/kernel-ptr）"
```

---

### Task 5: 正文「学习目标/本章导读/一、验证器的目标与安全模型/二、验证流水线内核实现分析」

- [x] Task 5: 正文「学习目标/本章导读/一、验证器的目标与安全模型/二、验证流水线内核实现分析」

**tasks.md 条目**：「Write full chapter prose explaining verifier goals, control-flow graph construction, state exploration, state pruning, pointer types, bounds checks, stack limits, and helper constraints.」（一节 + 二节的概念部分）＋「Analyze `kernel/bpf/verifier.c`, `bpf_verifier_env`, `bpf_reg_state`, and the high-level verification pipeline.」

**Files:**
- Create: `book/chapters/03-verifier-and-safety-guarantees.md`

**Interfaces:**
- Consumes: C7（标题逐字固定）；第 2 章预告回收清单（现状基线表①③④⑤⑥⑦⑧⑨）；附录 B/C 路径。
- Produces: 章节文件与前四节成稿；「二」节的验证流水线框架（Task 6 画廊 card 与 Task 7 实战的引用锚点）。

- [x] **Step 1: 写「# 第 3 章」题头、「学习目标」「本章导读」（约 2–2.5k）**
  - 学习目标：成果导向 6–8 条（能说出 verifier 的三类安全保证及其在加载路径的位置；能解释状态探索与剪枝为什么既安全又高效；能读懂 `bpf_reg_state` 指针状态族的含义；能独立跑通画廊七对的 bad/fixed 对照并预期拒绝结果；能逐行解读一条真实 verifier 拒绝日志；能完成 2 项课后练习）。
  - 本章导读：回收第 2 章预告（八处预告以列表呈现，逐条注明本章兑现小节）；学习路线图——先安全模型（一）→ 再内核流水线（二）→ 再失败画廊（三）→ 最后实战读日志（四）。
- [x] **Step 2: 写「一、验证器的目标与安全模型」（3.5–4.5k）**——小节锚点固定为 `### 1.1`–`### 1.4`：
  - 1.1 为什么内核不能直接执行任意 C：内核态内存安全/可终止/权限最小化三类保证；与内核模块的对照（第 1 章对比的深化回指）；**教师视角**段（类比：verifier 是「入境安检」——不是不让进，而是只放行能证明自己行为有界的访客；误区：verifier 不是运行时检查器）。
  - 1.2 静态验证与运行时定界的分工：第 2 章 3.2 的「数组 map 越界 helper 内返回 NULL」回指展开——verifier 证明不了的交给运行时兜底，两层防线的成本权衡。
  - 1.3 验证发生在哪一步：加载路径回指（第 2 章 4.3）——`BPF_PROG_LOAD` → license 检查 → **verifier** → JIT；拒绝时返回 `-EACCES` 并给出日志（预告⑥⑧回收）；「编译成功≠能加载」正式展开（预告①回收）。
  - 1.4 安全模型的边界：verifier 证明的是「这一版程序在这类输入下安全」，不含逻辑正确性；保守性取舍（合法程序也可能被拒）——为三节画廊「为什么我的对程序也被拒」伏笔；**学生动手**段（列出自己预见的三种「合法但难证明」的写法）+ 检查点（3 问 + 提示）。
- [x] **Step 3: 写「二、验证流水线内核实现分析」（8–11k）**——小节锚点固定为 `### 2.1`–`### 2.7`；纯讲解 + 内核路径引用（`kernel/bpf/verifier.c`），对随版本演进的函数名/结构体字段用保守措辞（「以所用内核源码为准」）：
  - 2.1 验证入口与总体管线：`bpf_check()` 在 `BPF_PROG_LOAD` 中的位置；`bpf_verifier_env`（验证环境：程序、日志缓冲、严格级别）与 `bpf_verifier_state`（探索中的机器状态）两个核心结构的角色（预告⑥回收主线）。
  - 2.2 指令级模拟执行：逐条指令推进的抽象解释（非真实执行）；寄存器/栈/映射值的抽象取值域；为什么「模拟」就能保证「真实执行」安全（过近似 safety 论证，通俗讲法）。
  - 2.3 寄存器状态族 `bpf_reg_state`：标量、`PTR_TO_CTX`、`PTR_TO_MAP_VALUE`、`PTR_TO_PACKET`/`PTR_TO_PACKET_END`、`PTR_TO_STACK`、`PTR_TO_BTF_ID`、`SCALAR_VALUE` 等主要指针类型的语义表；每种类型「允许什么算术、要求什么检查」；ch02 预告⑦回收——ctx 申报访问与任意内核指针 probe 界限的类型学解释。
  - 2.4 控制流探索与状态合并：分支两条路各模拟一遍、汇合点状态合并（取保守交集）；状态爆炸为什么没有发生——**剪枝**：已见过的等价状态直接跳过（配合 precision/精度追踪一句定位，保守措辞）；图示指向可视化节。
  - 2.5 边界推导：标量范围跟踪（`& 0xF` 后上界变 16 的例子，为画廊 card ⑤ 伏笔）；packet 指针比较建立 `data`/`data_end` 关系（为 card ② 伏笔）；有符号/无符号与掩码修正（为练习 2 伏笔）。
  - 2.6 栈模型与终止性：512 字节栈限、栈槽初始化跟踪（为 card ④ 伏笔）；「跳转偏移必须落在程序内 + 有界循环」的可终止性（预告⑤回收；5.3 前后差异一句话，保守措辞）。
  - 2.7 helper 约束与白名单：按程序类型登记的 `bpf_verifier_ops`/helper 可用面（预告②⑦回收）；参数原型 `bpf_func_proto` 的类型检查（`ARG_PTR_TO_MAP_KEY` 等）；日志如何产生与详细度控制（`verbose`、log_level 档位，指向四节实战）；**教师视角**段（讲解顺序设计说明：为什么先类型后流程再边界）+ **学生动手**段（在本机内核源码或 docs 中定位 `bpf_reg_state` 定义并核对本节表）+ 检查点（4 问 + 提示）。
- [x] **Step 4: 核对（硬性）**

Run: `grep -c "^## " book/chapters/03-verifier-and-safety-guarantees.md && grep -c "^### 1\." book/chapters/03-verifier-and-safety-guarantees.md && grep -c "^### 2\." book/chapters/03-verifier-and-safety-guarantees.md && wc -m book/chapters/03-verifier-and-safety-guarantees.md`
Expected: `4`（学习目标/导读/一/二）、`4`、`7`；`wc -m ≥ 14000`（本任务中段值参考；Task 10 终检 35–48k）。

- [x] **Step 5: 提交**

```bash
git add book/chapters/03-verifier-and-safety-guarantees.md
git commit -m "docs(ch03): 学习目标/导读/安全模型/验证流水线内核分析"
```

---

### Task 6: 正文「三、失败画廊：七类典型拒绝」

- [x] Task 6: 正文「三、失败画廊：七类典型拒绝」

**tasks.md 条目**：「Include integrated examples that intentionally fail verification, then show corrected versions.」（正文呈现部分）＋「Explain common errors involving unchecked map lookup, packet bounds, invalid pointer arithmetic, stack initialization, loops, and helper misuse.」（正文讲解部分）

**Files:**
- Modify: `book/chapters/03-verifier-and-safety-guarantees.md`

**Interfaces:**
- Consumes: C7 标题；Task 3/4 的七对文件（代码块逐字对应）；C8 交叉链接；2.3/2.5/2.6/2.7 小节（每个 card 回指对应原理小节）。
- Produces: 七个 card 固定结构（Task 10 审计依据）；每个 card 的「为什么拒绝」回指小节号。

- [x] **Step 1: 写「三、失败画廊：七类典型拒绝」（9–12k）**——小节锚点固定为 `### 3.1`–`### 3.7`，每类一个 card，**每个 card 固定六段**：背景问题 → bad 代码（贴对应文件全文）→ 预期 verifier 日志关键行（标注「预期输出示例 / 运行级待验证」+ 版本差异注）→ 为什么拒绝（原理讲解，回指二节小节号）→ fixed 代码（贴对应片段）→ 修复原则一句话；各 card 对应关系：
  - 3.1 未检查 map lookup（画廊 01）→ 回指 2.3（map_value_or_null 定型）；第 2 章 3.2 运行时定界回指。
  - 3.2 报文边界（画廊 02）→ 回指 2.5（packet 指针比较）；与锚点 `01-pkt-parse` 的对照（同一个检查在「通过路径」的位置）。
  - 3.3 无效指针运算（画廊 03）→ 回指 2.3（pkt_end 不可算术的类型学理由）。
  - 3.4 栈未初始化（画廊 04）→ 回指 2.6（栈槽初始化跟踪）。
  - 3.5 循环可终止性（画廊 05）→ 回指 2.5（掩码定界）与 2.6（终止性）；5.3 前后差异与 `bpf_loop` 一句话（保守措辞）。
  - 3.6 helper 误用（画廊 06）→ 回指 2.7（白名单按程序类型登记）；第 2 章 1.3 失败点预告回收。
  - 3.7 任意内核指针（画廊 07）→ 回指 2.3/2.7（probe 界限）；第 2 章「证明过程是第 3 章正题」预告的正式兑现。
- [x] **Step 2: 三节末尾落双视角**：**教师视角**段（为什么用「先见尸体再学解剖」的顺序——七对按从最常见到最隐蔽排列；读者容易卡住的地方：把「被拒」当编译错误排查）+ **学生动手**段（逐一 `make` 七对、diff bad/fixed、把预期日志关键行抄进自己的笔记）+ 检查点（3 问 + 提示，含「为什么 bad 程序能编译通过」）。
- [x] **Step 3: 核对（硬性）**

Run: `grep -c "^### 3\." book/chapters/03-verifier-and-safety-guarantees.md && grep -c "预期 verifier 日志" book/chapters/03-verifier-and-safety-guarantees.md && grep -c "examples/chapter-03/02-fail-gallery/" book/chapters/03-verifier-and-safety-guarantees.md && wc -m book/chapters/03-verifier-and-safety-guarantees.md`
Expected: `7`、`≥ 7`（每个 card 一处）、`≥ 7`（七个 card 均标注源文件路径）；`wc -m ≥ 24000`（中段值参考）。

- [x] **Step 4: 提交**

```bash
git add book/chapters/03-verifier-and-safety-guarantees.md
git commit -m "docs(ch03): 失败画廊七类典型拒绝走读"
```

---

### Task 7: 正文「四、实战：读懂你的第一条 verifier 日志」（五层环境紧凑版 + 代码走读 + 双视角 + 检查点）

- [x] Task 7: 正文「四、实战：读懂你的第一条 verifier 日志」（五层环境紧凑版 + 代码走读 + 双视角 + 检查点）

**tasks.md 条目**：「Write full chapter prose explaining verifier goals, control-flow graph construction, state exploration, state pruning, pointer types, bounds checks, stack limits, and helper constraints.」（实战落点部分：边界检查与栈限在锚点代码中的落地）

**Files:**
- Modify: `book/chapters/03-verifier-and-safety-guarantees.md`

**Interfaces:**
- Consumes: Task 2 的 01-pkt-parse 全部文件与 Task 3 的 02-pkt-bounds（代码块逐字对应）；`../00-env/check-env.sh` 三档结论；C6 验证状态三条；C7 标题；C1 挂载模式措辞。
- Produces: 5.1 的验证状态约定表（Task 10 审计依据）；「四」节末「独立示例」链接（C8）。

- [x] **Step 1: 写 `### 5.1 五层环境（紧凑版）`（2.5–3.5k）**——五层用 `#### 第一层：依赖环境` … `#### 第五层：运行效果` 定长小节，口径与第 2 章 5.1 一致但面向「加载即部署」的单机验证场景：
  - 第一层 依赖环境：内核 5.10+ 与 `CONFIG_DEBUG_INFO_BTF` 要求；权限矩阵表（root / CAP_BPF+CAP_NET_ADMIN / 非 root 三档 × 可编译/可加载/可挂载三列——XDP 挂载需 CAP_NET_ADMIN，与第 2 章 tracepoint 权限档的差异点明）；工具清单与检查方法——引用 `bash book/examples/chapter-03/00-env/check-env.sh` 及其三档结论解读（本机 `unprivileged_bpf_disabled=2` 语境示例输出，标「本地验证」）。
  - 第二层 编译环境：clang/LLVM 18.x、libbpf-dev、bpftool 7.x、make；各版本验证命令（本机实测输出标「本地验证」）；`apt install` 一行说明。
  - 第三层 部署环境：**「加载即部署」是本章特殊性**——验证发生在部署瞬间，`bpftool prog load` 与 loader 的 `open_and_load` 都是验证现场；XDP 挂载目标建议 `lo` 或 veth；**安全警告段**（全局约束 8 措辞，逐字）+ 与第 17 章「勿挂物理网卡」的呼应说明。
  - 第四层 运行环境：`sudo ./pkt_parse lo` 启动、Ctrl-C 卸载；C1 挂载模式措辞（generic 回退）；无规则下发、无 pin（第 2 章 4.5 对照）。
  - 第五层 运行效果：UDP 计数增长预期输出（标注运行级待验证）；**验证状态约定表**——C6 三条 + 本章补充条（bad 程序的「编译级已通过/加载级预期被拒待验证」双状态）逐字引入。
- [x] **Step 2: 写 `### 5.2 代码走读：通过与被拒两条路径`（3.5–4.5k）**——四段组织：
  - **教师视角引入**：为什么用「同一类检查的正反两面」教学（锚点通过路径 + 画廊 02 被拒路径互为镜像）；读者容易卡住的两处——「预期日志哪里来的」（不能本机实测，教学推导口径）、「日志第一行该看什么」。
  - 核心代码原理讲解：贴 `book/examples/chapter-03/01-pkt-parse/pkt_parse.bpf.c` 逐段讲解（三层边界检查推进模式、字节序、原子计数、`XDP_PASS` 语义）；贴 `book/examples/chapter-03/02-fail-gallery/02-pkt-bounds/bad.bpf.c` 与其预期拒绝日志（逐行读法：错误行在最后、`R2` 是哪个寄存器、`off`/`size` 指什么——「预期输出示例 / 运行级待验证」标注）。
  - **学生动手**：`make` 锚点、`sudo ./pkt_parse lo`（满足权限时）、生成一次本机 UDP 流量（如 `dig @127.0.0.53 localhost`）观察计数（`ping` 是 ICMP 不计数——实现期实测修正）；无 root 时改为 `make` 画廊 02 的 bad/fixed 并对照预期日志自查；动手清单用 checkbox。
  - 检查点：3 问 + 提示——(1) 前两层边界检查删掉任一行为什么必被拒？第三层（UDP 头）检查删掉后为什么预期仍能通过？（提示：检查是为解引用服务的——第三层之后无任何 udp 字段读取；实现期实测修正原「三行删掉任一都被拒」的可证伪断言，运行级待验证）(2) 预期日志里 `R2` 对应源码哪个变量？(3) 为什么本机的拒绝日志可能与本书不同？（提示：全局约束 7 版本差异）。
- [x] **Step 3: 核对（硬性）**

Run: `grep -c "examples/chapter-03/01-pkt-parse/" book/chapters/03-verifier-and-safety-guarantees.md && grep -c "#### 第" book/chapters/03-verifier-and-safety-guarantees.md && grep -c "教师视角" book/chapters/03-verifier-and-safety-guarantees.md && grep -c "学生动手" book/chapters/03-verifier-and-safety-guarantees.md && wc -m book/chapters/03-verifier-and-safety-guarantees.md`
Expected: `≥ 2`（两份源文件路径标注各至少一次）；`5`（五层定长小节）；`≥ 3`（一/三/四三节均有）；`≥ 3`；`wc -m ≥ 30000`（中段值参考）。

- [x] **Step 4: 提交**

```bash
git add book/chapters/03-verifier-and-safety-guarantees.md
git commit -m "docs(ch03): 五层环境紧凑版与实战两条路径走读"
```

---

### Task 8: 正文「可视化/常见问题与排错/课后扩展任务/总结/本章参考来源」

- [x] Task 8: 正文「可视化/常见问题与排错/课后扩展任务/总结/本章参考来源」

**tasks.md 条目**：「Add text visualization of verifier state branching and pruning.」＋「Add troubleshooting, summary, and references including kernel docs, LWN, and bpf mailing list discussions.」

**Files:**
- Modify: `book/chapters/03-verifier-and-safety-guarantees.md`

**Interfaces:**
- Consumes: Task 5–7 成稿；二节探索/剪枝与边界推导结论（可视化图 1 素材）；C3 七对与练习 2 的日志信号（排错速查表素材）；Task 9 未落地前先写好课后任务表（C9，与 exercises/README 逐字一致）。
- Produces: 全部 11 个 `##` 一级小节齐备（C7 收齐）；课后任务表（Task 9 exercises/README 总览表与其逐字一致）。

- [x] **Step 1: 写「可视化：验证器状态探索与剪枝」（1.5–2k）**——两张 text 图（` ```text ` 代码块，格式遵循 `book/STYLE.md` 可视化节）：
  - 图 1 状态探索树：一个双分支小程序（如锚点的字节序判断）的探索树——两条路径各自的寄存器状态、汇合点合并、等价状态剪枝标记（对应二节 2.4；任务条目「state branching and pruning」的直接落点）。
  - 图 2 加载-验证双结局流程图：`BPF_PROG_LOAD` → license 检查 → verifier（探索/剪枝循环）→ 通过（JIT → 挂载）或拒绝（`-EACCES` + 日志）双结局（对应一节 1.3 与四节实战）。
  - 图后各配 2–3 段「怎么读这张图」的讲解（禁止裸图）。
- [x] **Step 2: 写「常见问题与排错」（2.5–3.5k）**——四组，每组「现象/原因与处理」表：
  - verifier 日志速查表：八行信号→根因→修复（C3 七对 + 练习 2 的负偏移信号「R2 min value is negative」），每行链接对应画廊目录/练习目录。
  - 日志获取与详细度：loader 侧 libbpf 日志回调（`libbpf_set_strict_mode`/print 函数一句话口径）、`bpftool prog load` 的日志输出方式、日志太长被截断的处理（保守措辞，随 libbpf 版本）。
  - 权限类失败区分：`-EPERM`（非 root/无 CAP_BPF）与 `-EACCES`（verifier 拒绝）的判别流程——第 2 章排错节「详析指向第 3 章」预告的兑现（预告⑧回收）。
  - 版本差异导致的拒绝差异：有界循环（5.3）、`bpf_probe_read_user_str`（5.5）、`bpf_loop`（5.17）演进表 + 「以所用内核为准」保守措辞。
- [x] **Step 3: 写「课后扩展任务」（表格 + 防剧透说明段）**——固定表格（C9 逐字，Task 9 的 exercises/README.md 总览表须与此逐字一致）+ 防剧透说明段：先自行实现再看参考；README 结构（任务在前、思路独立成节放后部）。
- [x] **Step 4: 写「总结」（0.8–1.2k）**：验证器三类保证回顾；安全模型「静态证明 + 运行时定界」双防线一句话；**预告勾稽表**——第 2 章八处预告（现状基线清单）与本章兑现小节的对照表（预告⑨回收的正式落点）；通往第 4 章的桥（程序类型决定 helper 白名单与上下文，verifier 的「游戏规则」随挂载点变化）。
- [x] **Step 5: 写「本章参考来源」**：按 `book/STYLE.md` 格式列——Linux kernel documentation（Documentation/bpf/verifier.rst、BPF program types、helpers）、LWN.net verifier 系列（state/pruning/pointer types 文章）、bpf mailing list（有界循环与 precision tracking 经典讨论线索）、Andrii Nakryiko（libbpf 与 verifier 日志相关文章）、ebpf.top BPF 入门系列。
- [x] **Step 6: 结构收齐核对（硬性）**

Run: `grep -c "^## " book/chapters/03-verifier-and-safety-guarantees.md && grep -c "计划\|建议\|后续补齐\|占位" book/chapters/03-verifier-and-safety-guarantees.md | cat`
Expected: `11`（C7 全部一级小节齐备）；占位式表述 0 命中（「预期输出」含「预期」不算——grep 词表不含「预期」）。

- [x] **Step 7: 提交**

```bash
git add book/chapters/03-verifier-and-safety-guarantees.md
git commit -m "docs(ch03): 可视化/排错/课后任务/总结/参考来源"
```

---

### Task 9: 练习参考实现（01-fix-challenge、02-log-diagnosis）与 exercises 总览

- [x] Task 9: 练习参考实现（01-fix-challenge、02-log-diagnosis）与 exercises 总览

**tasks.md 条目**：「Create standalone Chapter 3 verifier examples under `book/examples/chapter-03/`.」（exercises 部分）；同时落实 `book/STYLE.md` 实战模板规则 4（完整可编译参考实现 + 防剧透 README + 验证状态标注）。

**Files:**
- Create: `book/examples/chapter-03/exercises/README.md`
- Create: `book/examples/chapter-03/exercises/01-fix-challenge/broken.bpf.c`
- Create: `book/examples/chapter-03/exercises/01-fix-challenge/fixed.bpf.c`
- Create: `book/examples/chapter-03/exercises/01-fix-challenge/Makefile`
- Create: `book/examples/chapter-03/exercises/01-fix-challenge/README.md`
- Create: `book/examples/chapter-03/exercises/02-log-diagnosis/reproduce.bpf.c`
- Create: `book/examples/chapter-03/exercises/02-log-diagnosis/Makefile`
- Create: `book/examples/chapter-03/exercises/02-log-diagnosis/README.md`

**Interfaces:**
- Consumes: C2（compile-only Makefile，`<SRCS>` 替换表）、C3 公共骨架、C5 练习防剧透七段、C6（含本章补充条）、Task 8 课后任务表（C9 总览表逐字一致）。
- Produces: verify-build.sh 的两个练习编译项；练习 README 的「参考实现思路」节与正文排错节日志速查表呼应。

- [x] **Step 1: 创建 `exercises/01-fix-challenge/broken.bpf.c`**（逐字如下——含三处组合错误的独立坏程序，冒烟测试已验证编译；bad 口径同全局约束 3）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 任务体：修复挑战（预期被拒：三处组合错误）。
 * 错误不重复画廊任何单一 case，需综合运用本章知识定位：
 *   (1) 报文边界缺失（对照画廊 02）
 *   (2) 未检查 map lookup（对照画廊 01）
 *   (3) 未初始化栈变量（对照画廊 04）
 * 破解顺序建议：从日志第一处报错入手，修一处再看下一处。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

SEC("xdp")
int challenge(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;
    struct iphdr *ip = data + sizeof(*eth);   /* 错误(1)：两层均无 data_end 检查 */
    if (ip->protocol == 17)
        return XDP_DROP;

    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);   /* 错误(2)：未检查 NULL */
    volatile __u64 verdict;                   /* 错误(3)：仅条件赋值（volatile 为可观测性装置而非错误本身——裸形式的栈槽读会被 clang -O2 寄存器分配消除，实测教训） */
    if (v)
        verdict = *v;
    if (verdict > 0)
        *v = verdict + 1;                     /* 连带：v 为 NULL 时此行同样危险 */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 2: 创建 `exercises/01-fix-challenge/fixed.bpf.c`**（逐字如下——参考实现，冒烟测试已验证编译）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 参考实现：三处错误逐一修复后的程序。
 * 修复要点与画廊对照：(1) 两层边界检查（画廊 02）(2) NULL 检查（画廊 01）
 * (3) 声明即初始化（画廊 04）；连带修复：v 的使用全部收进 NULL 检查之后。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

SEC("xdp")
int challenge(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)         /* 修复(1a)：以太网头边界 */
        return XDP_PASS;
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)          /* 修复(1b)：IPv4 头边界 */
        return XDP_PASS;
    if (ip->protocol == 17)
        return XDP_DROP;

    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (!v)                                   /* 修复(2)：NULL 分支 */
        return XDP_PASS;
    __u64 verdict = *v;                       /* 修复(3)：有确定初值 */
    *v = verdict + 1;                         /* 连带修复：v 已定型为 map_value */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 3: 创建 `exercises/02-log-diagnosis/reproduce.bpf.c`**（逐字如下——复现「R2 min value is negative」日志的程序，冒烟测试已验证编译）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 参考实现：复现「R2 min value is negative, either use unsigned
 * or 'var &= const'」拒绝日志的最小程序（预期被拒）。
 * 根因：协议字段按有符号数解读后直接当 packet 指针偏移，smin 为负——
 * verifier 拒绝一切「可能向报文头前方走」的指针算术（对照画廊 03：pkt_end
 * 禁止算术；本题是 pkt 指针允许算术但范围必须非负）。
 * 修复方向二选一：改无符号类型，或先掩码定界（正文 2.5）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

SEC("xdp")
int reproduce(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)          /* 先读 4 字节的合法性要单独证明 */
        return XDP_PASS;
    __s32 off = *(__s32 *)data;       /* 有符号字段：smin = -2^31 */
    if (data + off > data_end)        /* 预期被拒：负偏移指针算术 */
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 4: 创建两个 `Makefile`** —— C2 模板替换：练习 1 `<SRCS> := broken.bpf.c fixed.bpf.c`；练习 2 `<SRCS> := reproduce.bpf.c`。

- [x] **Step 5: 创建两个练习 `README.md`** —— 按 C5 防剧透七段结构。关键口径：
  - 练习 1 `## 提示`：三类信号（报文边界/map_value_or_null/未初始化栈）；「从日志第一处报错入手逐个修」的方法；`## 任务` 验收标准：修到编译通过 + 能说明三处错误各自的 verifier 拒绝理由；满足权限时 `bpftool prog load` 加载成功（运行级待验证）。`## 参考实现思路`（分界线后）：三处修复与画廊对照表、连带危险（`*v` 在 NULL 检查外）说明。
  - 练习 2 `## 提示`：有符号标量的 smin/smax 跟踪（正文 2.5）；修复两方向（无符号/掩码）；`## 任务` 验收标准：写出复现程序 + 逐行解读预期日志 + 给出两种修复。`## 参考实现思路`：复现程序讲解、预期日志逐行解读（`R2` 对应 `off`）、与画廊 03 的类型学区分。
  - 两者 `## 预期输出示例`（运行级待验证）：练习 1 给「加载成功 + `bpftool prog show` 可见」的预期输出；练习 2 给完整预期拒绝日志（标注版本差异）。`## 验证状态` 按 C6（含本章补充条）。
- [x] **Step 6: 创建 `exercises/README.md`**：两项任务总览表（C9 逐字一致）+ 防剧透阅读法（先做后看、思路节在分界线后）+ 各练习构建命令（各目录 `make`）+ 验证状态汇总表（编译级/运行级，按 C6）。

- [x] **Step 7: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-03/exercises/01-fix-challenge && make -C book/examples/chapter-03/exercises/02-log-diagnosis`
Expected: 两个目录退出码均 0（练习 1 生成 `broken.bpf.o` 与 `fixed.bpf.o`——broken 也必须编译通过；练习 2 生成 `reproduce.bpf.o`）。失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 8: 提交**

```bash
git add book/examples/chapter-03/exercises/
git commit -m "feat(ch03): 课后练习参考实现（修复挑战与日志诊断）"
```

---

### Task 10: scripts/verify-build.sh、全量编译验证、正文交叉链接与篇幅终检

**tasks.md 条目**：「Create standalone Chapter 3 verifier examples under `book/examples/chapter-03/`.」（scripts 部分）＋「Add troubleshooting, summary, and references including kernel docs, LWN, and bpf mailing list discussions.」（验证状态全量标注收口）

**Files:**
- Create: `book/examples/chapter-03/scripts/verify-build.sh`（可执行，`chmod +x`）
- Modify: `book/chapters/03-verifier-and-safety-guarantees.md`（交叉链接）
- Modify: `book/examples/chapter-03/README.md`（回填验证摘要）

**Interfaces:**
- Consumes: Task 1–9 全部产物；C6/C8。
- Produces: 全绿 verify-build 记录（章级 README 验证状态说明区）；Task 11 主审依据。

- [ ] **Step 1: 创建 `scripts/verify-build.sh`**（完整内容如下）

```bash
#!/usr/bin/env bash
# 第 3 章编译级验证：00-env 自检 + 01-pkt-parse 全链 + 画廊七对（bad/fixed 都必须编译通过）
#                    + exercises 逐目录 make 汇总。
# 无特权可执行；退出码 0=全绿，1=存在 FAIL。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-03
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

echo "== 锚点编译（全链） =="
run "01-pkt-parse" make -C 01-pkt-parse clean all

echo "== 失败画廊编译（bad 与 fixed 都必须通过） =="
run "02-fail-gallery/01-map-null"       make -C 02-fail-gallery/01-map-null clean all
run "02-fail-gallery/02-pkt-bounds"     make -C 02-fail-gallery/02-pkt-bounds clean all
run "02-fail-gallery/03-ptr-math"       make -C 02-fail-gallery/03-ptr-math clean all
run "02-fail-gallery/04-stack-uninit"   make -C 02-fail-gallery/04-stack-uninit clean all
run "02-fail-gallery/05-loop"           make -C 02-fail-gallery/05-loop clean all
run "02-fail-gallery/06-helper-misuse"  make -C 02-fail-gallery/06-helper-misuse clean all
run "02-fail-gallery/07-kernel-ptr"     make -C 02-fail-gallery/07-kernel-ptr clean all

echo "== 练习编译 =="
run "exercises/01-fix-challenge" make -C exercises/01-fix-challenge clean all
run "exercises/02-log-diagnosis" make -C exercises/02-log-diagnosis clean all

echo
echo "== 汇总: PASS=$pass FAIL=$fail =="
[ "$fail" -eq 0 ]
```

- [ ] **Step 2: 首次全量运行（硬性验收）**

Run: `bash book/examples/chapter-03/scripts/verify-build.sh`
Expected: `PASS 00-env`、`PASS 01-pkt-parse`、`PASS 02-fail-gallery/01..07`（七行）、`PASS exercises/01-fix-challenge`、`PASS exercises/02-log-diagnosis`；汇总 `FAIL=0`，退出码 0。任何 FAIL 回到对应任务修复后重跑。
记录：把本次运行摘要（PASS/FAIL 数与工具版本）替换进 `book/examples/chapter-03/README.md` 验证状态说明区的占位句。

- [ ] **Step 3: 正文交叉链接**（C8 惯例）：章首「本章独立示例已全部落地到 [`book/examples/chapter-03/`](../examples/chapter-03/)」与目录总览；5.2 与三节每个代码块前后标注对应文件路径；「一/二/三/四」各节末尾「**独立示例**」链接（二节末为附录 B/C 链接）；三节各 card 链接画廊目录；课后扩展任务节链接 exercises/ 两目录与总览 README。
- [ ] **Step 4: 交叉链接核对（硬性）**

Run: `grep -c "](../examples/chapter-03/" book/chapters/03-verifier-and-safety-guarantees.md && grep -c "](../appendix/" book/chapters/03-verifier-and-safety-guarantees.md`
Expected: `≥ 10`（锚点 + 画廊七对 + exercises 两目录 + 顶层/画廊 README）；`≥ 1`。

- [ ] **Step 5: 篇幅终检（硬性）**

Run: `wc -m book/chapters/03-verifier-and-safety-guarantees.md`
Expected: 35000–48000。低于下限：优先补原理讲解而非注水（写作纪律——每个概念钉回读者已见过的代码）；高于上限：压缩重复叙述，不删原理。

- [ ] **Step 6: 提交**

```bash
git add book/examples/chapter-03/ book/chapters/03-verifier-and-safety-guarantees.md
git commit -m "docs(ch03): 交叉链接示例资产并完成全量编译级验证标注"
```

---

### Task 11: 主审（teaching completeness）与 tasks.md 勾选（收口）

**tasks.md 条目**：「Main-agent review for technical accuracy and clear "expected rejection" labels.」；同时统一勾选「Chapter 3」全部 8 条。

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`（勾选）
- 产出：审查结论进入最终回复（不创建报告文件）

- [ ] **Step 1: 逐项审计并记录结论**（发现问题回对应任务修复后复验；全绿才进入 Step 2）：
  1. 教学完整性四条（STYLE.md 实战模板）：五层环境（5.1 五个 `####` 层，每层落到命令/脚本/表格）；核心代码原理注释（抽查 `pkt_parse.bpf.c`、画廊 `bad.bpf.c` 若干：字段语义/边界检查原因/helper 选型/map 选型/失败模式五类注释在场，bad 文件「预期被拒」注释在场）；双视角（一/三/四三节「教师视角」「学生动手」+ 检查点齐备）；课后练习（2 项目录、防剧透七段 README、参考实现可编译、验证状态标注）。
  2. 结构级（设计 §8）：C7 全部一级小节齐备；三节七 card 与画廊目录一一对应；5.2 代码块与示例文件一一对应（抽查 3 处路径）；可视化两张 text 图均有解读段。
  3. 术语与保守措辞：映射/辅助函数/程序类型/挂载点译名一致；「英文（中文解释）」首次出现格式；verifier 日志「随内核版本略有差异」统一口径在场；5.10+ 演进点保守措辞；全文零「已验证通过」式运行级表述（grep 复核）；**「预期被拒」标注逐对在场**（本条为 tasks.md 明确审查项）。
  4. 矩阵专项核对：一节四条（目标/分工/位置/边界，1.1–1.4）；二节七条（env/模拟执行/reg_state/探索剪枝/边界/栈与终止性/helper 约束，2.1–2.7）；七类常见错误（3.1–3.7 与画廊目录对齐）；可视化（探索剪枝 + 双结局，图 1/图 2）；排错/总结/参考含 kernel docs/LWN/bpf mailing list（Task 8）；预告勾稽表（总结节）。
- [ ] **Step 2: 勾选 tasks.md**：将「Chapter 3: Verifier and Safety Guarantees」8 条全部置为 `[x]`（仅当对应交付物真实存在且验收通过）。
- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch03): 勾选 Chapter 3 矩阵任务（build 阶段完成）"
```

按变更工作流在回复中列出：变更文件清单、参考来源、已执行验证（verify-build 全绿记录）、已知限制（运行级待验证清单）。

---

## 计划自查记录（writing-plans Self-Review）

1. **Spec 覆盖**：设计 §2 示例资产 → Task 1（README/00-env/.gitignore）、Task 2（01-pkt-parse）、Task 3/4（画廊七对 + 总览）、Task 9（exercises 全目录）、Task 10（verify-build.sh）；设计 §3 正文 → Task 5（学习目标/导读/一/二）、Task 6（三）、Task 7（四：五层 + 走读 + 双视角 + 检查点）、Task 8（可视化/排错/课后/总结/参考）；设计 §4 练习 → Task 9；设计 §5 验证策略 → Task 2/3/4/9（单目录硬性）与 Task 10（全量硬性）；设计 §6 风险 → 各任务缓解（日志版本差异入全局约束 7 与 C3 口径、内核分析深度入 2.1–2.7 保守措辞、与 ch02 分工入回收清单映射、画廊节膨胀入 card 六段结构、演进点入排错第四组、加载级待验证入全局约束 2）；tasks.md 8 条 → 条 1（Task 5 一/二节 + Task 7 实战落点）、条 2（Task 5 二节）、条 3（Task 3/4 七对 + Task 6 正文）、条 4（Task 1/2/3/4/9/10）、条 5（Task 3/4 文件对 + Task 6 讲解）、条 6（Task 8 图 1）、条 7（Task 8）、条 8（Task 11）。无遗漏条目；Chapter 1 遗留与第 9 章改造按设计 §7 明确排除在范围外。
2. **占位符扫描**：全部任务给出实际内容——锚点两份源码、画廊七对 bad/fixed 函数体、练习三份源码、check-env.sh、verify-build.sh 逐字全文；Makefile 经 C2/C4 模板 + 替换表定死；README 给出结构 + 关键口径；正文任务给出节标题锚点、讲解要点清单、篇幅锚点与 grep 硬性核对；无「TBD/TODO/类似 Task N」表述（Task 3 Step 2 的 `ip_check` 占位已改为「替换为三行平铺形式」的明确指令——该占位符不是内容缺失而是防止 fixed 与锚点不一致的写作约束，最终代码形态已定死）；正文小节标题经 C7 逐字固定。
3. **类型一致性**：`ETH_P_IP`/`IPPROTO_UDP` 两侧一致（C1/C2/C3，uapi 常量手工定义已冒烟验证）；`stats` map 块逐字一致（C2，仅 ①④ 与练习 1 使用）；函数名 `probe_bad`/`probe_fixed`/`pkt_parse`/`challenge`/`reproduce` 与 skeleton 前缀一一对应（`pkt_parse_bpf`）；compile-only Makefile `<SRCS>` 替换表（C2）与目录/文件名一致；章末课后任务表（Task 8/C9）与 exercises/README.md 总览表（Task 9）逐字一致；loader 侧含 `<net/if.h>` 与 `<bpf/bpf.h>`（冒烟测试发现隐式声明告警后修正）；gallery 07 的 `trace_event_raw_sys_enter` 布局已在本机 vmlinux.h 核实。
4. **硬性验收**：编译级验证在 Task 2/3/4/9（单目录，含「bad 也必须编译通过」口径）与 Task 10（全量 11 项）均为显式步骤，退出码 0 为通过标准；正文核对 grep（标题计数/占位句扫描/链接计数/篇幅区间/「预期 verifier 日志」逐对标注）在 Task 5/6/7/8/10 均给出确切命令与预期值；运行级全部「预期输出示例 + 运行级待验证」标注，零「已验证通过」表述。

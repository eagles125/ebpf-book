---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-10-25-chapter04-teaching-design.md
base-ref: c9ae92e75a8ba0d5cfbde846f1f7660598f07d75
---

# 第 4 章完整教学章节（build 阶段）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 4 章《eBPF 程序类型与钩子点深入》从零写为 35–52k 字符的完整教学章节（机制章 + 类型画廊实战化），并落地 `book/examples/chapter-04/` 全部独立示例资产（00-env + 2 个集成锚点「同一事件五副眼镜」「同一报文两种 ctx」+ 类型画廊 8 类 compile-only + scripts 编译级验证）与 2 项课后练习的完整可编译参考实现（含防剧透 README）。

**架构:** `01-multi-observe`（一个对象 5 个程序同挂 openat2 事件：tracepoint/raw_tp/kprobe/fentry/fexit，共享分镜 map）承载「同一事件多副眼镜」教学主轴；`02-xdp-tc`（XDP 与 TC 同挂 lo）承载「同一报文两种 ctx」；`03-type-gallery/` 8 类 compile-only 目录承载目录级覆盖；正文按「完整章节模板」组织为 类型系统→内核注册机制→画廊逐类→实战选型 四段；课后练习（选型决策、类型错位修复）各带完整参考实现；`scripts/verify-build.sh` 做单章编译级汇总验证。

**技术栈:** C（vmlinux.h + clang -target bpf + bpftool gen skeleton + libbpf loader；画廊与练习为 compile-only，无 loader）、bash（环境自检/编译级验证脚本）、Markdown（中文正文）。本章无 Go。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md`「Chapter 4: Program Types and Hook Points」7 条；完成即勾满 Batch 2（第 2–4 章）。
- delta spec 无需变更（无 Spec Patch），不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 不触碰 `book/chapters/01-*.md`、`02-*.md`、`03-*.md`、`09-*.md`、`17-*.md` 及对应 examples 目录。
- 只修改：`book/chapters/04-program-types-and-hook-points.md`（新建）、`book/examples/chapter-04/**`（新建）、`openspec/changes/generate-ebpf-book/tasks.md`（勾选）、`.gitignore`（追加产物条目）。

## 全局约束

以下约束适用于所有任务（复制自设计文档与 `book/STYLE.md`，逐字为准）：

1. **语言**：全书与全部代码注释使用中文（`Language: zh-CN`）；术语遵循 `book/STYLE.md`（map=映射、helper=辅助函数、program type=程序类型、attachment point=挂载点）；术语首次出现用「英文（中文解释）」或「中文（English）」格式。
2. **验证策略（硬性验收）**：编译级真实验证在本机执行——clang 18.1.3、bpftool 7.7.0（libbpf 1.3 系统头）、gcc、BTF（`/sys/kernel/btf/vmlinux` 存在）齐备，本章无 Go、无网络依赖；**无 root，`kernel.unprivileged_bpf_disabled=2`**。凡涉及加载/运行的效果一律写「预期输出示例」并标注「运行级待验证」，不得写成「已验证通过」。
3. **画廊 compile-only 口径（本章硬性验收）**：画廊 8 类与练习 broken/fixed/solution 程序**必须全部 clang 编译通过**；两个锚点为全链（BPF 编译 + skeleton + loader gcc 链接全部通过）。加载级行为（实际挂载、事件触发、ctx 访问拒绝）本机不可实测，预期日志一律标注「预期输出示例 / 运行级待验证」。
4. **篇幅**：章节正文目标 35–52k 字符（`wc -m` 口径）；分节预算按设计 §3.3：目标+导读 2–2.5k、一（类型系统）6–8k、二（内核实现）8–10k、三（画廊走读）12–15k、四（实战含五层）6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后任务与总结/参考 2.5–3.5k。
5. **叙事**：双视角落「一（类型系统）、三（画廊）、四（实战）」三节——各节内用「**教师视角**」「**学生动手**」加粗引导段，检查点用编号列表（2–4 个自测问题 + 答案提示）。本章非渐进阶段章节，「四段式阶段教学节奏」不约束一/二/三节；「四、实战」5.2 内以教师视角引入/核心代码原理讲解/学生动手/检查点四段组织（设计 §3.2）。
6. **代码注释**：所有 `.bpf.c` 与 loader C 源文件中文注释，覆盖五类要点：数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。loader 注释覆盖 attach 选型与失败模式。
7. **内核兼容**：helper/特性保守选择，5.10+ 兼容优先；签名/参数形状以**本机 BTF 解析**为准并注明「随内核版本演进，加载期由 BTF 校验」；netfilter 需 6.6+、LSM 需 CONFIG_BPF_LSM+y 与 `lsm=bpf` 启动参数（本机 =y 实证）——全部保守措辞并注明。本机内核 7.0.0-38 仅作编译口径参考，不作日志事实来源。
8. **安全**：XDP 锚点安全警告统一措辞：「XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。」画廊程序与编译脚本不加载、无此风险，脚本头部警告写明「本脚本只编译不加载」。
9. **构建产物不入库**：`.gitignore` 全局规则已排除 `book/examples/**/vmlinux.h`、`*.skel.h`、`*.o`；新增二进制产物按任务追加条目。
10. **任务对应**：每个任务必须引用 tasks.md「Chapter 4」矩阵的唯一任务文本片段；全部任务完成后由 Task 12 统一勾选 tasks.md 的 7 条。
11. **提交纪律**：每个任务以一次 git 提交结束；提交信息格式 `feat(ch04): ...` / `docs(ch04): ...` / `chore(ch04): ...`；从仓库根 `/work/ai/ebpf-book` 执行命令。
12. **工作区**：开始前确认 `git rev-parse HEAD == c9ae92e1f2e8be36d9b399c1bf28c4c52a6cbbed`（或基于该提交的工作树）。Comet 编排产物（`openspec/changes/generate-ebpf-book/.comet/*`、`.zcodeignore`）的未提交改动**不属于任何 ch04 提交**，每次 `git add` 只加当前任务 Files 列出的路径。

## 现状基线（编写计划时已核实）

| 事实 | 值 |
|---|---|
| HEAD | `c9ae92e1f2e8be36d9b399c1bf28c4c52a6cbbed`（工作区含 .comet 编排产物改动，见全局约束 12） |
| 章节现状 | 第 4 章正文**不存在**（`book/chapters/` 仅 01/02/03/09/17）；`book/examples/chapter-04/` **不存在** |
| 前章预告回收清单 | ①ch02:72「完整谱系（XDP、TC、cgroup、kprobe、fentry/fexit、LSM 等）随内核版本增长，第 4 章逐类展开」→ 一/三节；②ch03:1123「第 4 章逐类展开程序类型与钩子点——随身带上两件行李：2.3 类型表当字典，排错速查表当地图」→ 导读与三节口径；③ch03 06 card「bpf_get_current_pid_tgid 不在 XDP 白名单」→ 三节 helper 面正面演示（tracing 类可用） |
| 本机工具链 | clang 18.1.3、bpftool 7.7.0、gcc、libbpf-dev 1.3.0（`/usr/include/bpf/bpf_helpers.h`、`/usr/include/bpf/bpf_tracing.h` 存在）、内核 7.0.0-38 |
| 权限 | 非 root；`/proc/sys/kernel/unprivileged_bpf_disabled` = 2（非特权不可加载） |
| 内核特性 | `CONFIG_BPF_LSM=y`、`CONFIG_DEBUG_INFO_BTF=y`（/boot/config-7.0.0-38-generic 实证）；BTF 含 `struct bpf_nf_ctx`、`struct open_how`、`enum sk_action`（成员名 `__SK_DROP`/`__SK_PASS`）、`BPF_SOCK_OPS_PASSIVE/ACTIVE_ESTABLISHED_CB` |
| 冒烟测试（编写计划时已完成） | ①15 类 SEC 最小程序全部 `clang -O2 -g -target bpf -D__TARGET_ARCH_x86` 编译通过（tracepoint/raw_tp/kprobe/kretprobe/fentry/fexit/uprobe/xdp/tc/cgroup-skb/socket/sockops/sk_msg/lsm/netfilter），BPF_PROG/BPF_KPROBE/BPF_KRETPROBE 三宏均实测；②01 锚点 5 程序合一个 `.bpf.o` 且 `bpftool gen skeleton` 成功（skel 含 5 个 `bpf_program *` 符号）；③**BPF_PROG/BPF_KPROBE/BPF_KRETPROBE 宏在 `<bpf/bpf_tracing.h>`**（不在 bpf_helpers.h）——tracing 类程序必须包含；④**`TC_ACT_OK`（uapi `pkt_cls.h` #define）与 `NF_ACCEPT`（uapi `netfilter.h` #define）不在 BTF/vmlinux.h**，需源文件头部手工 define（`XDP_PASS` 在 BTF，`enum xdp_action`）；⑤`do_unlinkat` 本机 BTF 缺席（内联优化），锚点目标函数用 **`do_sys_openat2`**（BTF 实证签名 `int do_sys_openat2(int dfd, const char *filename, struct open_how *how)`，5.10+ 稳定）；`vfs_unlink` 第 4 参数本机为 `struct delegated_inode *`（上游 6.x 为 `struct inode **`——签名漂移实证，进教学）；⑥syscall 类 tracepoint 的 ctx 是通用 `struct trace_event_raw_sys_enter`（逐事件结构不在 BTF；事件本身在 tracefs 运行时存在）；**raw_tp 的原始参数形状随事件类不同（syscall 类为 `(regs, id)`，以内核源码 `kernel/trace/trace_syscalls.c` 为准）——raw 镜按 `__u64 *ctx` 处理、只读形状不拆字段**；⑦ch17 已用 `SEC("tc")`；TC 挂载走 `bpf_tc_hook_create`/`bpf_tc_attach`（libbpf 1.3 支持，ch17 04-nat-tc 先例）；⑧netfilter 需内核 6.6+（本机 7.0 满足）；`openat2` 系统调用有 4 个参数（dirfd/pathname/how/usize），tracepoint `args[1]` 即 filename |
| 可复用模式 | 第 2/3 章计划的 C4（全链 Makefile）/C5（README 结构）/C6（验证状态）/C7（标题固定）/C8（交叉链接）——本计划以同字母契约引用并给出本章替换与新增（见下） |

## 共享接口契约（跨任务定义，类型一致性以此为准）

### C1. 01-multi-observe 锚点契约

- 共享分镜 map：`lenses`，`BPF_MAP_TYPE_ARRAY`，`max_entries 5`，key `__u32`（镜编号 0–4），value `struct lens_val { __u64 count; char last_name[64]; __s64 last_ret; }`（count=本镜触发次数；last_name=最近一次文件名；last_ret 仅 fexit 写）。map 选型注释统一：「ARRAY——key 恒定 5 个、无删除，教学聚焦类型差异而非 map 选型」。
- 5 个程序与 SEC（skeleton 符号前缀 `multi_observe_bpf`——bpftool 以对象文件名 `multi_observe.bpf.o` 派生 `<名>_bpf__open/load/destroy`，第 3 章先例 `pkt_parse_bpf` 同型【Task 2 实现期实测教训回写：计划原文误写 `multi_observe`】；`skel->progs.<名>`）：
  | 镜 | key | SEC | 函数签名（逐字） |
  |---|---|---|---|
  | tracepoint | 0 | `SEC("tracepoint/syscalls/sys_enter_openat2")` | `int tp_entry(struct trace_event_raw_sys_enter *ctx)` |
  | raw_tp | 1 | `SEC("raw_tp/sys_enter_openat2")` | `int raw_entry(__u64 *ctx)` |
  | kprobe | 2 | `SEC("kprobe/do_sys_openat2")` | `int BPF_KPROBE(kp_entry, int dfd, const char *filename, struct open_how *how)` |
  | fentry | 3 | `SEC("fentry/do_sys_openat2")` | `int BPF_PROG(fe_entry, int dfd, const char *filename, struct open_how *how)` |
  | fexit | 4 | `SEC("fexit/do_sys_openat2")` | `int BPF_PROG(fx_entry, int dfd, const char *filename, struct open_how *how, int ret)` |
- ctx 处理口径：tp/raw/kp/fe 四镜写 `count` 并用 `bpf_probe_read_user_str` 写 `last_name`（filename 为 `__user` 指针——任何镜都不得直接解引用，第 3 章 card ⑦ 界限回指）；raw 镜**不读 filename**（只 `count`，ctx[1] 为 id 的口径写注释：原始参数 (regs, id)，以内核源码为准）；fexit 镜写 `count` 与 `last_ret`。
- helper 选型注释统一：「bpf_probe_read_user_str——用户指针按 helper 界限读入栈/map，length 截断安全（第 3 章 card ⑦）」。
- loader：`multi_observe.c`，`bpf_program__attach()` 对 5 程序逐一自动挂载（libbpf 按 SEC 解析挂载点）；每秒读 map 打印 5 行分镜（镜名/计数/最近文件名或返回值）；Ctrl-C 退出即 links destroy + skeleton destroy。运行需 root/特权档（运行级待验证）。

### C2. compile-only Makefile 模板（画廊 8 目录与练习目录共用；`<SRCS>` 按目录替换；规则行用制表符缩进）

```make
# 第 4 章「类型画廊」编译脚本（compile-only：本目录程序只编译不加载）
# 安全警告：本脚本只编译不加载（无挂载风险）
BPF_SRCS := <SRCS>
# 目标名替换用 .c=.o：源文件名已是 *.bpf.c，直接得到 *.bpf.o
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
| `03-type-gallery/<8 个目录>/` | 各自唯一源文件（见 C3 表） |
| `exercises/01-type-selection/` | `solution.bpf.c` |
| `exercises/02-type-mismatch/` | `broken.bpf.c fixed.bpf.c` |

### C3. 类型画廊 8 类契约（目录级覆盖；每目录含唯一 `.bpf.c` + Makefile + README 六段）

| 目录 | prog_type | SEC | ctx 类型 | 返回值语义 | 关键教学点（README 与正文 card 共用口径） |
|---|---|---|---|---|---|
| 01-kretprobe | KPROBE | `SEC("kretprobe/do_sys_openat2")` | `BPF_KRETPROBE(krp, int ret)`（宏内部为 pt_regs） | 0 正常 | kretprobe 在目标函数**返回处**触发；返回值经 `BPF_KRETPROBE` 宏读取（架构相关展开，x86 由 PT_REGS_RC——「以所用架构为准」）；`ret>=0` 为 fd、负值为错误码 |
| 02-uprobe | KPROBE（uprobe 变体） | `SEC("uprobe")` | `int up(struct pt_regs *ctx)` | 0 | 用户态探针：SEC 命名规则 `uprobe/<二进制路径>:<偏移>`（loader/bpftool 提供路径与偏移，编译期 SEC 串仅为标记）；用户内存读必须走 probe 系 helper（回指第 3 章 card ⑦） |
| 03-cgroup-skb | CGROUP_SKB | `SEC("cgroup/skb")` | `int cg(struct __sk_buff *skb)` | **1 放行 / 0 丢弃** | cgroup 维度的网络 verdict；`bpf_get_current_cgroup_id()` 演示 cgroup 上下文获取；与 XDP/TC 的选择边界（挂哪、能否看 socket 状态） |
| 04-socket-filter | SOCKET_FILTER | `SEC("socket")` | `int sf(struct __sk_buff *skb)` | **返回值=接受拷贝的字节数**（0=该 socket 不见此包） | 经典 tcpdump/pcap 载体；只读观测（不干预协议栈）；与 cgroup-skb 同 ctx 不同返回语义的对照 |
| 05-sockops | SOCK_OPS | `SEC("sockops")` | `int so(struct bpf_sock_ops *ops)` | 1=已处理 / 0 透传 | TCP 连接生命周期回调（`ops->op` 判 `BPF_SOCK_OPS_PASSIVE/ACTIVE_ESTABLISHED_CB`）；socket 级状态可见（XDP/TC 没有的视角） |
| 06-sk-msg | SK_MSG | `SEC("sk_msg")` | `int sm(struct sk_msg_md *msg)` | `__SK_PASS` / `__SK_DROP` | sockmap 重定向判决；与 sockops 配对构建 sockmap 体系（深入留第 22/23 章交叉引用） |
| 07-lsm | LSM | `SEC("lsm/file_open")` | `int BPF_PROG(lf, struct file *file)` | **0 允许 / 非 0 阻断**（裁决语义以所用内核源码 `include/linux/lsm_hook_defs.h` 为准） | 安全决策点：内核对象（`struct file *`）直接可读；前提 CONFIG_BPF_LSM=y + `lsm=bpf` 启动参数（本机 =y 实证，运行级待验证） |
| 08-netfilter | NETFILTER | `SEC("netfilter")` | `int nf(struct bpf_nf_ctx *ctx)` | `NF_ACCEPT` / `NF_DROP` | 6.6+ 新程序类型；`bpf_nf_ctx{state, in, out}`；完整可运行示例交叉引用 ch09 `09-netfilter-go/` |

- 各目录源文件名：`kretprobe.bpf.c` / `uprobe.bpf.c` / `cgroup_skb.bpf.c` / `socket_filter.bpf.c` / `sockops.bpf.c` / `sk_msg.bpf.c` / `lsm.bpf.c` / `netfilter.bpf.c`。
- 每目录 README.md 固定六段：**任务一句话 / 代码走读 / 挂载方式与前提 / 预期输出示例（运行级待验证）/ 验证状态（按 C6）/ 参考来源**；头部注释必须含「预期加载前提」一句话。

### C4. 全链 Makefile 模板（锚点 01/02 用；沿用第 3 章 01-pkt-parse/Makefile 落地原文，仅替换目标名与依赖）

单对象单 loader（01-multi-observe）：

```make
# 第 4 章示例构建脚本（libbpf-bootstrap 模式，与第 2/3/17 章示例一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: multi_observe

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

multi_observe.bpf.o: multi_observe.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

multi_observe.skel.h: multi_observe.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

multi_observe: multi_observe.c multi_observe.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h multi_observe
```

02-xdp-tc 变体（两个 BPF 对象、两个 skeleton、一个 loader；其余同上）：`all: xdp_tc`；`xdp_count.bpf.o` 与 `tc_count.bpf.o` 各一条 clang 规则；`xdp_count.skel.h`/`tc_count.skel.h` 各一条 skeleton 规则；`xdp_tc: xdp_tc.c xdp_count.skel.h tc_count.skel.h` 一条 gcc 规则；`clean` 加 `xdp_tc`。首行注释同上。

### C5. README 结构（沿用第 2/3 章计划 C5 原文）

- **示例/章级 README 十段结构**：1 标题 + 一句话定位（对应章节小节）　2 演示目标　3 文件说明（表）　4 环境前提　5 编译与运行（命令）　6 预期输出（标注「预期输出示例 / 运行级待验证」）　7 代码讲解要点（中文注释呼应）　8 常见失败点（表：现象/原因与处理）　9 验证状态（按 C6）　10 参考来源。
- **画廊 README 六段结构**（每类，见 C3）。
- **练习 README 防剧透七段结构**：1 `## 任务`（面向学生的任务描述与验收标准）　2 `## 提示`（涉及哪些机制/错误信号，不给答案）　3 `## 环境前提与构建`　4 `## 预期输出示例`（运行级待验证）　5 `## 参考实现思路`（独立成节，防剧透分界线 `---` 在第 4 节之后）　6 `## 验证状态`　7 `## 参考来源`。

### C6. 验证状态约定（沿用第 2/3 章计划 C6 原文）

- **编译验证**：写明「已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过」。
- **待运行验证**：写明「编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例」。
- **本章补充**：全链锚点（01/02）的验证状态为「编译级（含 gcc 链接）已通过；加载级/运行级待特权环境验证」；画廊与练习程序为「编译级已通过；加载级/运行级待特权环境验证」。

### C7. 章节正文标题（逐字固定，所有正文任务不得改动一二级标题）

```text
# 第 4 章 eBPF 程序类型与钩子点深入
## 学习目标
## 本章导读
## 一、程序类型系统：类型决定什么
## 二、类型与钩子的内核实现分析
## 三、类型画廊：十四类逐类走读
## 四、实战：为任务选对类型并挂上钩子
## 可视化：类型谱系与挂载路径
## 常见问题与排错
## 课后扩展任务
## 总结
## 本章参考来源
```

### C8. 交叉链接惯例（沿用第 2/3 章计划 C8 惯例）

相对路径链接 + 仓库根路径文本：章首「本章独立示例已全部落地到 [`book/examples/chapter-04/`](../examples/chapter-04/)」与目录总览；每个正文代码块前后标注对应文件路径（如 `book/examples/chapter-04/01-multi-observe/multi_observe.bpf.c`）；「一/二/三/四」各节末尾「**独立示例**：`../examples/chapter-04/<目录>/`」链接（二节末为附录 B/C 链接 `../appendix/b-tool-command-index.md`、`../appendix/c-distro-btf-build-guide.md`）；三节各 card 链接画廊目录与锚点目录；课后任务节链接 exercises/ 两目录。

### C9. 章末课后任务表（Task 9 与 Task 10 的 exercises/README.md 总览表逐字一致）

```markdown
| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 选型决策 | 三个场景（容器内文件打开审计、TCP 连接建立观测、内核函数入口参数抓取）各选程序类型与挂载点并给出选型论证；参考实现按正确选型合并为单个可编译对象 | `book/examples/chapter-04/exercises/01-type-selection/` |
| 2 | 类型错位修复 | 一段报文解析逻辑被放进 kprobe 程序且 ctx 声明为 `xdp_md`——编译通过但加载预期被拒；诊断并把类型与 SEC 归位 | `book/examples/chapter-04/exercises/02-type-mismatch/` |
```

---

### Task 1: 章级 README、00-env 环境自检与构建基座

- [x] Task 1: 章级 README、00-env 环境自检与构建基座

**tasks.md 条目**：「Create standalone Chapter 4 examples under `book/examples/chapter-04/`.」（环境与总览部分）。

**Files:**
- Create: `book/examples/chapter-04/README.md`
- Create: `book/examples/chapter-04/00-env/check-env.sh`

- [x] **Step 1: 创建 `book/examples/chapter-04/README.md`**——按 C5 十段结构（章级版）。关键口径：示例总览表（2 锚点全链 + 画廊 8 目录 compile-only + 2 练习）；「同一事件多副眼镜」导读；构建模式（锚点全链 make / 画廊逐目录 make / `scripts/verify-build.sh` 汇总）；验证状态约定区（含 C6 本章补充条与「画廊 compile-only 口径」，占位句「全量编译结果由 `scripts/verify-build.sh` 生成（摘要见 Task 11 回填）」）；安全警告统一措辞（全局约束 8）。
- [x] **Step 2: 创建 `00-env/check-env.sh`**——以 `book/examples/chapter-03/00-env/check-env.sh` 为基底（读该文件后改写）：章号注释改第 4 章；追加两项 ch04 特有检查：tracefs 在场性（`[ -d /sys/kernel/tracing/events ]`→OK；`[ -r /sys/kernel/tracing/events/syscalls ]`→OK，不可读→WARN「tracefs 需 root 读取，加载级验证时使用」）；`CONFIG_BPF_LSM`（`/boot/config-$(uname -r)` 或 `/proc/config.gz` grep `CONFIG_BPF_LSM=y`→OK「LSM 类型运行前提在内核侧满足（还需 lsm=bpf 启动参数与 root）」，缺失→WARN「LSM 画廊条目仅编译级可玩」）；`grep -qw bpf /proc/cmdline`→OK「已启用 lsm=bpf」否则 INFO 级提示（不置 fail）。
- [x] **Step 3: 本机验证（无特权可执行，硬性）**

Run: `bash book/examples/chapter-04/00-env/check-env.sh`
Expected: 退出码 0；BTF/工具链各行 OK；权限分级输出 WARN 档（非 root）。
- [x] **Step 4: `.gitignore` 检查**——确认全局规则已覆盖 `book/examples/**/vmlinux.h`、`*.skel.h`、`*.o`；无需追加则不动（如锚点可执行物需追加 `book/examples/chapter-04/**/multi_observe`、`xdp_tc` 条目则追加）。
- [x] **Step 5: 提交**

```bash
git add book/examples/chapter-04/ .gitignore
git commit -m "feat(ch04): 章级 README、00-env 环境自检与构建基座"
```

---

### Task 2: 01-multi-observe 集成锚点（同一事件五副眼镜）

- [x] Task 2: 01-multi-observe 集成锚点（同一事件五副眼镜）

**tasks.md 条目**：「Include at least two integrated hook examples from tracing, networking, or security categories.」（追踪类）＋「Explain `bpf_prog_type` versus `bpf_attach_type`, context structure differences, return values, and helper availability.」（代码实物部分）。

**Files:**
- Create: `book/examples/chapter-04/01-multi-observe/multi_observe.bpf.c`
- Create: `book/examples/chapter-04/01-multi-observe/multi_observe.c`
- Create: `book/examples/chapter-04/01-multi-observe/Makefile`
- Create: `book/examples/chapter-04/01-multi-observe/README.md`

- [x] **Step 1: 创建 `multi_observe.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 4 章锚点①：同一事件五副眼镜——openat2 经 5 类追踪程序观察。
 * 事件统一为 do_sys_openat2(dfd, filename, how)：openat/openat2 系统调用的公共实现。
 * 签名在本机 BTF 中实证（bpftool btf dump）；上游 5.10+ 同签名——fentry/fexit 的参数
 * 类型在加载期由 BTF 校验，签名随内核版本演进（正文 2.4）。
 * 失败模式：签名与目标内核不一致时加载被拒（预期日志见 README 排错表）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>   /* BPF_PROG/BPF_KPROBE 宏在此（冒烟结论③） */

/*
 * 共享分镜 map：ARRAY 5 槽，key=镜编号（0..4，见下）。
 * map 选型：ARRAY——key 恒定 5 个、无删除、O(1) 定位，教学聚焦类型差异而非 map 选型。
 */
struct lens_val {
    __u64 count;         /* 本镜触发次数 */
    char last_name[64];  /* 最近一次打开的文件名（bpf_probe_read_user_str 截断安全） */
    __s64 last_ret;      /* 仅 fexit 镜写：openat2 返回值（fd 或负错误码） */
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 5);
    __type(key, __u32);
    __type(value, struct lens_val);
} lenses SEC(".maps");

/*
 * helper 选型：bpf_probe_read_user_str——filename 是 __user 指针，任何「眼镜」下都
 * 不得直接解引用（第 3 章画廊 card ⑦ 的界限）；helper 读入 map 值，长度截断安全。
 */

/* 镜 0：tracepoint——带类型的事件 ctx（syscall 类复用通用布局，args[1]=filename） */
SEC("tracepoint/syscalls/sys_enter_openat2")
int tp_entry(struct trace_event_raw_sys_enter *ctx)
{
    const char *filename = (const char *)ctx->args[1];
    __u32 key = 0;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)                       /* lookup 失败分支必检（第 3 章 card ①） */
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/*
 * 镜 1：raw_tp——原始参数无类型：ctx 是 u64 数组，形状随事件类不同
 * （syscall 类为 (regs, id)，以内核源码 kernel/trace/trace_syscalls.c 为准）。
 * 本镜只读形状不拆字段：count 计数，ctx[1] 原样存进 last_ret 供对照。
 */
SEC("raw_tp/sys_enter_openat2")
int raw_entry(__u64 *ctx)
{
    __u32 key = 1;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    v->last_ret = (__s64)ctx[1];
    return 0;
}

/* 镜 2：kprobe——ctx 是 pt_regs；BPF_KPROBE 宏按 C 原型从寄存器解包参数（x86 展开，
 * 「以所用架构为准」）。第 1、2 参数即 dfd/filename。 */
SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(kp_entry, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 2;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/* 镜 3：fentry——BTF 直接类型化参数：无 pt_regs 中转，原型即函数原型（本机 BTF 实证）。
 * filename 仍是 __user 指针：类型精确不豁免 probe 界限（正文 3.4）。 */
SEC("fentry/do_sys_openat2")
int BPF_PROG(fe_entry, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 3;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/* 镜 4：fexit——在 fentry 基础上多一个返回值参数：openat2 成功返回 fd、失败返回负错误码。
 * 五镜中只有 fexit 能直接看到「这次调用办成了没有」。 */
SEC("fexit/do_sys_openat2")
int BPF_PROG(fx_entry, int dfd, const char *filename, struct open_how *how, int ret)
{
    __u32 key = 4;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    v->last_ret = ret;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 2: 创建 `multi_observe.c`**（逐字如下；loader 中文注释覆盖 attach 选型与失败模式）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点①用户态 loader：同一事件五副眼镜的分镜读数器。
 * 挂载方式：bpf_program__attach() 逐程序自动挂载——libbpf 按 SEC 前缀解析挂载点
 * （tracepoint→tracefs 事件、kprobe→kprobe 接口、fentry/fexit→BTF 驱动的
 * tracing link、raw_tp→原始 tracepoint），比逐类型手写 attach 更能体现
 * 「类型决定挂载方式」。
 * 失败模式：无特权时 open/load 报 EPERM、fentry attach 报ENOENT/EINVAL
 * （目标函数签名不匹配），README 排错表逐一对应。 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "multi_observe.skel.h"

static volatile sig_atomic_t exiting = 0;
static void on_sigint(int sig) { exiting = 1; }

/* libbpf 1.x：错误走标准 errno；日志回调把 WARN/INFO 透传到 stderr（DEBUG 级滤除），
 * 便于教学观察挂载过程。libbpf_set_print 只接收回调函数指针，不能直接传级别常量
 * （Task 2 实现期实测教训：传 LIBBPF_INFO 枚举是类型错误，运行必崩）。 */
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level > LIBBPF_INFO)
        return 0;
    return vfprintf(stderr, format, args);
}

/* 与 multi_observe.bpf.c 的 struct lens_val 保持一致（skeleton 不导出 map 值类型） */
struct lens_val {
    __u64 count;
    char last_name[64];
    __s64 last_ret;
};

static const char *LENS_NAMES[5] = {
    "tracepoint", "raw_tp", "kprobe", "fentry", "fexit",
};

int main(void)
{
    struct multi_observe_bpf *skel;
    struct bpf_link *links[5] = {0};
    struct bpf_program *progs[5];
    int err = 0;

    signal(SIGINT, on_sigint);
    /* libbpf 1.x：错误走标准 errno，日志级别给到 INFO 便于教学观察挂载过程 */
    libbpf_set_print(libbpf_print_fn);

    skel = multi_observe_bpf__open();
    if (!skel) {
        fprintf(stderr, "skeleton open 失败：见上方 libbpf 日志\n");
        return 1;
    }
    /* 加载：这一步内部依次过 ELF 解析、重定位、verifier——第 3 章的全部内容
     * 都发生在 multi_observe_bpf__load() 内部 */
    err = multi_observe_bpf__load(skel);
    if (err) {
        fprintf(stderr, "load 失败（无特权环境的预期失败：%s）\n", strerror(-err));
        multi_observe_bpf__destroy(skel);
        return 1;
    }

    progs[0] = skel->progs.tp_entry;
    progs[1] = skel->progs.raw_entry;
    progs[2] = skel->progs.kp_entry;
    progs[3] = skel->progs.fe_entry;
    progs[4] = skel->progs.fx_entry;
    for (int i = 0; i < 5; i++) {
        links[i] = bpf_program__attach(progs[i]);
        if (!links[i]) {
            fprintf(stderr, "attach %s 失败（需 root/特权档；fentry 还需签名匹配）\n",
                    LENS_NAMES[i]);
            err = 1;
            goto out;
        }
    }
    printf("五副眼镜已挂上 openat2，随便跑几条命令（如 ls、cat）观察计数……\n");

    while (!exiting) {
        sleep(1);
        for (__u32 key = 0; key < 5; key++) {
            struct lens_val v = {};
            if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.lenses),
                                    &key, &v))
                continue;
            printf("  [%-10s] count=%-6llu last_ret=%-4lld name=%s\n",
                   LENS_NAMES[key], (unsigned long long)v.count,
                   (long long)v.last_ret,
                   v.last_name[0] ? v.last_name : "(本镜不读文件名)");
        }
        printf("----\n");
    }

out:
    for (int i = 0; i < 5; i++)
        bpf_link__destroy(links[i]);   /* link 销毁即卸载（第 2 章生命周期回指） */
    multi_observe_bpf__destroy(skel);
    return err;
}
```

- [x] **Step 3: 创建 `Makefile`**——C4 单对象模板逐字（仅 `<SRCS>` 无替换、目标 `multi_observe`）。
- [x] **Step 4: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-04/01-multi-observe`
Expected: 退出码 0（`multi_observe.bpf.o` → `multi_observe.skel.h` → 可执行 `multi_observe`）。失败即修代码重跑，直至通过。
- [x] **Step 5: 创建 `README.md`**——按 C5 十段结构。关键口径：五镜对照表（镜/SEC/ctx 形状/能拿到什么）；预期输出示例（5 行分镜 + 计数增长，标注「预期输出示例 / 运行级待验证」）；无特权预期失败表（open/load EPERM、attach 失败形态）；验证状态按 C6 本章补充条。
- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-04/01-multi-observe/ .gitignore
git commit -m "feat(ch04): 01-multi-observe 同一事件五副眼镜（追踪类集成锚点）"
```

---

### Task 3: 02-xdp-tc 集成锚点（同一报文两种 ctx）

- [x] Task 3: 02-xdp-tc 集成锚点（同一报文两种 ctx）

**tasks.md 条目**：「Include at least two integrated hook examples from tracing, networking, or security categories.」（网络类）。

**Files:**
- Create: `book/examples/chapter-04/02-xdp-tc/xdp_count.bpf.c`
- Create: `book/examples/chapter-04/02-xdp-tc/tc_count.bpf.c`
- Create: `book/examples/chapter-04/02-xdp-tc/xdp_tc.c`
- Create: `book/examples/chapter-04/02-xdp-tc/Makefile`
- Create: `book/examples/chapter-04/02-xdp-tc/README.md`

- [x] **Step 1: 创建 `xdp_count.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②（XDP 侧）：同一报文在 XDP 挂载点的视角。
 * ctx 是 struct xdp_md：只有 data/data_end/data_meta 与少量元数据——
 * 报文解析全部手工：data/data_end 边界检查是 verifier 的硬要求
 * （第 3 章画廊 card ② 的正面用法）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* map 选型：ARRAY——单 key 全局计数，教学聚焦 ctx 差异而非 map 选型 */
struct pkt_stats {
    __u64 packets;   /* 经过的报文总数 */
    __u64 udp;       /* 其中 UDP 报文数 */
};
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct pkt_stats);
} xdp_stats SEC(".maps");

SEC("xdp")
int xdp_entry(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u32 key = 0;
    struct pkt_stats *v = bpf_map_lookup_elem(&xdp_stats, &key);
    if (!v)
        return XDP_PASS;

    __sync_fetch_and_add(&v->packets, 1);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)      /* 以太网头边界检查 */
        return XDP_PASS;
    if (eth->h_proto != bpf_htons(0x0800)) /* ETH_P_IP 不在 BTF，直接用数值 */
        return XDP_PASS;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)       /* IPv4 头边界检查 */
        return XDP_PASS;
    if (ip->protocol == 17)                /* IPPROTO_UDP */
        __sync_fetch_and_add(&v->udp, 1);

    return XDP_PASS;                       /* 只观测不干预 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 2: 创建 `tc_count.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②（TC 侧）：同一报文在 TC 挂载点的视角。
 * ctx 是 struct __sk_buff：除了 data/data_end（与 xdp_md 形状相似！），还有
 * mark/protocol/len 等协议栈加工后的字段——「同一个包，两副眼镜」的另一半。
 * 冒烟结论④：TC_ACT_OK 是 uapi #define 不在 BTF，需手工定义。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define TC_ACT_OK 0    /* uapi pkt_cls.h 的 #define，不在 BTF/vmlinux.h（冒烟结论④） */

struct pkt_stats {
    __u64 packets;
    __u64 udp;
};
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct pkt_stats);
} tc_stats SEC(".maps");

SEC("tc")
int tc_entry(struct __sk_buff *skb)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 key = 0;
    struct pkt_stats *v = bpf_map_lookup_elem(&tc_stats, &key);
    if (!v)
        return TC_ACT_OK;

    __sync_fetch_and_add(&v->packets, 1);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;
    if (eth->h_proto != bpf_htons(0x0800))
        return TC_ACT_OK;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;
    if (ip->protocol == 17)
        __sync_fetch_and_add(&v->udp, 1);

    return TC_ACT_OK;    /* 放行：TC 的 verdict 在协议栈入口，语义与 XDP_PASS 对照 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 3: 创建 `xdp_tc.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②用户态 loader：XDP 与 TC 同挂 lo，对照读数。
 * attach 选型：XDP 走 bpf_program__attach_xdp（返回 link）；TC 没有 link 对应物——
 * 走 netlink 的 qdisc/filter 模型（bpf_tc_hook_create 建 clsact、bpf_tc_attach 绑
 * ingress，沿第 17 章 04-nat-tc 先例）。flags=0 挂 lo 时内核一般自动回退 generic
 * （SKB）模式——具体行为随内核版本，以实测为准（运行级待验证）。
 * 失败模式：无特权 load 即 EPERM；clsact 已存在时 hook_create 返回 -EEXIST 视为成功。 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <net/if.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "xdp_count.skel.h"
#include "tc_count.skel.h"

static volatile sig_atomic_t exiting = 0;
static void on_sigint(int sig) { exiting = 1; }

/* libbpf 1.x：日志回调把 WARN/INFO 透传到 stderr（DEBUG 级滤除）——
 * libbpf_set_print 只接收回调函数指针，不能直接传级别常量（同 Task 2 教训）。 */
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level > LIBBPF_INFO)
        return 0;
    return vfprintf(stderr, format, args);
}

struct pkt_stats { __u64 packets; __u64 udp; };

static void print_stats(int map_fd, const char *tag)
{
    __u32 key = 0;
    struct pkt_stats v = {};
    if (bpf_map_lookup_elem(map_fd, &key, &v) == 0)
        printf("  [%s] packets=%-8llu udp=%-8llu\n", tag,
               (unsigned long long)v.packets, (unsigned long long)v.udp);
}

int main(void)
{
    struct xdp_count_bpf *xdp = NULL;
    struct tc_count_bpf *tc = NULL;
    struct bpf_link *xdp_link = NULL;
    unsigned int ifindex;
    int err = 1;

    signal(SIGINT, on_sigint);
    libbpf_set_print(libbpf_print_fn);

    ifindex = if_nametoindex("lo");   /* <net/if.h>；隐式声明告警的教训见第 3 章 */
    if (!ifindex) {
        fprintf(stderr, "lo 接口不可见\n");
        return 1;
    }

    xdp = xdp_count_bpf__open();
    tc = tc_count_bpf__open();
    if (!xdp || !tc) {
        fprintf(stderr, "skeleton open 失败\n");
        goto out;
    }
    if (xdp_count_bpf__load(xdp) || tc_count_bpf__load(tc)) {
        fprintf(stderr, "load 失败（无特权环境的预期失败）\n");
        goto out;
    }

    /* XDP 挂载：驱动层 hook，返回 bpf_link */
    xdp_link = bpf_program__attach_xdp(xdp->progs.xdp_entry, (int)ifindex);
    if (!xdp_link) {
        fprintf(stderr, "XDP attach 失败（需 root）\n");
        goto out;
    }

    /* TC 挂载：先建 clsact qdisc（已存在返回 -EEXIST，视为成功），再绑 ingress */
    {
        LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = ifindex,
                    .attach_point = BPF_TC_INGRESS);
        LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 1,
                    .prog_fd = bpf_program__fd(tc->progs.tc_entry));
        if (bpf_tc_hook_create(&hook) != 0 && errno != EEXIST) {
            fprintf(stderr, "clsact 创建失败（需 root）\n");
            goto out;
        }
        hook.attach_point = BPF_TC_INGRESS;
        if (bpf_tc_attach(&hook, &opts)) {
            fprintf(stderr, "TC attach 失败（需 root）\n");
            goto out;
        }
    }

    printf("XDP 与 TC 已同挂 lo，制造流量（如 ping 127.0.0.1、curl 本机服务）观察对照……\n");
    while (!exiting) {
        sleep(1);
        print_stats(bpf_map__fd(xdp->maps.xdp_stats), "xdp      ");
        print_stats(bpf_map__fd(tc->maps.tc_stats), "tc       ");
        printf("----\n");
    }
    err = 0;

out:
    {
        LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = ifindex,
                    .attach_point = BPF_TC_INGRESS);
        LIBBPF_OPTS(bpf_tc_opts, detach_opts, .handle = 1, .priority = 1);
        /* best-effort detach filter：须传填充 opts（NULL 会被 libbpf 拒绝，-EINVAL 静默 no-op），
         * handle/priority 与 attach 时一致（沿 ch17 04-nat-tc 先例）；filter 不在时 -ENOENT 属正常，
         * 返回值忽略；qdisc 留置无害 */
        bpf_tc_detach(&hook, &detach_opts);
    }
    bpf_link__destroy(xdp_link);
    xdp_count_bpf__destroy(xdp);
    tc_count_bpf__destroy(tc);
    return err;
}
```

- [x] **Step 4: 创建 `Makefile`**——C4 双对象变体逐字（目标 `xdp_tc`；`xdp_count.bpf.o`/`tc_count.bpf.o` 两条 clang 规则；两个 skeleton 规则；`xdp_tc: xdp_tc.c xdp_count.skel.h tc_count.skel.h` 一条 gcc 规则；clean 含 `xdp_tc`）。
- [x] **Step 5: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-04/02-xdp-tc`
Expected: 退出码 0（两个 `.bpf.o`、两个 skel、可执行 `xdp_tc`）。失败即修代码重跑，直至通过。
- [x] **Step 6: 创建 `README.md`**——按 C5 十段结构。关键口径：两种 ctx 对照表（xdp_md vs __sk_buff 字段/语义/verdict）；挂载差异（attach_xdp 返回 link vs bpf_tc_attach netlink 模型）；预期输出示例（两行对照计数，标注「预期输出示例 / 运行级待验证」）；安全警告统一措辞；验证状态按 C6。
- [x] **Step 7: 提交**

```bash
git add book/examples/chapter-04/02-xdp-tc/ .gitignore
git commit -m "feat(ch04): 02-xdp-tc 同一报文两种 ctx（网络类集成锚点）"
```

---

### Task 4: 类型画廊前四类（kretprobe/uprobe/cgroup-skb/socket-filter）与总览

**tasks.md 条目**：「Write full chapter prose covering kprobe, kretprobe, tracepoint, raw tracepoint, fentry/fexit, uprobe, XDP, TC, cgroup skb, socket filter, sockops, sk_msg, LSM, and Netfilter where relevant.」（示例资产部分·前半）＋「Create standalone Chapter 4 examples under `book/examples/chapter-04/`.」。

**Files:**
- Create: `book/examples/chapter-04/03-type-gallery/README.md`
- Create: `book/examples/chapter-04/03-type-gallery/01-kretprobe/{kretprobe.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/02-uprobe/{uprobe.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/03-cgroup-skb/{cgroup_skb.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/04-socket-filter/{socket_filter.bpf.c,Makefile,README.md}`

- [ ] **Step 1: 创建 `01-kretprobe/kretprobe.bpf.c`**（逐字如下；骨架按 C2 公共块）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊①：kretprobe——在目标函数返回处触发（预期加载前提：root/特权档）。
 * 与 kprobe 的分工：kprobe 看入口参数，kretprobe 看「办成了没有」。
 * 返回值读取：BPF_KRETPROBE 宏（bpf_tracing.h）按架构展开取返回值寄存器
 * （x86 由 PT_REGS_RC——以所用架构为准）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* map 选型：ARRAY——两个 key（成功/失败）固定计数 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=成功(fd) 1=失败(负错误码) */
    __type(value, __u64);
} openat2_verdict SEC(".maps");

SEC("kretprobe/do_sys_openat2")
int BPF_KRETPROBE(krp, int ret)
{
    __u32 key = ret >= 0 ? 0 : 1;
    __u64 *v = bpf_map_lookup_elem(&openat2_verdict, &key);
    if (!v)                  /* lookup 失败分支必检（第 3 章 card ①） */
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 2: 创建 `02-uprobe/uprobe.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊②：uprobe——用户态函数探针（预期加载前提：root + 指定二进制路径与偏移）。
 * SEC 命名规则：uprobe/<二进制路径>:<偏移>——路径与偏移由 loader/bpftool 提供，
 * 编译期 SEC 串只是标记（libbpf 解析后转成 KPROBE 类型的 uprobe 变体）。
 * 失败模式：路径/偏移错→attach 失败；用户内存读必须走 probe 系 helper
 * （第 3 章画廊 card ⑦ 界限回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} uprobe_hits SEC(".maps");

SEC("uprobe")
int up(struct pt_regs *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&uprobe_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 3: 创建 `03-cgroup-skb/cgroup_skb.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊③：cgroup/skb——cgroup 维度的网络 verdict
 * （预期加载前提：root + 挂到某个 cgroup；veth/netns 拓扑见第 17 章）。
 * 返回值语义：1 放行 / 0 丢弃——与 TC 的 TC_ACT_*、XDP 的 XDP_* 都不同，
 * 这是「类型决定返回值语义」的活例子。
 * map 选型：PER-CPU ARRAY——verdict 计数是高频写，per-CPU 免锁聚合
 * （第 2 章映射全景回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=放行 1=丢弃 */
    __type(value, __u64);
} cg_verdict SEC(".maps");

SEC("cgroup/skb")
int cg(struct __sk_buff *skb)
{
    __u32 key = 0;
    /* helper 选型：bpf_get_current_cgroup_id——拿到触发本程序执行的 cgroup，
     * 是 cgroup 族类型独有的上下文维度（对照：XDP/TC 没有 cgroup 语义） */
    __u64 cg_id = bpf_get_current_cgroup_id();
    (void)cg_id;             /* 教学示例：演示获取，不做过滤决策 */
    __u64 *v = bpf_map_lookup_elem(&cg_verdict, &key);
    if (!v)
        return 1;            /* map 异常时放行，避免误杀流量 */
    __sync_fetch_and_add(v, 1);
    return 1;                /* 1=放行 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 4: 创建 `04-socket-filter/socket_filter.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊④：socket filter——最古老的 BPF 用途（tcpdump 的载体）
 * （预期加载前提：root + SO_ATTACH_BPF 到某个 socket）。
 * 返回值语义：返回值 = 接受拷贝给该 socket 的字节数（0 = 这个包对该 socket 不可见）。
 * 只读观测：不干预协议栈，报文照常投递——与 cgroup/skb 的「丢弃」语义本质不同。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sf_seen SEC(".maps");

SEC("socket")
int sf(struct __sk_buff *skb)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&sf_seen, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;    /* 教学示例：恒 0=不投递给观测 socket，包本身不受影响 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 5: 为四目录各创建 `Makefile`**（C2 模板，`<SRCS>` 按 C2 替换表）。
- [ ] **Step 6: 创建 `03-type-gallery/README.md`**——画廊总览：8 类总览表（C3 表的目录/prog_type/SEC/ctx/返回值语义五列逐字一致，关键教学点列展开为一段）；阅读顺序建议（与正文三节 3.8–3.14 对应）；「compile-only 口径」与安全警告；验证状态汇总表（按 C6）。
- [ ] **Step 7: 为四目录各创建 `README.md`**——按 C5 画廊六段结构（C3 表「关键教学点」展开为代码走读段；挂载方式与前提段含预期加载前提；预期输出示例标注「运行级待验证」）。
- [ ] **Step 8: 编译验证（本机真实执行，硬性）**

Run: `for d in 01-kretprobe 02-uprobe 03-cgroup-skb 04-socket-filter; do make -C book/examples/chapter-04/03-type-gallery/$d || exit 1; done`
Expected: 四目录退出码均 0。失败即修代码重跑。
- [ ] **Step 9: 提交**

```bash
git add book/examples/chapter-04/03-type-gallery/
git commit -m "feat(ch04): 类型画廊前四类（kretprobe/uprobe/cgroup-skb/socket-filter）与总览"
```

---

### Task 5: 类型画廊后四类（sockops/sk-msg/lsm/netfilter）

**tasks.md 条目**：同 Task 4（示例资产部分·后半）。

**Files:**
- Create: `book/examples/chapter-04/03-type-gallery/05-sockops/{sockops.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/06-sk-msg/{sk_msg.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/07-lsm/{lsm.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/03-type-gallery/08-netfilter/{netfilter.bpf.c,Makefile,README.md}`

- [ ] **Step 1: 创建 `05-sockops/sockops.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑤：sockops——TCP 连接生命周期回调
 * （预期加载前提：root + cgroup 挂载；事件由内核 TCP 状态机驱动）。
 * ctx 是 struct bpf_sock_ops：含本地/对端地址端口、TCP 状态与 snd_cwnd 等。
 * 返回值语义：1=本 callback 已处理该事件，0=交给默认路径。
 * 视角价值：socket 级状态可见——这是 XDP/TC（报文级）没有的维度。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* map 选型：ARRAY——两类连接建立事件固定计数 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=被动建立 1=主动建立 */
    __type(value, __u64);
} tcp_estab SEC(".maps");

SEC("sockops")
int so(struct bpf_sock_ops *ops)
{
    __u32 key;
    /* ops->op 是事件码：BPF_SOCK_OPS_* 枚举在 BTF（冒烟结论「常量在场」）
     * 失败模式：未知 op 一律放行——sockops 回调种类多，教学只关心两种建立 */
    switch (ops->op) {
    case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:   /* 服务端：三次握手完成 */
        key = 0;
        break;
    case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:    /* 客户端：connect 完成 */
        key = 1;
        break;
    default:
        return 0;
    }
    __u64 *v = bpf_map_lookup_elem(&tcp_estab, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 1;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 2: 创建 `06-sk-msg/sk_msg.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑥：sk_msg——sockmap 上的报文重定向判决
 * （预期加载前提：root + sockmap 与 sockops 配对搭建，深入见第 22/23 章交叉引用）。
 * ctx 是 struct sk_msg_md：数据所在内存区间与两端 socket 元数据。
 * 返回值语义：__SK_PASS / __SK_DROP（uapi enum sk_action，成员名带下划线前缀，
 * 在 BTF——冒烟结论「常量在场」）。
 * 失败模式：sockmap 未挂对应 sockops 前该程序不会被触发。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sm_verdict SEC(".maps");

SEC("sk_msg")
int sm(struct sk_msg_md *msg)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&sm_verdict, &key);
    if (!v)
        return __SK_DROP;    /* map 异常时丢弃，教学示例从简 */
    __sync_fetch_and_add(v, 1);
    return __SK_PASS;        /* 教学示例：一律放行（真实重定向走 bpf_msg_redirect_hash） */
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 3: 创建 `07-lsm/lsm.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑦：LSM——把 BPF 程序变成一条安全策略
 * （预期加载前提：root + CONFIG_BPF_LSM=y + 内核启动参数 lsm=bpf；
 *   本机 CONFIG_BPF_LSM=y 实证，运行级待验证）。
 * ctx 是 hook 原型的参数直列：file_open hook 即 BPF_PROG(lf, struct file *file)——
 * 内核对象（struct file）直接可读，这是安全类类型与追踪/网络类的最大差别。
 * 返回值语义：0 允许；非 0 阻断该操作（各 hook 裁决语义以所用内核源码
 * include/linux/lsm_hook_defs.h 为准）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lsm_opens SEC(".maps");

SEC("lsm/file_open")
int BPF_PROG(lf, struct file *file)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&lsm_opens, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;    /* 0=允许打开；教学示例只审计不拦截 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 4: 创建 `08-netfilter/netfilter.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑧：netfilter——6.6+ 的新程序类型（本机 7.0 满足；预期加载前提：root）。
 * ctx 是 struct bpf_nf_ctx：{ state, in, out }——netfilter 钩子点的报文与状态。
 * 返回值语义：NF_ACCEPT/NF_DROP（uapi netfilter.h #define，不在 BTF——冒烟结论④）。
 * 完整可运行示例（Go 控制面 + 规则表）见第 9 章 09-netfilter-go/。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define NF_ACCEPT 1    /* uapi netfilter.h 的 #define，不在 BTF/vmlinux.h（冒烟结论④） */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} nf_seen SEC(".maps");

SEC("netfilter")
int nf(struct bpf_nf_ctx *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&nf_seen, &key);
    if (!v)
        return NF_ACCEPT;
    __sync_fetch_and_add(v, 1);
    return NF_ACCEPT;    /* 教学示例：只计数不拦截 */
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 5: 为四目录各创建 `Makefile`**（C2 模板）与 `README.md`（C5 画廊六段，同 Task 4 Step 7 口径）。
- [ ] **Step 6: 编译验证（本机真实执行，硬性）**

Run: `for d in 05-sockops 06-sk-msg 07-lsm 08-netfilter; do make -C book/examples/chapter-04/03-type-gallery/$d || exit 1; done`
Expected: 四目录退出码均 0。失败即修代码重跑。
- [ ] **Step 7: 提交**

```bash
git add book/examples/chapter-04/03-type-gallery/
git commit -m "feat(ch04): 类型画廊后四类（sockops/sk-msg/lsm/netfilter）"
```

---

### Task 6: 正文「学习目标/本章导读/一、程序类型系统/二、内核实现分析」

**tasks.md 条目**：「Explain `bpf_prog_type` versus `bpf_attach_type`, context structure differences, return values, and helper availability.」＋「Write full chapter prose covering …」（机制部分）。

**Files:**
- Create: `book/chapters/04-program-types-and-hook-points.md`

- [ ] **Step 1: 写「# 第 4 章」题头、「学习目标」「本章导读」（约 2–2.5k）**——导读回收 ch02:72 与 ch03:1123 两处预告（逐条兑现清单同 ch03 导读模式）；本章问题：「同一个 BPF 程序为什么不能到处挂」；学习路线图：先类型系统、再内核注册机制、再画廊逐类、最后实战选型。
- [ ] **Step 2: 写「一、程序类型系统：类型决定什么」（6–8k）**——小节锚点 `### 1.1`–`### 1.5`：1.1 两层模型（`bpf_prog_type` 与 `bpf_attach_type` 为什么要分两层：一个类型多个挂载语义——kprobe/kretprobe/uprobe 同类型不同 attach；XDP 的 XDP-generic/native/offload 三种挂载模式一句话）；1.2 类型三元组（ctx 布局/helper 可用面/返回值语义——各配最小例，helper 面演示用 ch03 card ⑥ 反例的正面版：`bpf_get_current_pid_tgid` 在 tracing 类可用）；1.3 十四类总对比表（列：程序类型/触发源/ctx 类型/返回值语义/稳定边界/典型用途——C3 表 + 锚点 5 类 + perf event 一行带过注明第 9 章主场）；1.4 选型决策树（文本图：先分观测/网络/安全三类→再按「要什么数据」分叉→落到类型+attach；教师视角：为什么选型错误的程序多数**编译通过**——类型检查在加载期，回指第 3 章）；1.5 检查点（3 问+提示）。
- [ ] **Step 3: 写「二、类型与钩子的内核实现分析」（8–10k）**——小节锚点 `### 2.1`–`### 2.6`：2.1 注册表（`include/uapi/linux/bpf.h` 的 `bpf_prog_type` 枚举与 `kernel/bpf/bpf_types.c` 的 `BPF_PROG_TYPE` 宏表→编译期生成 `bpf_types[]`，每类型一组 `bpf_verifier_ops`[ch03 2.6 回指]）；2.2 ctx 的两层皮（UAPI ctx 结构→`is_valid_access` 白名单→`convert_ctx_accesses` 把字段访问重写为内核内部偏移——「ctx 字段为什么按 UAPI 偏移稳定」）；2.3 SEC 命名与 libbpf 解析（`bpf_helpers.h` 文档化 SEC 名义法；SEC→prog_type+attach_type 映射表[本章涉及的 15 种 SEC 逐行]；`SEC("tc")` 与旧名 `classifier` 的关系）；2.4 挂载机制（`bpf_link` 与 `link_create`：attach 类型决定 link 子操作；自动 attach 的实现路径；fentry/fexit 依赖 BTF 的原因与签名漂移的加载期校验——冒烟结论⑤的 vfs_unlink 漂移例证进正文）；2.5 返回值校验（verifier 对程序返回值的按类型检查：XDP 必须 xdp_action、tracing 0/1 等——`bpf_prog_type` 决定的 enforce 规则，保守措辞）；2.6 检查点（2 问+提示）。
- [ ] **Step 4: 核对（硬性）**

Run: `grep -c "^## " book/chapters/04-program-types-and-hook-points.md && grep -c "^### 1\." book/chapters/04-program-types-and-hook-points.md && grep -c "^### 2\." book/chapters/04-program-types-and-hook-points.md && wc -m book/chapters/04-program-types-and-hook-points.md`
Expected: `4`（学习目标/导读/一/二）、`5`、`6`；`wc -m ≥ 16000`（本任务中段值参考；Task 11 终检 35–52k）。
- [ ] **Step 5: 提交**

```bash
git add book/chapters/04-program-types-and-hook-points.md
git commit -m "docs(ch04): 学习目标/导读/一/二（类型系统与内核注册机制）"
```

---

### Task 7: 正文「三、类型画廊：十四类逐类走读」

**tasks.md 条目**：「Write full chapter prose covering …」（画廊走读部分）＋「Add text comparison table …」（表落位）。

**Files:**
- Modify: `book/chapters/04-program-types-and-hook-points.md`

- [ ] **Step 1: 写「三、类型画廊：十四类逐类走读」（12–15k）**——小节锚点 `### 3.1`–`### 3.7`（五副眼镜）+ `### 3.8`–`### 3.14`（目录级 7 card，fentry/fexit 已在 3.5/3.6；kretprobe 单独 card 后 uprobe…按 C3 目录序 3.8–3.14 对应画廊 8 目录中的 7 类[kretprobe/uprobe/cgroup-skb/socket-filter/sockops/sk-msg/lsm/netfilter 中八类——注意 14 类计数：3.1 tracepoint 3.2 raw_tp 3.3 kprobe 3.4 fentry 3.5 fexit 3.6 XDP 3.7 TC 3.8 kretprobe 3.9 uprobe 3.10 cgroup-skb 3.11 socket-filter 3.12 sockops 3.13 sk-msg 3.14 lsm+netfilter 合并 card（同为「verdict 决策点」对照讲）]）。每 card 固定六段：背景问题 → 代码（贴对应文件全文或关键段）→ 预期输出/挂载形态（标注「预期输出示例 / 运行级待验证」）→ 类型要点（ctx/返回值/helper 面哪个三元组维度被点亮）→ 与相邻类型的选择边界 → 独立示例链接；3.1–3.7 五镜共用一张「同一事件五副眼镜」对照讲解（贴 multi_observe.bpf.c 全文 + 分镜 map 读数解读）；每 card 失败形态段回指第 3 章对应 card（不重复展开）。
- [ ] **Step 2: 三节末尾落双视角**：**教师视角**段（为什么用「同一事件五副眼镜」开画廊——ctx 差异不看实物永远停在表格层面；读者容易卡住的地方：把「SEC 名字写错」当「类型选错」排错）+ **学生动手**段（逐目录 `make` 画廊 8 类、跑 01 锚点读分镜计数、把每类的返回值语义抄进自己的笔记）+ 检查点（3 问+提示）。
- [ ] **Step 3: 核对（硬性）**

Run: `grep -c "^### 3\." book/chapters/04-program-types-and-hook-points.md && grep -c "预期输出示例" book/chapters/04-program-types-and-hook-points.md`
Expected: `14`、`≥ 14`。
- [ ] **Step 4: 提交**

```bash
git add book/chapters/04-program-types-and-hook-points.md
git commit -m "docs(ch04): 三、类型画廊十四类走读"
```

---

### Task 8: 正文「四、实战：为任务选对类型并挂上钩子」

**tasks.md 条目**：「Main-agent review to ensure the chapter explains selection boundaries, not just hook taxonomy.」（实战落点前置）＋「Write full chapter prose covering …」（实战部分）。

**Files:**
- Modify: `book/chapters/04-program-types-and-hook-points.md`

- [ ] **Step 1: 写「四、实战」（6–8k，5.1 五层环境紧凑版 3–4k + 5.2 代码走读 3–4k）**——5.1 五层：依赖（内核/BTF/tracefs/CONFIG_BPF_LSM 与 lsm=bpf/netfilter 6.6+/权限矩阵[非 root 只读、root 全家桶]→00-env 输出示例）；编译（clang/bpftool/libbpf 版本与验证命令）；部署（挂载即部署：tracepoint/kprobe/fentry 挂上即生效，XDP/TC 需接口[lo 或 veth]；安全警告统一措辞；多副眼镜同挂的注意点）；运行（`sudo ./multi_observe`、`sudo ./xdp_tc` 与制造流量命令）；效果与验证状态约定（分镜计数/双计数对照的预期输出示例，标注「运行级待验证」）。5.2 四段式：教师视角引入（选型是本章的最终产出：三步法——要什么数据→谁产生它→哪个类型的 ctx 给它）→ 核心代码原理讲解（走读 multi_observe.c 的 attach 循环与分镜读数、xdp_tc.c 的两种 attach 路径对照）→ 学生动手（跑两锚点、改镜编号加一镜[kretprobe]、观察 openat2 失败路径 ret<0 的计数）→ 检查点（3 问+提示，第 1 问指向「为什么五镜计数不完全相等」[raw_tp 不读文件名/计数时点不同]）。
- [ ] **Step 2: 核对（硬性）**

Run: `grep -c "^### 5\." book/chapters/04-program-types-and-hook-points.md`
Expected: `2`。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/04-program-types-and-hook-points.md
git commit -m "docs(ch04): 四、实战（五层环境与选型走读）"
```

---

### Task 9: 正文「可视化/常见问题与排错/课后扩展任务/总结/本章参考来源」

**tasks.md 条目**：「Add text comparison table for trigger source, context, helper set, return behavior, stability, and typical use case.」（表复核落位）＋「Add troubleshooting, summary, and references.」。

**Files:**
- Modify: `book/chapters/04-program-types-and-hook-points.md`

- [ ] **Step 1: 写「可视化：类型谱系与挂载路径」（1.5–2k）**——图 1：`bpf_prog_type → bpf_attach_type → 挂载点`三层文本图（kprobe 族一型三 attach 的展开例）；图 2：同一事件五副眼镜数据流图（syscall→内核函数→五 hook→分镜 map→loader）。两图均配解读段。
- [ ] **Step 2: 写「常见问题与排错」（2.5–3.5k）**——选型速查表（症状→类型规则→修复：SEC 名写错的加载报错形态[unknown prog type / unknown attach type]、ctx 字段访问被拒[invalid bpf_context access 回指第 3 章]、fentry 签名漂移[BTF 校验失败形态与解析法]、挂载点不存在[tracefs 缺事件/CONFIG_BPF_LSM 未开]、权限档位差异）；每条标注「预期日志 / 运行级待验证」。
- [ ] **Step 3: 写「课后扩展任务」（含 C9 表逐字）+「总结」（含预告勾稽表：兑现 ch02/ch03 预告，给第 5/6/15/16/20 章留桥）+「本章参考来源」（kernel docs program-types/attach 文档、bpf_helpers/bpf_tracing 头文档、LWN XDP/LSM/fentry 系列、Andrii Nakryiko libbpf 系列）。
- [ ] **Step 4: 核对（硬性）**

Run: `grep -c "^## " book/chapters/04-program-types-and-hook-points.md && wc -m book/chapters/04-program-types-and-hook-points.md`
Expected: `11`（C7 十一小节齐）；篇幅进入 35–52k 区间或记录差值待 Task 11 终检。
- [ ] **Step 5: 提交**

```bash
git add book/chapters/04-program-types-and-hook-points.md
git commit -m "docs(ch04): 可视化/排错/课后任务/总结/参考来源"
```

---

### Task 10: 练习参考实现（01-type-selection、02-type-mismatch）与 exercises 总览

**tasks.md 条目**：「Create standalone Chapter 4 examples under `book/examples/chapter-04/`.」（exercises 部分）＋ STYLE.md 实战模板规则 4。

**Files:**
- Create: `book/examples/chapter-04/exercises/README.md`
- Create: `book/examples/chapter-04/exercises/01-type-selection/{solution.bpf.c,Makefile,README.md}`
- Create: `book/examples/chapter-04/exercises/02-type-mismatch/{broken.bpf.c,fixed.bpf.c,Makefile,README.md}`

- [ ] **Step 1: 创建 `01-type-selection/solution.bpf.c`**（逐字如下——三场景的正确选型合并对象）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 1 参考实现：三个场景的正确选型合并为一个可编译对象。
 * 场景A 容器内文件打开审计 → LSM（lsm/file_open）：安全决策点、内核对象直读。
 * 场景B TCP 连接建立观测 → sockops：socket 级生命周期回调，握手完成即可见。
 * 场景C 内核函数入口参数抓取 → fentry：BTF 类型化直参，签名加载期校验。
 * 选型论证（含错误选项为何不行）见本目录 README 分界线之后。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* 三场景共用一张 ARRAY 计数表（key=场景编号），map 选型：ARRAY——key 恒定 3 个 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);
    __type(key, __u32);
    __type(value, __u64);
} scenario_hits SEC(".maps");

/* 场景A：file_open hook——审计容器内文件打开（返回 0=允许，只审计不拦截） */
SEC("lsm/file_open")
int BPF_PROG(audit_open, struct file *file)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

/* 场景B：passive established——服务端视角的连接建立计数 */
SEC("sockops")
int so_conn(struct bpf_sock_ops *ops)
{
    if (ops->op != BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB)
        return 0;
    __u32 key = 1;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 1;
}

/* 场景C：fentry 抓 do_sys_openat2 入口参数——dfd/filename 均可直接命名 */
SEC("fentry/do_sys_openat2")
int BPF_PROG(grab_params, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 2;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 2: 创建 `02-type-mismatch/broken.bpf.c` 与 `fixed.bpf.c`**（逐字如下）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 2 任务体：类型错位（预期被拒：ctx 访问越权——预期日志关键行
 * 「invalid bpf_context access」形态，运行级待验证）。
 * 错误：报文解析逻辑属于网络 ctx，却放在 kprobe SEC 且 ctx 声明为 xdp_md——
 * kprobe 的 is_valid_access 只放行 pt_regs 形状的字段（第 3 章 2.3/3.6 回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} mismatch_hits SEC(".maps");

SEC("kprobe/do_sys_openat2")
int probe_broken(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;       /* 预期被拒：kprobe ctx 无 data 字段 */
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&mismatch_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 2 参考实现：SEC 与 ctx 类型归位——报文解析逻辑回到 XDP。
 * 逻辑一字未动（数据/边界检查/计数），只把「程序类型三元组」换对：
 * SEC("xdp") + struct xdp_md *——ctx 访问从此在 is_valid_access 白名单内。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} mismatch_hits SEC(".maps");

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&mismatch_hits, &key);
    if (!v)
        return XDP_PASS;
    __sync_fetch_and_add(v, 1);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [ ] **Step 3: 创建两个练习 `Makefile`**——C2 模板替换：练习 1 `<SRCS> := solution.bpf.c`；练习 2 `<SRCS> := broken.bpf.c fixed.bpf.c`。
- [ ] **Step 4: 创建两个练习 `README.md`**——按 C5 防剧透七段结构。关键口径：练习 1 `## 提示`：三元组三问（要什么数据/谁产生/哪个 ctx 给它）+ 三场景各自的候选类型集合；`## 参考实现思路`（分界线后）：选型论证表（场景→选型→错误选项为何不行：场景A 用 kprobe 行不行[能拿到参数但拿不到 LSM 决策点语义与 audit 语义]、场景B 用 XDP 行不行[看不见 socket 状态]等）；练习 2 `## 提示`：从日志第一处 ctx 访问拒绝入手（回指第 3 章排错节）；`## 参考实现思路`：错位诊断三步（看 SEC→看 ctx 类型→看 is_valid_access 白名单）；两者 `## 预期输出示例`（运行级待验证；练习 2 给完整预期拒绝日志并标注「随内核版本略有差异」）；`## 验证状态` 按 C6。
- [ ] **Step 5: 创建 `exercises/README.md`**：C9 逐字任务总览表 + 防剧透阅读法 + 各练习构建命令（各目录 `make`）+ 验证状态汇总表（按 C6）。
- [ ] **Step 6: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-04/exercises/01-type-selection && make -C book/examples/chapter-04/exercises/02-type-mismatch`
Expected: 退出码均 0（练习 2 生成 `broken.bpf.o` 与 `fixed.bpf.o`——broken 也必须编译通过）。失败即修代码重跑。
- [ ] **Step 7: 提交**

```bash
git add book/examples/chapter-04/exercises/
git commit -m "feat(ch04): 课后练习参考实现（选型决策与类型错位修复）"
```

---

### Task 11: scripts/verify-build.sh、全量编译验证、正文交叉链接与篇幅终检

**tasks.md 条目**：「Create standalone Chapter 4 examples under `book/examples/chapter-04/`.」（scripts 部分）＋「Add troubleshooting, summary, and references.」（验证状态全量标注收口）。

**Files:**
- Create: `book/examples/chapter-04/scripts/verify-build.sh`（可执行，`chmod +x`）
- Modify: `book/chapters/04-program-types-and-hook-points.md`（交叉链接）
- Modify: `book/examples/chapter-04/README.md`（回填验证摘要）

- [ ] **Step 1: 创建 `scripts/verify-build.sh`**——模式逐字沿用第 3 章 verify-build.sh（`book/examples/chapter-03/scripts/verify-build.sh` 为基底），替换 run 项为：`00-env`、`01-multi-observe`、`02-xdp-tc`、`03-type-gallery/01-kretprobe … 08-netfilter`（8 项）、`exercises/01-type-selection`、`exercises/02-type-mismatch`（共 13 项 run）；头部注释改第 4 章（「类型画廊与锚点全链」口径）。

- [ ] **Step 2: 首次全量运行（硬性验收）**

Run: `bash book/examples/chapter-04/scripts/verify-build.sh`
Expected: `PASS 00-env`、`PASS 01-multi-observe`、`PASS 02-xdp-tc`、`PASS 03-type-gallery/01..08`（八行）、`PASS exercises/01-type-selection`、`PASS exercises/02-type-mismatch`；汇总 `FAIL=0`，退出码 0。任何 FAIL 回到对应任务修复后重跑。
记录：把本次运行摘要（PASS/FAIL 数与工具版本实测值）替换进 `book/examples/chapter-04/README.md` 验证状态说明区的占位句。

- [ ] **Step 3: 正文交叉链接**（C8 惯例）：章首「本章独立示例已全部落地到 [`book/examples/chapter-04/`](../examples/chapter-04/)」与目录总览；5.2 与三节每个代码块前后标注对应文件路径；「一/二/三/四」各节末尾「**独立示例**」链接（二节末为附录 B/C 链接）；三节各 card 链接画廊/锚点目录；课后扩展任务节链接 exercises/ 两目录与总览 README。

- [ ] **Step 4: 交叉链接核对（硬性）**

Run: `grep -c "](../examples/chapter-04/" book/chapters/04-program-types-and-hook-points.md && grep -c "](../appendix/" book/chapters/04-program-types-and-hook-points.md`
Expected: `≥ 12`（锚点 2 + 画廊 8 + exercises 2 + 顶层/画廊 README）；`≥ 1`。

- [ ] **Step 5: 篇幅终检（硬性）**

Run: `wc -m book/chapters/04-program-types-and-hook-points.md`
Expected: 35000–52000。低于下限：优先补原理讲解而非注水（写作纪律——每个概念钉回读者已见过的代码）；高于上限：压缩重复叙述，不删原理。

- [ ] **Step 6: 提交**

```bash
git add book/examples/chapter-04/ book/chapters/04-program-types-and-hook-points.md
git commit -m "docs(ch04): 交叉链接示例资产并完成全量编译级验证标注"
```

---

### Task 12: 主审（teaching completeness）与 tasks.md 勾选（收口）

**tasks.md 条目**：「Main-agent review to ensure the chapter explains selection boundaries, not just hook taxonomy.」；同时统一勾选「Chapter 4」全部 7 条。

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`（勾选）
- 产出：审查结论进入最终回复（不创建报告文件）

- [ ] **Step 1: 逐项审计并记录结论**（发现问题回对应任务修复后复验；全绿才进 Step 2）：
  1. 教学完整性四条（STYLE.md 实战模板）：五层环境（5.1 五个 `####` 层落到命令/脚本/表格；tracefs/LSM/netfilter 特有检查在场）；核心代码原理注释（抽查 `multi_observe.bpf.c`、画廊若干：五类注释在场）；双视角（一/三/四节「教师视角」「学生动手」+检查点齐备）；课后练习（2 项目录、防剧透七段 README、参考实现可编译、验证状态标注）。
  2. 结构级（设计 §8）：C7 十一小节齐备；三节 14 card 与画廊/锚点目录一一对应；5.2 代码块与示例文件一一对应（抽查 3 处路径）；可视化两图均有解读段；十四类总对比表六列齐备（矩阵第 5 条）。
  3. 术语与保守措辞：译名一致；「英文（中文解释）」首现格式；「预期输出示例 / 运行级待验证」统一口径；签名「以本机 BTF 为准、随内核版本演进」保守措辞；全文零「已验证通过」式运行级表述（grep 复核，排除约定表原文自引）。
  4. 矩阵专项核对：七条逐条有交付物；**选型边界审查**（矩阵第 7 条）：一节决策树/三节各 card 选择边界段/练习 1 论证表三处在场——「解释选择边界，而非只给分类学」。
- [ ] **Step 2: 勾选 tasks.md**：将「Chapter 4: Program Types and Hook Points」7 条全部置为 `[x]`（仅当对应交付物真实存在且验收通过）。
- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch04): 勾选 Chapter 4 矩阵任务（Batch 2 完成）"
```

---

# 第 2 章 eBPF 架构与核心概念

## 学习目标

读完本章后，你应当能够：

- 说出 eBPF 系统的八个组成要素——程序（program）、映射（map）、辅助函数（helper）、程序类型（program type）、挂载点（attachment point）、上下文对象（context）、事件通道（event channel）、生命周期（lifecycle）——并按顺序描述它们在一次「事件触发 → 数据可见」过程中的协作关系。
- 手工解码一条 8 字节的 `struct bpf_insn`：给定 `b7 00 00 00 00 00 00 00` 这样的原始字节，还原出它对应的 eBPF 汇编语句。
- 说明 eBPF 的寄存器模型与调用约定：哪个寄存器放返回值、哪几个放参数、哪几个在函数调用后必须保持不变。
- 按照三节末（3.9）的选型决策表，为「精确查找、计数统计、容量受限字典、事件流、前缀匹配」等场景选择合适的映射类型。
- 讲清 `bpf()` 系统调用创建的内核对象其文件描述符（fd）所有权、引用计数与 pin（持久化）三种生命周期形态（四节 4.2–4.5 展开）。
- 独立完成 `book/examples/chapter-02/01-trace-stat/` 的编译与运行，并解读其输出的统计口径（为什么读回时要对 per-CPU 槽位求和）。
- 完成 2 项课后扩展任务：把轮询改为 ring buffer 事件流；用 LRU hash 实现每进程调度 top-N 榜单。

## 本章导读

第 1 章末尾留下一个预告：「第 2 章补齐指令集和 map」。本章兑现它，并且不止于此——第 1 章把 eBPF 描述为「一套机制」而非单一工具，本章的任务是把这句话展开成一份可以逐个检查的零件清单：每个要素叫什么、解决什么问题、在读者已经运行过的第 1 章示例里对应哪一行代码。

学习路线如下。第一步是全景：「一、eBPF 架构总览」用 1.1–1.9 九个小节把八要素逐一讲清，每小节都钉回第 1 章 `hello_exec` 示例的具体代码位置。第二步是两根支柱：指令集（二节）回答「程序在内核里以什么形态存在」，映射类型（三节）回答「状态如何保存、数据如何共享」。第三步下探内核：四节进入 `kernel/bpf/syscall.c`，分析 `bpf()` 如何创建对象、管理 fd 与引用计数。第四步动手：五节用本章独立示例 01-trace-stat 走完「五层环境 → 代码走读」的完整闭环。最后以可视化、常见问题与排错、课后扩展任务收尾。

两个阅读约定先说在前面。其一，本章采用双视角行文：加粗的「**教师视角**」段落给出类比、常见误区与教学取舍；「**学生动手**」段落给出可操作的练习；各节末尾的检查点用编号自测问题收拢。其二，验证状态沿用全书约定：确实在编写环境执行过的命令标注「本地验证」；需要加载权限的效果一律写「预期输出示例」并标注「运行级待验证」。本章全部示例资产位于 `book/examples/chapter-02/`，编译级验证脚本为 `book/examples/chapter-02/scripts/verify-build.sh`。

## 一、eBPF 架构总览

一个 eBPF 工具由内核态程序与用户态 loader 组成，但「程序」只是入口——它运转起来依赖一整套要素。本节逐一建立这八个要素的直觉，每个小节按「背景问题 → 机制 → 示例 → 常见失败点」推进；其中最复杂的两块（映射与 `bpf()` 系统调用）由三、四两节深入。

### 1.1 程序（program）：经 verifier 验证的受限 C 子集

背景问题：如何让开发者写的代码在内核里执行，又不把内核稳定性押注在作者的自制力上？内核模块的答案是「信任作者」，代价是一个空指针就可能 panic。eBPF 的答案是「限制表达力，再做加载前验证」。

机制上，eBPF 程序用 C 的受限子集编写：不能有无法证明会终止的循环，不能直接解引用任意内核指针，只能调用内核以固定编号暴露的辅助函数。源文件经 `clang -target bpf` 编译为字节码，loader 经 `bpf(BPF_PROG_LOAD)` 提交内核，验证器（verifier）静态检查通过后即时编译（JIT，Just-In-Time）为本机指令。第 1 章 `hello_exec.bpf.c` 的 `handle_execve` 与本章示例的 `trace_stat` 函数都是完整程序。

**教师视角**：把 eBPF 程序想象成搭在内核这栋大楼外的「外包脚手架」——由外部运入、先过安全检查（verifier）才能架设、只能在指定位置（挂载点）作业、撤走时不留残骸。最常见误区是把它当成内核模块：eBPF 没有完整内核权限，不能随意调用内核符号、不能长期持有复杂内核对象，内存访问被 verifier 事先证明安全；反过来它也做不了驱动和文件系统这类活。另一个误区是把它和 BCC、bpftrace、bpftool 混为一谈——那些是架在机制之上的前端工具，机制只有一套。

常见失败点：普通 C 写法（无限循环、堆分配、直接访问用户态指针）多数能通过 clang，却在 verifier 处被拒——「编译成功」和「能加载」是两件事，中间隔着第 3 章的静态验证。

### 1.2 映射（map）：内核态与用户态的共享数据对象

背景问题：第 1 章的 `hello_exec` 有个明显局限——没有记忆。每次 `execve` 触发都是独立的函数调用，局部变量随调用结束消失，程序数不了「第几次触发」，用户态也只能读 trace_pipe 里的文本行。跨事件保存状态、向用户态交结构化数据，都需要一个双方可访问的存储。

机制上，映射（map）是内核分配、两个世界共同访问的数据结构：内核态经辅助函数 `bpf_map_lookup_elem`/`bpf_map_update_elem` 读写，用户态经 map 关联的 fd 用 `bpf()` 的 `BPF_MAP_LOOKUP_ELEM` 等命令访问（libbpf 在 `<bpf/bpf.h>` 提供同名封装）。map 类型决定 key/value 形态与并发语义，三节展开八类。

承接点：第 1 章示例刻意不含 map——`bpf_printk` 直出 trace_pipe，绕开了状态保存。本章补上这块拼图：独立示例 `book/examples/chapter-02/01-trace-stat/` 的 BPF 侧定义了 per-CPU 数组映射（每 CPU 一份独立副本，3.4 展开）`sched_counts`，按 CPU 下标累计 `sched_switch` 次数并记录最近切入的进程 pid，loader 每秒读回（走读见五节）。

常见失败点：BPF 侧自定义的 value 类型（如 `struct cpu_stat`）不会被 skeleton 导出，loader 侧必须重新定义并逐字段一致，否则读回数据被错误解释；多 CPU 写同一表项会丢计数，per-CPU 变体为此而生。

### 1.3 辅助函数（helper）：内核提供的受控能力面

背景问题：eBPF 程序不能直接调用内核函数——不能改写内核内存、不能主动调度——那它怎么获取当前 CPU 编号、怎么访问 map？答案是内核主动暴露的一组能力，且只有这些。

机制上，辅助函数（helper）由内核实现、以稳定编号注册在 UAPI（`include/uapi/linux/bpf.h`），BPF 侧经 `<bpf/bpf_helpers.h>` 声明后像普通函数一样调用。看本章示例真正用到的两个：

- `bpf_get_smp_processor_id()`：返回当前 CPU 下标。01-trace-stat 用它选定 per-CPU 数组的 key（`trace_stat.bpf.c` 的 `__u32 cpu = bpf_get_smp_processor_id();`）——获取当前 CPU 的稳定途径，无需解析上下文。
- `bpf_map_lookup_elem(&sched_counts, &cpu)`：按 key 查表，返回 value 指针或 NULL，NULL（key 越界、表项不存在）必须显式检查。

「受控」有两层：能力集合由内核决定；且**每个程序类型只能看到自己可用的那部分 helper**——tracepoint 程序调用不了 XDP 专用 helper，反之亦然。这正是下一小节「程序类型」的意义之一。

常见失败点：调用当前类型不支持的 helper，编译期不报错（原型都在头文件里），加载时被 verifier 以「该程序类型不能使用此 helper」类日志拒绝（第 3 章）。

### 1.4 程序类型（program type）：上下文、helper 集合与返回值语义的总开关

背景问题：同样是「读上下文、做处理、返回结果」，为什么要分出几十种程序类型？因为挂载位置不同，事件现场与返回值约定完全不同。

机制上，程序类型（program type）同时决定三件事：

1. **上下文对象的结构**：程序入口第一个参数是什么形状（1.6）。
2. **可用的 helper 集合**：1.3 末尾伏笔在此回收——能力面按类型裁剪。
3. **返回值语义**：XDP 程序返回动作码（`XDP_DROP` 丢弃、`XDP_PASS` 放行）；tracepoint 程序的返回值被内核忽略，按惯例返回 0。具体语义以各类型的内核实现为准。

以 tracepoint 类型展开：它是观测入门最常用的类型，挂载在内核静态埋设的 tracepoint 上。第 1 章 `hello_exec` 的 `SEC("tracepoint/syscalls/sys_enter_execve")` 与本章 `trace_stat` 的 `SEC("tracepoint/sched/sched_switch")` 都属此类——SEC 前缀 `tracepoint/` 被 libbpf 翻译成程序类型与挂载目标（1.5）。完整谱系（XDP、TC、cgroup、kprobe、fentry/fexit、LSM 等）随内核版本增长，第 4 章逐类展开；本节记住「类型 = 上下文 + helper 面 + 返回值语义」即可。

常见失败点：类型语义错位——例如期望 tracepoint 程序的返回值能丢包（tracepoint 是纯观测点，返回值没有动作语义）。先确认类型，再写逻辑。

### 1.5 挂载点（attachment point）：程序在哪里被触发

背景问题：通过验证的程序只是「待命」，必须挂到某个内核位置，事件到来时才会被调用。挂载点决定触发时机与频率，而频率直接决定开销量级——`sched_switch` 在繁忙机器上每秒触发上万次，低频 tracepoint 可能一天命中不了几次。

机制上，现代 libbpf 工作流有两种挂载模式：

1. **SEC 名隐式推断**。`SEC("tracepoint/sched/sched_switch")` 提供了挂载所需的全部信息：类别 + 子系统 + 事件名。loader 调用 skeleton 生成的 `trace_stat_bpf__attach(skel)`，无需任何显式参数，第 1 章 `hello_exec_bpf__attach()` 同理。
2. **显式 attach**。挂载信息无法从 SEC 名推出时由用户态显式提供。典型是 XDP：SEC 名只写 `SEC("xdp")`，「挂到哪块网卡」必须由 loader 显式传入接口索引。tracepoint 从 SEC 完整推断、XDP 需要显式接口，是两种模式的分界记忆点；第 17 章防火墙实战会大量用到 XDP/TC 的显式挂载与 link 管理。

常见失败点：SEC 名写错时编译期一切正常——section 名只是 ELF 里的字符串，clang 不校验内容；错误在 attach、libbpf 按名查找目标事件失败时才暴露。这是最典型的「编译通过、运行报错」。

### 1.6 上下文对象（context）：事件现场的结构化视图

背景问题：事件发生那一刻，内核把什么交给程序？答案是上下文对象（context）：按挂载点类型构造的结构体指针，作为第一个参数传入。程序对它只能读，字段由程序类型定义（1.4）。

机制上，tracepoint 上下文结构体（`struct trace_event_raw_*`）由内核的 tracepoint 声明生成；CO-RE 工作流不 include 内核头，而用 `bpftool btf dump` 从 `/sys/kernel/btf/vmlinux` 生成的 `vmlinux.h` 获得类型定义。以下是本机（内核 7.0.0-31，BTF 实测）的 `trace_event_raw_sched_switch`：

```c
/* 本机 BTF 实测字段布局；字段随内核版本可能增删或调整顺序，
   跨版本访问由 CO-RE 重定位在加载时修正（第 8 章展开机制） */
struct trace_event_raw_sched_switch {
    struct trace_entry ent;   /* 8 字节公共记录头，所有 tracepoint 上下文都有 */
    char prev_comm[16];       /* 让出 CPU 的进程名（定长数组） */
    pid_t prev_pid;
    int prev_prio;
    long prev_state;
    char next_comm[16];       /* 接管 CPU 的进程名 */
    pid_t next_pid;
    int next_prio;
};
```

01-trace-stat 读取的正是 `ctx->next_pid`（`p->last_next_pid = ctx->next_pid;`）。注意 `prev_comm`/`next_comm` 是 16 字节定长数组（本机 BTF 实测）而非变长字符串，直接读取不越界，不需要 `bpf_probe_read_str`——那类 helper 服务于 `__data_loc` 变长字段（五节与排错节展开）。「字段布局随内核版本变化」不是客套话：按固定偏移硬读会读错位置，CO-RE（Compile Once - Run Everywhere，一次编译、到处运行）在加载时依据目标内核 BTF 重定位字段偏移，这正是 vmlinux.h 工作流可移植的原因；内核未开启 `CONFIG_DEBUG_INFO_BTF` 时，附录 C 给出了自行构建带 BTF 内核的路径。

常见失败点：对 ctx 做指针算术或越界读会被 verifier 拒绝；把 ctx 当可写内存同样被拒。字段名拼错在编译期就会暴露（vmlinux.h 没有该成员）——与 SEC 名写错（运行期才暴露）方向正好相反。

### 1.7 事件通道（event channel）：数据如何到达用户态

背景问题：程序在内核里执行完毕，结果怎么让用户态看到？观测类程序的一切价值最终落在「数据可见」，而通道不止一条。按演进顺序有三代，各自延迟、开销与丢失语义一句话：

1. **`bpf_printk` → trace_pipe**（第 1 章 `hello_exec`）：实现最简单，文本直写 tracing 缓冲区；用户态 `cat trace_pipe` 才能看到，延迟不定；高频时格式化开销大、无结构，仅适合教学与临时诊断。
2. **轮询 map**（本章 01-trace-stat）：程序把统计累计进 `sched_counts`，loader 每秒读回。数据结构化、数据面开销极低；新鲜度取决于轮询周期，只有「最新状态」语义，中间变化不可见。
3. **ring buffer 推送**（练习 1）：程序用 `bpf_ringbuf_reserve`/`bpf_ringbuf_submit` 推事件，用户态 `ring_buffer__poll` 消费。每事件可见、延迟低；代价是每事件一次 reserve/submit 开销，缓冲满后新事件被静默丢弃——观测程序不得反压内核路径。

取舍口诀：状态统计选轮询 map，逐事件分析选 ring buffer（3.5/3.6 与排错节对照表展开）。

### 1.8 生命周期（lifecycle）：从加载到卸载的六步

背景问题：一个 eBPF 对象从诞生到消亡经过哪些状态？谁决定它何时消失？想清楚这两个问题，多数「进程退出了 eBPF 还在不在」的困惑都会消失。

```text
加载            验证            JIT            挂载           触发             卸载
bpf(PROG_LOAD) -> verifier 检查 -> 编译为本机码 -> attach 挂载点 -> 事件到达时执行 -> 最后一个引用释放，对象回收
```

六步各一句话：加载由 loader 发起，走 `bpf(BPF_PROG_LOAD)` 路径（四节 4.3）；验证是第 3 章主题；JIT 编译为本机指令，架构相关（第 5–7 章）；挂载见 1.5；触发是数据面——事件到达、程序执行、读写 map；卸载由引用计数决定：fd 关闭、进程退出、link 销毁都在减少引用，最后一个引用释放时内核回收对象。一句话版本：01-trace-stat 的「Ctrl-C 退出即卸载」成立，是因为 skeleton 管理的程序、link 与 map 引用都随进程消失——fd 引用计数完整机制留给四节 4.4 与可视化节。

### 1.9 八要素对照第 1 章示例

概念不钉回你已经运行过的代码就只是名词。下表把八要素对应到第 1 章 `hello_exec` 的具体代码位置与本章扩展：

| 要素 | 第 1 章 `hello_exec` 代码位置 | 本章扩展 |
|---|---|---|
| 程序（program） | `hello_exec.bpf.c` 的 `SEC(...) int handle_execve(...)` | 同一形态换成 `trace_stat`，二节拆开它的字节码 |
| 映射（map） | 无——示例刻意不含 map | `sched_counts`（per-CPU 数组）补上状态保存（1.2、三节） |
| 辅助函数（helper） | `bpf_get_current_pid_tgid()`、`bpf_get_current_comm()`、`bpf_printk()` | `bpf_get_smp_processor_id()`、`bpf_map_lookup_elem()`（1.3） |
| 程序类型（program type） | SEC 前缀 `tracepoint/` 隐式确定 tracepoint 类型 | 讲清类型如何决定上下文、helper 面与返回值（1.4） |
| 挂载点（attach point） | `tracepoint/syscalls/sys_enter_execve`，`hello_exec_bpf__attach()` 自动挂载 | `tracepoint/sched/sched_switch`；两种挂载模式的分界（1.5） |
| 上下文对象（context） | `struct trace_event_raw_sys_enter *ctx`（签名需要，未读字段） | 读取 `trace_event_raw_sched_switch` 的 `next_pid`（1.6） |
| 事件通道（event channel） | `bpf_printk` 直出 trace_pipe | 轮询 map（01-trace-stat）；ring buffer 推送（练习 1）（1.7） |
| 生命周期（lifecycle） | `open_and_load` → attach → Ctrl-C 退出 | 六步总览与 fd 引用计数（1.8、四节） |

**学生动手**：重开第 1 章的 `hello_exec.bpf.c` 与 `hello_exec.c`，对照上表逐行确认八个要素的位置；再对 `book/examples/chapter-02/01-trace-stat/` 的两个源文件做同样对照，确认换了示例后八要素一个不少、只是每一项都前进一步。

检查点（先自答，再看提示）：

1. 为什么第 1 章的 `hello_exec` 不需要 map？（提示：它不保存跨事件状态、不回传结构化数据，`bpf_printk` 直出 trace_pipe；一旦要「按 CPU 累计调度次数」，没有 map 就无处可写。）
2. SEC 名写错（如拼成 `tracepoint/sched/sched_swich`），错误在编译期还是运行期？（提示：section 名只是 ELF 里的字符串，clang 不校验；错误在 attach、libbpf 按名找不到目标 tracepoint 时才暴露——运行期。）
3. 上下文字段（如 `next_pid`）是谁定义的？（提示：布局由内核中该 tracepoint 的声明决定，BPF 侧的类型定义来自 BTF 生成的 vmlinux.h；跨版本一致性由 CO-RE 重定位保证。）

## 二、指令集基础

一节把程序当「黑盒部件」讲了它的角色。本节打开黑盒：对内核而言，eBPF 程序不是 C 代码，而是一串定长 8 字节的指令。理解指令集不是为了手写汇编，而是为了三件实际的事：看懂 `bpftool prog dump` 的输出（2.4），理解 verifier 为什么能对程序做静态证明（第 3 章的基础），以及会查附录 A 的完整速查表。

### 2.1 64 位 RISC 模型：寄存器与调用约定

背景问题：内核为什么不直接执行 x86-64 机器码？因为通用架构指令组合上千、寻址模式复杂、每条指令的副作用难以枚举，verifier 无法对它做静态安全证明。eBPF 选择精简指令集计算机（RISC，Reduced Instruction Set Computer）风格：指令语义有限、行为可枚举、每条指令定长。这不是性能选择，而是「可验证性」选择——指令集越小，verifier 能证明的越多（第 3 章的前提）。

eBPF 共 11 个寄存器：r0–r9 通用，r10 是只读栈帧指针，全部 64 位宽。调用约定：

| 寄存器 | 角色 | 说明 |
|---|---|---|
| r0 | 返回值 | 程序返回值与 helper 调用返回值都放在 r0 |
| r1–r5 | 参数 | 第 1–5 个参数依次放在 r1–r5；程序入口处 r1 固定是上下文对象指针 |
| r6–r9 | callee-saved | 跨函数调用保持不变；需要跨 helper 调用存活的值放在这几个寄存器 |
| r10 | 只读栈帧指针 | 恒指向当前栈帧；不能写 r10，只能以 `r10 + 偏移` 访问局部变量 |

两点补充：超过 5 个参数的调用极少出现，按约定把多余参数放在栈上，细节以内核指令集文档为准；eBPF 栈固定 512 字节，数组开大一点就可能碰上限，verifier 对栈越界的检查因此非常严格。

调用约定不是纸面规定，它直接解释示例程序的字节码。看 01-trace-stat 反汇编的前几条（`llvm-objdump -d` 对 ELF 对象文件反汇编是纯编译级操作，无需加载权限；输出为本地验证）：

```text
$ llvm-objdump -d book/examples/chapter-02/01-trace-stat/trace_stat.bpf.o
       0:	bf 16 00 00 00 00 00 00	r6 = r1                ; 把 ctx 从 r1 存进 r6
       1:	85 00 00 00 08 00 00 00	call 0x8               ; bpf_get_smp_processor_id()
       2:	63 0a fc ff 00 00 00 00	*(u32 *)(r10 - 0x4) = r0   ; 返回值溢出到栈
```

第 0 条把上下文指针从 r1 拷进 r6，第 2 条把 helper 返回值（r0）写进栈上槽位——r0–r5 在 helper 调用后不保证保留，只有 r6–r9 跨调用存活。编译器在替你执行调用约定。

### 2.2 8 字节指令编码：struct bpf_insn

每条 eBPF 指令定长 8 字节，格式定义在 `include/uapi/linux/bpf.h`：

```c
struct bpf_insn {
    __u8  code;       /* 操作码（opcode）：决定指令类别与操作数的解释方式 */
    __u8  dst_reg:4;  /* 目标寄存器编号（字节的低 4 位） */
    __u8  src_reg:4;  /* 源寄存器编号（字节的高 4 位） */
    __s16 off;        /* 有符号 16 位偏移：跳转距离或访存偏移 */
    __s32 imm;        /* 有符号 32 位立即数 */
};
```

四个字段恰好 1 + 1 + 2 + 4 = 8 字节。定长编码让内核能用最简单的循环逐条取指、逐条验证——依然是为 verifier 和 JIT 服务的取舍。

拿一条真实指令做手工解码——对同一对象文件再跑一次 `llvm-objdump -d`，第 19 条是 `trace_stat` 防御分支 `return 0`：

```text
      19:	b7 00 00 00 00 00 00 00	r0 = 0x0
```

逐字段拆开 `b7 00 00 00 00 00 00 00`：

- **opcode = `0xb7`**。按位分解：低 3 位 `0x07` 表示指令类别是 BPF_ALU64（64 位算术逻辑类）；第 3 位（`0x08`）为 0 表示操作数来自立即数（BPF_K）而非寄存器（BPF_X）；高 4 位 `0xb` 表示操作是 MOV（传送）。合起来：把 64 位立即数送入寄存器。
- **寄存器字节 = `0x00`**：低 4 位 dst_reg = 0，即 r0；高 4 位 src_reg = 0，MOV 立即数形式不用源寄存器。
- **off = `0x0000`**：MOV 不涉及访存或跳转，偏移无用，填 0。
- **imm = `0x00000000`**：立即数值 0。

结论：`r0 = 0`——64 位整体写入，不需要先清高位再置低位。这就是 `if (cpu >= MAX_CPUS) return 0;` 防御分支的返回值装载。

一个必须知道的例外：64 位立即数加载（ld_imm64）占用连续两个 8 字节槽位，共 16 字节。反汇编第 8 条 `r1 = 0x0 ll` 就是——它是 `bpf_map_lookup_elem(&sched_counts, &cpu)` 的第一个参数装载，立即数在对象文件里是 0，真正的 map 地址以 ELF 重定位记录（`R_BPF_64_64`，符号 `sched_counts`）记录在旁，libbpf 在加载阶段依据重定位把实际的 map fd 改写进这条指令。看到 `ll` 后缀的 16 字节行，就是遇到了宽指令。

### 2.3 常见指令类别速览

eBPF 指令按低 3 位的类别字段分成几大类。本节给每类两三句定位加一条编码示例，完整 ISA 表不在这里展开——需要查表时看附录 A（[`book/appendix/a-ebpf-instruction-set.md`](../appendix/a-ebpf-instruction-set.md)）。

**算术逻辑类（BPF_ALU / BPF_ALU64，类别码 0x04 / 0x07）**：加减乘除、位运算、移位与 MOV。ALU64 在 64 位寄存器上运算，ALU 是 32 位子集（结果按 UAPI 约定零扩展回 64 位）。示例是反汇编第 13 条，`p->switches++` 的自增：

```text
      13:	07 01 00 00 01 00 00 00	r1 += 0x1
```

opcode `0x07` = ALU64 + 立即数源 + ADD，dst = r1，imm = 1。

**跳转类（BPF_JMP / BPF_JMP32，类别码 0x05 / 0x06）**：条件与无条件跳转（jeq/jne/jgt/ja 等），以及 call（helper 或 BPF-to-BPF 调用）与 exit（程序退出）。跳转目标是相对当前指令的带符号偏移。示例是反汇编第 5 条——`if (cpu >= MAX_CPUS)` 的防御检查，编译器把「大于等于 64」改写成「大于 63」：

```text
       5:	25 00 0d 00 3f 00 00 00	if r0 > 0x3f goto +0xd <LBB0_3>
```

opcode `0x25` = JMP + 立即数源 + JGT，off = 13 表示向前跳 13 条，imm = 63 恰是 MAX_CPUS − 1。同类的还有 `85 00 00 00 08 00 00 00  call 0x8`（call，imm 8 是 helper 编号——`bpf_get_smp_processor_id`）和 `95 00 00 00 00 00 00 00  exit`。正是「跳转偏移必须落在程序内、程序必须可证明会终止」这两条，让 verifier 能排除无限循环（第 3 章）。

**内存加载类（BPF_LDX，类别码 0x01）**：从内存读入寄存器，地址 = src_reg + off，用宽度修饰区分 1/2/4/8 字节；verifier 要求地址可证明合法（上下文、map value 或栈）。两条真实示例：第 12 条读 map value 首字段（8 字节），第 15 条读 `ctx->next_pid`（4 字节，`0x38` 是 `next_pid` 在上下文结构中的偏移）：

```text
      12:	79 01 00 00 00 00 00 00	r1 = *(u64 *)(r0 + 0x0)
      15:	61 61 38 00 00 00 00 00	r1 = *(u32 *)(r6 + 0x38)
```

opcode `0x79` = LDX + MEM 模式 + 8 字节（DW）；`0x61` = LDX + MEM + 4 字节（W）。第 15 条就是 1.6 的 `ctx->next_pid` 落到字节码层面的样子。

**内存存储类（BPF_ST / BPF_STX，类别码 0x02 / 0x03）**：向内存写入——ST 写立即数，STX 写寄存器值。真实示例是反汇编第 14 条，把自增后的计数写回 map value；同一代码块附一条构造示例「向栈上 4 字节写立即数 0」：

```text
      14:	7b 10 00 00 00 00 00 00	*(u64 *)(r0 + 0x0) = r1
（构造示例）62 0a fc ff 00 00 00 00	*(u32 *)(r10 - 0x4) = 0x0
```

opcode `0x7b` = STX + MEM + DW；`0x62` = ST + MEM + W，off = `0xfffc` 即 −4。写栈（r10 相对寻址）正是编译器放置局部变量与溢出寄存器的方式——第 2 条 `*(u32 *)(r10 - 0x4) = r0` 就是一次典型的寄存器溢出。

**原子类（BPF_ATOMIC，opcode 形如 0xdb）**：STX 语义上的原子扩展——原子加、交换（xchg）、比较交换（cmpxchg），部分操作可带 BPF_FETCH 取回旧值。构造示例「对 r1 指向的 8 字节内存原子加 r2」编码为 `db 21 00 00 00 00 00 00`（imm = 0 即 BPF_ADD、不取回旧值）。注意 01-trace-stat 一条原子指令都没用——per-CPU 映射让每个 CPU 只写自己的槽位，天然无竞争（3.4）；原子指令属于跨 CPU 共享同一内存字的场景。

### 2.4 观察法：bpftool 的两个反汇编视图

背景问题：2.2/2.3 用 `llvm-objdump` 看的是 ELF 对象文件里「编译器交出去」的字节码。程序加载进内核之后，想确认「内核实际拿到、实际执行的是什么」，要用 bpftool 的两个视图：

- `bpftool prog dump xlated name trace_stat`：输出 **xlated（已翻译）视图**——verifier 处理之后的 eBPF 指令。读法与 2.3 相同（每行一条指令），但它反映验证后的最终形态：map 指针已解析、（若使用）BPF-to-BPF 调用已展开、不可达指令已移除。
- `bpftool prog dump jited name trace_stat`：输出 **jit 视图**——宿主 CPU（本机为 x86-64）实际机器码的反汇编。适合核对「真的 JIT 了吗」与性能层面的指令计数，不适合学习 eBPF 指令集本身。

两者差异一句话：xlated 是「eBPF 指令集层面、验证之后」的跨架构规范视图，jit 是「宿主 CPU 层面」的实际执行形态。它们都不等于源码，也不等于 2.2 里 `llvm-objdump` 看到的原始 ELF 字节码——链路是：ELF 字节码（编译产物）→ xlated（verifier 处理后）→ jit（宿主机码）。

**教师视角**：最常见的误区是把 `bpftool prog dump xlated` 的输出当成「源码对应的字节码」逐行核对——它不是，xlated 是 verifier 处理后的视图，指令可能已被改写或移除；想看编译器交出去的原始字节码，用 `llvm-objdump -d` 看对象文件（2.2 的做法，编译级即可，无需权限）。第二个误区是在无特权环境反复尝试 prog dump：dump 需要程序已加载，而非 root（`unprivileged_bpf_disabled=2`）连加载都做不到。

本机是非 root 环境，程序无法加载，prog dump 的对象根本不存在。以下标注为预期输出示例 / 运行级待验证（在满足 root 权限并加载 01-trace-stat 的环境中应看到类似形态）：

```text
（预期输出示例 / 运行级待验证——需先以 root 执行 sudo ./trace_stat 保持程序加载）
$ sudo bpftool prog dump xlated name trace_stat
   0: (bf) r6 = r1
   1: (85) call bpf_get_smp_processor_id#8
   ...
  20: (95) exit
```

与 2.2 的 ELF 反汇编对照读：指令序列大体相同（同一个程序），但这是内核里验证之后的形态，带 BTF 注解的环境会直接显示 helper 名与编号。动手路径：先做编译级验证（`make -C book/examples/chapter-02/01-trace-stat`，再 `llvm-objdump -d` 拆对象文件），加载级观察留到有权限的环境。

本节把「程序在内核里的存在形态」讲到了字节层。完整的指令编码表、全部 ALU/JMP/LD/ST 操作码与原子操作速查，见附录 A（[`book/appendix/a-ebpf-instruction-set.md`](../appendix/a-ebpf-instruction-set.md)）；下一节回到系统层面，回答另一个支柱问题——状态放在哪里、怎么选型。

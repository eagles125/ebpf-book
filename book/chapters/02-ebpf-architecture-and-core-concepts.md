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

机制上，eBPF 程序用 C 的受限子集编写：不能有无法证明会终止的循环，不能直接解引用任意内核指针，只能调用内核以固定编号暴露的辅助函数。源文件经 `clang -target bpf` 编译为字节码，loader 经 `bpf(BPF_PROG_LOAD)` 提交内核，验证器（verifier）静态检查通过后，随后由 JIT（Just-In-Time，即时编译）编译为本机指令（或由内核解释器执行）。第 1 章 `hello_exec.bpf.c` 的 `handle_execve` 与本章示例的 `trace_stat` 函数都是完整程序。

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

拿一条真实指令做手工解码——对同一对象文件再跑一次 `llvm-objdump -d`，第 19 条是 `trace_stat` 各 `return 0` 路径的汇合点（防御分支与 NULL 检查分支在此汇合）：

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

**跳转类（BPF_JMP / BPF_JMP32，类别码 0x05 / 0x06）**：条件与无条件跳转（jeq/jne/jgt/ja 等），以及 call（helper 或 BPF-to-BPF 调用）与 exit（程序退出）。跳转目标是相对**下一条指令**的带符号偏移（目标 = pc + 1 + off）。示例是反汇编第 5 条——`if (cpu >= MAX_CPUS)` 的防御检查，编译器把「大于等于 64」改写成「大于 63」：

```text
       5:	25 00 0d 00 3f 00 00 00	if r0 > 0x3f goto +0xd <LBB0_3>
```

opcode `0x25` = JMP + 立即数源 + JGT，off = 13 表示跳过其后 13 条指令（第 6–18 条），落在第 19 条——正是 2.2 手工解码的 `r0 = 0`；imm = 63 恰是 MAX_CPUS − 1。同类的还有 `85 00 00 00 08 00 00 00  call 0x8`（call，imm 8 是 helper 编号——`bpf_get_smp_processor_id`）和 `95 00 00 00 00 00 00 00  exit`。正是「跳转偏移必须落在程序内、程序必须可证明会终止」这两条，让 verifier 能排除无限循环（第 3 章）。

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

## 三、映射类型全景

1.2 建立了直觉：映射是内核分配、两个世界共同访问的数据对象。但「映射」是一个类型族——内核 UAPI 实际注册的类型远不止几种（本机 `/usr/include/linux/bpf.h` 的 `enum bpf_map_type` 实测 40 余个成员，随内核版本只增不减）。本节选出覆盖绝大多数观测与网络场景的八类，每类一张卡片、固定四要素：**语义 / 典型场景 / 容量与并发特征 / 选型理由**。选型不靠背 API，靠回答四个问题：key 长什么样、容量怎么定、谁在写、读回要付出什么代价。机制深挖不属于本节——LPM 与 sockmap 的机制与实战分别是第 17 章、第 19 章的专题，映射在内核里的实现结构是第 5 章的内容。

### 3.1 哈希映射（hash）：全等 key 的字典

**语义**：`BPF_MAP_TYPE_HASH`。key 是任意定长字节序列、按全等匹配查找，均摊 O(1)；`bpf_map_lookup_elem`/`bpf_map_update_elem`/`bpf_map_delete_elem` 增删改查齐备（更新可用 `BPF_NOEXIST`/`BPF_EXIST` 标志区分「仅新建」与「仅覆盖」），是八类里「字典语义」最完整的一类。

**典型场景**：key 集合可枚举的精确规则表——按五元组查规则、按 pid 查目标。第 17 章防火墙的 `acl_rules`（hash）即此类。

**容量与并发特征**：`max_entries` 创建时定死；表满后新 key 插入失败（`-E2BIG`），既有数据不受影响——容量超限是**显式错误**，调用方必须处理。并发上，多 CPU 更新同一 key 是先查后写的竞争（后写覆盖前写），结构完整性由内核内部锁保证（实现细节随内核版本演进，不展开）。

**选型理由**：需要「能删、能区分不存在、容量语义可控」的字典时最直接。满表失败正是它与 3.3 LRU 的分野——伏笔在此埋下。

### 3.2 数组映射（array）：下标即 key 的定长表

**语义**：`BPF_MAP_TYPE_ARRAY`。key 是 `__u32` 下标，创建时一次性预分配全部元素，value 定长；元素不可删除，合法下标的查找必命中——没有「key 不存在」状态。

**典型场景**：按下标统计与配置表。01-trace-stat 的 `sched_counts` 就是数组（per-CPU 变体，3.4 展开）。一个你可能已经用过的隐藏用户：libbpf 把 BPF 侧 `.rodata`/`.bss`/`.data` 全局数据落成内部数组映射——这是跨内核与用户态共享常量与全局变量的机制基础。

**容量与并发特征**：容量固定、无删除；越界下标查找返回 NULL 而非报错（helper 内部运行时定界，1.3 的 NULL 检查义务由此而来）。同下标的并发写仍是先查后写竞争。

**选型理由**：key 是自然下标时，数组比哈希更省内存、查找更快（无哈希计算与桶定位）；它与 per-CPU 的组合（3.4）是观测统计的黄金搭档。

### 3.3 LRU 哈希映射：满表自动让位的字典

**语义**：`BPF_MAP_TYPE_LRU_HASH`——LRU（Least Recently Used，最久未使用）。查找与更新语义同 3.1，差别全在满表行为：**内核静默淘汰最久未用的表项，新 key 插入仍然成功**，调用方得不到任何错误码。

**典型场景**：key 集合不可预知、规模必须设上界的观测——每进程计数（练习 2）、连接跟踪。第 17 章阶段 3 的 `conntrack` 表正是这个口径：满表不阻塞新流，淘汰是设计行为而非故障。

**容量与并发特征**：满表 = 淘汰最旧 + 插入成功；观测上表现为「旧表项消失」，只能靠读回或遍历发现。同 key 并发更新同 3.1；被淘汰项的选择由内核维护的近期使用序决定。

**选型理由**：当「插不进去」的代价高于「悄悄忘掉最旧的」时选它——观测数据宁丢旧不丢新。反向警告：把 LRU 当可靠存储用就是事故（你以为还在的数据被静默淘汰了）。本节口径与第 17 章阶段 3 一致，那里还演示了靠控制面统计观察满表行为的方法。

### 3.4 per-CPU 映射：每 CPU 一份副本的无锁写入

**语义**：per-CPU 变体（`BPF_MAP_TYPE_PERCPU_ARRAY`/`BPF_MAP_TYPE_PERCPU_HASH`）把 value 复制成每 CPU 一份：BPF 侧读写只落在当前 CPU 的副本上，数据面无锁；用户态读回拿到的是按 CPU 顺序排列的全部副本（单份大小 × CPU 数），**必须自行聚合**（求和或取对应 CPU 的槽位）。

**典型场景**：高频计数与每 CPU 状态。01-trace-stat 是教科书式用法——「key 选 CPU（`bpf_get_smp_processor_id`）+ per-CPU 槽位」双层去竞争：第一层让不同 CPU 写不同 key，第二层让写到同一 key 的各写各的槽位，整条数据面没有一次锁操作。读回求和的代码在 5.2 逐段走读。

**容量与并发特征**：内存 = 单份 value × CPU 数，value 大时膨胀可观（不适合大 value）；读回聚合是义务不是优化——不聚合读到的是原始副本数组。

**选型理由**：写竞争是计数类程序的头号精度杀手（两个 CPU 的后写互相覆盖，计数悄悄变少），per-CPU 用空间换掉整个竞争面；代价只有内存与读回聚合。

### 3.5 perf event array：历史事件通道

**语义**：`BPF_MAP_TYPE_PERF_EVENT_ARRAY`。key 恒为 CPU 下标，value 恒为该 CPU 的事件缓冲——它不是字典，而是「每 CPU 一条独立环形缓冲（ring buffer）」的事件通道。BPF 侧 `bpf_perf_event_output` 把记录推入当前 CPU 的缓冲；用户态要逐 CPU 挂接 perf 事件（`perf_event_open` 一类系统调用，老版 libbpf API 已封装）再逐 CPU 读取。

**典型场景**：2019 年之前的主流事件通道（BCC 时代标配），今天仍能在旧代码与既有工具里遇到；新项目没有理由再选它。

**容量与并发特征**：每 CPU 独立缓冲互不干扰，跨 CPU 记录乱序（各缓冲独立推进）；缓冲满后新事件被内核静默丢弃，**map 层面没有内置丢弃计数**，BPF 程序侧也无从感知——用户态侧仅能事后经 perf 丢失记录（PERF_RECORD_LOST，libbpf `perf_buffer` 的 lost 回调可拿到确切数字）得知丢了多少。

**选型理由**：新代码优先 ring buffer（3.6）：共享缓冲免逐 CPU 挂接、丢弃可判别。1.7 的三代事件通道演进线在此收口——练习 1 直接选 ring buffer 的原因就是它。

### 3.6 ring buffer：跨 CPU 共享的事件环形缓冲

**语义**：`BPF_MAP_TYPE_RINGBUF`。全部 CPU 共享一条大环形缓冲；没有 key/value 语义，只有「租借—提交」两段式：`bpf_ringbuf_reserve` 租一块记录内存，填好后 `bpf_ringbuf_submit` 发布（或 `bpf_ringbuf_discard` 放弃）；用户态经 `ring_buffer__new`/`ring_buffer__poll` 消费。

**典型场景**：逐事件推送。练习 1 的 `events`（16 MB 教学容量）即是。

**容量与并发特征**：两条硬性要求——`max_entries` 必须是 2 的幂且按页对齐；事件结构必须 8 字节对齐（练习 1 的 `struct sched_event` 显式补 pad 到 24 字节正是为此）。缓冲满则 reserve 返回 NULL：**失败在 BPF 侧可见**，程序可以当场感知并计数丢弃——与 3.5 的静默丢弃是决定性差异。

**选型理由**：事件流的默认答案——API 简单、单缓冲、丢弃可感知。与轮询 map 的取舍（延迟/开销/丢失语义）在 1.7 有一句话版本，常见问题与排错节给出完整对照表。

### 3.7 LPM trie：最长前缀匹配

**语义**：`BPF_MAP_TYPE_LPM_TRIE`——LPM（Longest Prefix Match，最长前缀匹配）。key 以前缀长度开头（如 `__u32 prefixlen` 后跟地址数据），查找返回**匹配到的最长网段规则**而非全等命中；底层是前缀树（trie）。

**典型场景**：CIDR 网段规则——防火墙黑/白名单、按网段的路由策略。第 17 章阶段 2 的 `acl_lpm` 即此类。

**容量与并发特征**：创建时必须设 `BPF_F_NO_PREALLOC` 标志——前缀树无法像 hash/array 那样按 `max_entries` 一次性预分配内存，条目按需分配；副作用是名义容量可以开得很大，而实际内存跟随条目数增长。同前缀的更新竞争同 3.1。

**选型理由**：需求里出现「网段」「前缀」「越具体越优先」时是唯一正解。前缀树的插入/查找/回溯机制是第 17 章阶段 2 的正题，本节点到为止。

### 3.8 sockmap：socket 域的映射与消息重定向

**语义**：socket 域映射（`BPF_MAP_TYPE_SOCKMAP`/`BPF_MAP_TYPE_SOCKHASH`）：key 到 socket 的映射，value 不是数据而是 socket 本身；配套辅助函数把 socket 间消息直接重定向，绕开完整的协议栈路径。

**典型场景**：代理/网关类负载中「从 A socket 收、往 B socket 发」的转发加速——普通路径要两次完整的 TCP/IP 栈穿越，重定向把这段路径大幅缩短。

**容量与并发特征**：容量即可挂接的 socket 数上限；它管理的是活连接，socket 关闭时表项随之失效——连接的生命周期管理是它的主要复杂度来源。

**选型理由**：何时想到它——性能剖析显示转发路径上的协议栈开销是瓶颈，且流量模式是 socket 间转发时。它是什么、何时想到，本节到此为止；机制与实战是第 19 章的专题。

### 3.9 选型决策表与 bpftool 观察

八类收进一张可操作的表——从需求出发，先答「key/容量/写者/读回」四问，再落类型：

| 需求 | 推荐类型 | 理由 |
|---|---|---|
| 精确查找：key 全等、集合可枚举 | hash（3.1） | 全等 O(1)；满表显式失败，语义可预期 |
| 固定下标统计 / 配置下发 | array（3.2） | 下标即 key、预分配、合法下标必命中 |
| 高频计数：多 CPU 写竞争敏感 | per-CPU array / per-CPU hash（3.4） | 每 CPU 副本无锁写，读回聚合 |
| 容量受限字典：key 不可预知 | LRU hash（3.3） | 满表静默淘汰最久未用、插入不失败 |
| 事件流：逐事件推用户态 | ring buffer（3.6；历史方案 perf event array 见 3.5） | 共享缓冲、API 简单、丢弃可判别 |
| 前缀匹配：CIDR 网段规则 | LPM trie（3.7） | 最长前缀语义；必须 `BPF_F_NO_PREALLOC` |
| socket 间转发加速 | sockmap / sockhash（3.8） | 消息重定向绕开协议栈 |

正文 5.2 的 map 选型四问与排错节的选型误用表都以这张表为参照。

观察法：`bpftool map show` 列出系统当前全部映射（类型、id、名、`max_entries`）；`bpftool map dump name sched_counts` 读回内容——per-CPU 映射按 CPU 逐份列出，3.4 的聚合语义在输出里肉眼可见。两者都要求对象已加载且需要特权：本机非 root 连 bpffs 目录都不可列（`ls /sys/fs/bpf` 实测返回 EACCES，本地验证），而 bpftool 的 map 子命令面可用 `bpftool map help` 就地查看（本地验证）——pin/unpin、push/pop、enqueue/dequeue 等专用子命令的存在，本身就对应着特定 map 类型族。预期输出示例 / 运行级待验证：

```text
（预期输出示例 / 运行级待验证——需 root 加载 01-trace-stat 后观察）
$ sudo bpftool map show
123: percpu_array  name sched_counts  flags 0x0
    key 4B  value 16B  max_entries 64  memlock 16.405KiB  btf_id 62
$ sudo bpftool map dump name sched_counts
key: 00 00 00 00  value:
    cpu 0: switches=1234  last_next_pid=1823
    cpu 1: switches=987   last_next_pid=910
    ...
```

**教师视角**：选型误用是 eBPF 程序最常见的「设计级」事故源，而且几乎都在运行期才暴露。三类高发：用普通 hash 做高频计数（各 CPU 竞争同一 key，计数悄悄偏少）；用 array 当字典（「key 不存在」的 NULL 被当成功路径处理）；把 LRU 满表淘汰误解为「插入失败」，在错误的分支里找 bug。共同点：编译期零警告、运行期数据悄悄不对。排查路径见常见问题与排错节。

**学生动手**：对 3.1–3.8 逐类写一句「若用错类型会怎样」。两个示例打底：把 01-trace-stat 的 per-CPU array 换成普通 hash 会怎样？（各 CPU 竞争同一 key，`switches` 低于真实值）；把练习 2 的 LRU hash 换成普通 hash 会怎样？（进程数超过 4096 后新进程入不了榜——若调用方不检查返回值，现象更加隐蔽）。写完八条，与排错节的选型误用表对照。

检查点（先自答，再看提示）：

1. 同样是「计数」，为什么 01-trace-stat 用 per-CPU array（key 是 CPU 下标），练习 2 用 LRU hash（key 是 pid）？（提示：先问 key 集合是否可预知、每 CPU 的写者是谁——CPU 下标天然可预知且写者唯一；pid 集合不可预知，且每个 CPU 都可能写同一个 pid。）
2. hash 与 LRU hash 都配了 `max_entries`，向超出容量的新 key 插入，行为分别是什么？（提示：`-E2BIG` 显式失败 vs 静默淘汰最久未用项后成功——3.1 与 3.3 的分野。）
3. ring buffer 满与 perf event array 缓冲满，BPF 程序分别如何得知？（提示：前者 reserve 返回 NULL 可判别并计数，后者 `bpf_perf_event_output` 静默丢弃、程序无从感知。）

## 四、bpf() 系统调用与内核实现分析

三节回答了「状态放在哪种结构里」，本节回答「这些对象是谁、如何被创建、如何被持有与释放」。观察对象是 `kernel/bpf/syscall.c`——`bpf()` 系统调用全部命令的入口。本节是纯讲解加内核路径引用，不需要内核源码级调试环境；凡随版本演进的函数名一律用保守措辞，以具体内核版本源码为准（本机内核 7.0.0-31，讲解兼容 5.10+ 口径）。

### 4.1 `bpf(2)` 入口与命令集概览

背景问题：一个系统调用如何承载「建映射、载程序、pin 对象、建链接」这么多操作？答案是命令字加参数联合体。

- **命令集**：`include/uapi/linux/bpf.h` 的 `enum bpf_cmd`。本机头文件实测 37 个成员（含一个别名）：从 `BPF_MAP_CREATE`、`BPF_MAP_LOOKUP_ELEM`、`BPF_MAP_UPDATE_ELEM`、`BPF_PROG_LOAD`，到 `BPF_OBJ_PIN`/`BPF_OBJ_GET`、`BPF_PROG_ATTACH`，再到后来加入的 `BPF_LINK_CREATE`/`BPF_LINK_DETACH`。命令集随内核版本只增不减（UAPI 稳定性纪律），以头文件为准。本节聚焦四个：`BPF_MAP_CREATE`（4.2）、`BPF_PROG_LOAD`（4.3）、`BPF_OBJ_PIN`/`BPF_OBJ_GET`（4.5）。
- **参数复用**：`union bpf_attr` 按命令复用同一块内存——map 命令用 `map_type`/`key_size`/`value_size`/`max_entries` 字段，程序加载用 `prog_type`/指令缓冲等字段。libbpf 在 `<bpf/bpf.h>` 为每个命令提供同名封装（如 `bpf_map_create`），示例代码从不手填联合体。
- **入口分发**：`kernel/bpf/syscall.c` 是全部命令的入口，按命令字分发到各自处理路径（分发函数名随版本演进，以具体内核版本源码为准）。子系统源码地图（`syscall.c`/`verifier.c`/各映射实现文件的关系）是第 5 章的正题。

常见失败点：attr 参数按 libbpf 版本与内核版本匹配——内核向后兼容旧尺寸的 attr，反过来旧内核遇到不认识的新字段会拒绝。

### 4.2 BPF_MAP_CREATE：映射对象的诞生路径

以 01-trace-stat 的 `sched_counts` 为例走一遍创建路径（`kernel/bpf/syscall.c`，函数名以具体内核版本源码为准）：

1. **属性校验**：用户态传入的属性（`map_type=BPF_MAP_TYPE_PERCPU_ARRAY`、`key_size=4`、`value_size=16`、`max_entries=64`）逐一检查——类型是否已注册、大小是否合法；per-CPU 变体的 `value_size` 是单份大小，实际内存 = value × CPU 数（3.4）。
2. **按类型找操作集**：每种映射类型在内核里以一段 `struct bpf_map_ops` 操作表注册；创建路径按 `map_type` 查表，此后该对象的分配、释放、读写全部经这张操作表转发。本机 BTF 实测 `struct bpf_map` 的第一个字段正是 `const struct bpf_map_ops *ops`。
3. **分配对象**：调用操作表的分配函数构造 `struct bpf_map`（本机 BTF 实测含 `max_entries`、`name` 等字段），填入类型、容量与关键约束。
4. **绑定 fd**：对象与一个匿名 inode 的 `struct file` 关联，返回新 fd 给调用方（`bpf_map_new_fd` 一类函数）。

fd 所有权语义：**fd 是进程视角的句柄，不是对象本身**。进程 fd 表的一项指向内核的 `struct file`，`struct file` 再关联 `bpf_map` 对象。对象由内核持有，fd 只是一层「引用」；`dup` 复制、fork 继承都是再多拿一个句柄，不产生新对象。libbpf 把这层关系包进 skeleton：`skel->maps.sched_counts` 是用户态对象，`bpf_map__fd()` 取到的才是内核 fd（loader 源码里 `int fd = bpf_map__fd(skel->maps.sched_counts);`）。

### 4.3 BPF_PROG_LOAD：程序的加载路径

1.8 的六步总览里，「加载」与「验证」两步都在这条路径上。四步走完（`kernel/bpf/syscall.c`，函数名保守措辞）：

1. **license 检查**：程序声明的 license（`trace_stat.bpf.c` 末行的 `char LICENSE[] SEC("license") = "GPL"`）在此参与检查——内核把部分辅助函数标记为仅 GPL 兼容 license 可用，license 决定程序能看到的辅助函数面（1.3「受控」的延伸）。
2. **verifier 静态验证**：逐指令模拟执行，证明内存安全、可终止、权限合法；拒绝时返回 `-EACCES` 并给出日志。这是第 3 章整章的主题，此处只需知道它发生在加载路径的哪一步。
3. **JIT 编译**：验证通过后，eBPF 指令被编译为宿主架构机器码（本机 x86-64）；JIT 实现按架构分布在 `arch/*/net/` 一类路径下，随内核版本演进（第 5–7 章按架构展开）。
4. **组装与 fd**：构造 `struct bpf_prog`（含验证与 JIT 产物的程序对象），绑定 fd 返回——与 map 同构的对象诞生方式。

回指：01-trace-stat 的 `trace_stat_bpf__open_and_load()` 一次完成上述两条命令——libbpf 先以 `BPF_MAP_CREATE` 创建全部映射（2.2 提到的 ld_imm64 重定位此刻才拿到真实 map fd 并改写进指令），再以 `BPF_PROG_LOAD` 提交程序；open 阶段只解析 ELF，真正的内核对象创建全部发生在 load 阶段。

### 4.4 引用计数与生命周期钩子

背景问题：谁决定对象何时消失？「关闭 fd = 销毁对象」这个直觉不对——fd 只是引用之一。

机制：`bpf_map`/`bpf_prog` 内置原子引用计数（本机 BTF 实测 `struct bpf_map` 带 `atomic64_t refcnt` 字段；程序对象同样维护原子计数，字段名随版本演进）。引用的增减各有明确的触发点：

- **增加**：对象创建时初值为 1；此后每多一类持有者就多一份——进程 fd（经 `struct file`）、使用该 map 的程序（加载验证时内核记录 prog→map 引用并取引用）、bpffs pin 节点（4.5）、挂载产物。5.10+ 上 tracepoint 挂载的产物是 bpf_link：link 持有 prog 的引用，link 自身由 link fd 持有。
- **减少**：fd 关闭（显式 close，或进程退出时内核统一关闭全部 fd）、link 销毁（其 fd 关闭或显式 detach），最终都落到引用释放函数 `bpf_map_put`/`bpf_prog_put` 一类命名上（随版本演进，以具体内核版本源码为准）。
- **归零回收**：最后一个引用释放时触发对象回收——map 走其操作表的释放函数，prog 释放 JIT 产物与它对 map 的引用；真正归还内存在内核安全点完成（延迟机制随版本演进，保守措辞）。

01-trace-stat 的 Ctrl-C 在这条链上是：进程退出 → prog fd、map fd、link fd 全部由内核关闭 → prog 引用归零 → prog 释放对 `sched_counts` 的引用 → map 引用归零 → 全部回收。「Ctrl-C 退出即卸载」的机制本质是**引用计数归零**，信号处理只是让 loader 走了正常的 destroy 路径；就算进程被 kill -9，内核也会关闭其全部 fd——这是 eBPF 对象「崩溃安全」的由来。

**教师视角**：两个最常见的误区。其一，「卸载是一个显式动作」——机制上没有卸载命令，只有引用计数归零；close、detach、unpin 都只是在减引用。其二，「Ctrl-C 之后对象可能还残留在内核里跑」——未 pin 的对象不会；会残留的是 pin 过的（4.5）或被其他进程持有 fd 的对象。检查对象是否残留的实用命令就是 3.9 的 `bpftool map show`/`prog show`。

### 4.5 pin 与 bpffs：跨越进程生命周期的引用

背景问题：如果控制面与数据面的生命周期分离——第 17 章阶段 5 就是如此：Go 控制面（`fwctl attach`）自己加载、pin 并挂载后退出——第一个进程退出后对象怎么活下来？

机制：`BPF_OBJ_PIN` 把对象「钉」到 bpffs（BPF 伪文件系统，习惯挂载在 `/sys/fs/bpf`）的某个路径上——内核在该路径创建文件节点，并让**文件节点本身持有对象的一个活引用**。从此对象存活不再依赖任何进程的 fd。`BPF_OBJ_GET` 按路径重新打开，返回一个新 fd——这是另一个进程接管对象的入口。`bpftool map pin`/`bpftool prog pin`（`bpftool map help` 可见的子命令，本地验证）与 libbpf 的 `bpf_obj_pin`/`bpf_obj_get` 是两条常用封装。

本机实测：bpffs 已挂载，但非 root 连列目录都被拒绝（`ls /sys/fs/bpf` 实测返回 EACCES，本地验证）——pin/unpin 全部是特权操作。

两种形态对照：

- **不 pin（01-trace-stat）**：对象引用全部来自本进程 fd，Ctrl-C 即引用归零、对象回收（4.4 链条收口）。适合「控制面即数据面宿主」的单进程工具。
- **pin（第 17 章阶段 5）**：Go 控制面（`fwctl attach`）把 map/prog/link 钉到 `/sys/fs/bpf/fw17` 后退出；后续 `fwctl` 子命令等进程用 `BPF_OBJ_GET` 按路径取回 fd 接管（cilium/ebpf 侧是 `LoadPinnedMap`/`LoadPinnedLink` 一类接口）。对象存活条件从「进程活着」变成「有人负责解除 pin」。代价：忘记 unpin 的对象会永久残留，运维上要把清理当作控制面的一部分。

可视化节的对象关系图会把「进程 fd 表 / 内核对象 / bpffs 节点」三方引用画在一张图上；此处先把结论钉住：**对象生命周期由引用计数决定，pin 只是给它加了一个不受进程约束的引用**。

**学生动手**：在纸上画 trace_stat 进程退出前后的对象引用变化。退出前列出全部引用边（进程 fd 表 → prog/map/link 三个 `struct file`；link → prog；prog → map）；退出后把随进程消失的边擦掉，确认没有任何边指向内核对象（全部回收）。再加一问：若 loader 退出前把 `sched_counts` pin 到 `/sys/fs/bpf/ch02_demo`，退出后哪条边存活？（答案：bpffs 节点 → map 一条；map 因此存活，prog 与 link 未 pin，仍随进程回收。）

检查点（先自答，再看提示）：

1. 为什么 close(fd) 不一定立刻释放对象？（提示：fd 只是引用之一——使用该 map 的程序、pin 节点都可能是仍然活着的引用；归零才回收。）
2. Ctrl-C 退出 01-trace-stat 后，`bpftool map show` 里为什么找不到 `sched_counts`？如果 loader 退出前 pin 过它，又会看到什么？（提示：引用归零回收 vs bpffs 节点持有活引用——对象仍在，但需要用 `BPF_OBJ_GET` 按路径取回 fd 才能访问。）
3. 第 17 章阶段 5 的 Go 控制面为什么要 `BPF_OBJ_GET` 取回 map，而不是自己重新 `BPF_MAP_CREATE` 一个同参数的？（提示：新建的是另一个对象——数据面程序引用的还是旧 map，重建只会得到一个永远读不到统计的空壳。）

## 五、实战：第一个追踪统计示例

前四节把八要素拆成了清单：程序与挂载（一节）、字节码形态（二节）、映射选型（三节）、对象创建与生命周期（四节）。本节把它们装进一个能编译、能运行的完整闭环——`book/examples/chapter-02/01-trace-stat/` 的第一个映射（map）交互示例：BPF 侧 47 行、loader 侧 79 行，每个要素不多不少落在一处。走读分两步：5.1 按五层把环境备齐（层序与第 17 章 1.1–1.5 一致，场景收窄为单机 tracepoint），5.2 逐段读代码。本节与示例 README 同口径：README 供快速上手，正文供教学走读。

### 5.1 五层环境（紧凑版）

#### 第一层：依赖环境

**内核版本与 BTF**：示例要求 Linux 5.10+ 且开启 `CONFIG_DEBUG_INFO_BTF`——用到的 per-CPU 数组映射、`bpf_get_smp_processor_id` 与 skeleton 工作流在 5.10 上均已可用；每个示例 `make` 的第一步是从 `/sys/kernel/btf/vmlinux` 生成 `vmlinux.h`，BTF 缺失则编译停在第一步（自建带 BTF 内核的路径见附录 C）。本机内核 7.0，在基线之上。

**权限矩阵**。加载 BPF 程序（`bpf(BPF_PROG_LOAD)`，4.3）需要特权；挂载 tracepoint 是纯观测操作，不涉及网络控制，不需要 `CAP_NET_ADMIN`（第 17 章 TC 挂载需要——两章权限口径的可见差异）：

| 环境档位 | 可编译 | 可加载/挂载 | 可观察（bpftool prog/map） |
|---|---|---|---|
| root | 可 | 可 | 可 |
| 非 root + `CAP_BPF`+`CAP_PERFMON`（+`CAP_SYS_ADMIN`） | 可 | 可 | 可 |
| 非 root 且无 `CAP_BPF` | 可 | 不可 | 不可 |

组合要求随内核版本略有差异，教学上以 root 或第二档为稳妥口径。本机真实档位是第三行：非 root、`unprivileged_bpf_disabled=2`——编译全可、加载不可，这正是 2.4 与 3.9 的观察输出都标注「运行级待验证」的原因。

**工具清单与检查方法**：清单不必背——运行 `bash book/examples/chapter-02/00-env/check-env.sh`（无 root 也可），脚本按「内核与 BTF → 工具链（编译级）→ 权限分级（加载级/运行级）」三段输出并给出结论。本机前两段全部 OK，第三段如下（本地验证）：

```text
== 权限分级（加载级/运行级） ==
OK    具备 CAP_BPF：可加载程序，挂载 tracepoint 还需 CAP_PERFMON/CAP_SYS_ADMIN
WARN  tracefs 未见 sched/sched_switch（运行级挂载会失败，编译不受影响）

结论：编译级依赖齐备；加载级/运行级能力见上方权限分级输出。
```

两行都值得停下来看——它们都不「属实」，且不属实的方式正是两个现成教学点：

- **那行 OK 为何不算数**：脚本以 capsh 的 **bounding set** 粗检权限，本机 bounding set 恰含 `cap_bpf`；但 bounding set 不等于生效能力——`capsh --has-p=cap_bpf` 实测 not permitted（非 root、`unprivileged_bpf_disabled=2`），真实档位是上表第三行。判断自身权限要以 permitted/effective 集为准，第 17 章 1.1 有同款偏差。
- **那行 WARN 为何虚惊**：tracefs 根目录 0700 仅 root 可遍历（`ls -ld /sys/kernel/tracing` 实测 `drwx------ root root`，本地验证），非 root 连 `events/sched/sched_switch` 的存在性都探测不到，`-d` 测试自然失败。sched_switch 是内核调度器的内置 tracepoint，root 视角该事件存在——这行 WARN 反映的是受限身份的观测边界，不是内核缺失。

#### 第二层：编译环境

四件工具一条链：clang/LLVM 18.x 以 `-target bpf` 把 BPF C 编成字节码；libbpf-dev 提供两侧的头文件与链接库——BPF 侧 `<bpf/bpf_helpers.h>`（`SEC` 与 map 定义宏）、loader 侧 `<bpf/libbpf.h>`/`<bpf/bpf.h>` 与 `-lbpf`；bpftool 7.x 负责 BTF dump 与 skeleton 生成；gcc + make 构建用户态 loader。Debian/Ubuntu 一行安装：

```bash
sudo apt install clang llvm gcc bpftool libbpf-dev make
```

版本自检（本机实测输出节选，本地验证）：

```text
$ clang --version | head -1
Ubuntu clang version 18.1.3 (1ubuntu1)
$ bpftool version
bpftool v7.7.0
using libbpf v1.7
$ dpkg -s libbpf-dev | grep ^Version
Version: 1:1.3.0-2build2
$ make --version | head -1
GNU Make 4.3
```

一句提醒：`bpftool version` 报出的 libbpf（v1.7）是 bpftool 自身随带的运行时；编译 loader 的头文件与链接库来自 libbpf-dev 1.3.0——两者不必同版，口径以 libbpf-dev 为准。

#### 第三层：部署环境

无拓扑可搭：不建 veth、不建 netns、不动网卡。唯一前置是「目标事件存在」——`sched/sched_switch` 是内核调度器的内置 tracepoint，任何内核都有（第一层 WARN 行的回扣：root 视角可见即可）。部署动作只剩一条：把示例目录放进满足前两层的主机。

安全警告（本章示例统一措辞）：

> tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

与第 17 章「勿挂物理网卡」红线的差异：XDP/TC 程序有丢包、改包能力，挂错接口可能立即失联；本章 tracepoint 没有网络接口可选、返回值没有动作语义（1.4），「挂错地方」这个风险面根本不存在。风险收窄为两点：sched_switch 高频触发（繁忙机器每秒上万次），程序路径要短——本示例只做两次 map 访问，符合口径；以及引文里的通用禁忌。风险面变小，不等于零。

#### 第四层：运行环境

启动与停止共两条：`sudo ./trace_stat` 前台运行，Ctrl-C 退出即卸载。没有守护进程、没有配置文件——**最简控制面就是 loader 本身**：它不接收规则下发（没有规则表可写），也不 pin 对象（4.5 两种形态里「不 pin」的一侧），对象生命周期完全跟随进程（4.4）。

运行期间的观察命令（需对象已加载且特权，运行级待验证）：`sudo bpftool prog show` 确认程序的注册形态（类型 tracepoint、名 trace_stat）；`sudo bpftool map dump name sched_counts` 读回映射原始内容——输出示例见 5.2 学生动手，逐 CPU 列出槽位的形态在 3.9 已给样例。

#### 第五层：运行效果

加载成功后，loader 每秒打印一轮：

```text
（预期输出示例 / 运行级待验证）
已挂载 tracepoint/sched/sched_switch，每秒打印，Ctrl-C 退出。
--- 各 CPU 调度次数 ---
cpu0  switches=1234       last_next_pid=1823
cpu1  switches=987        last_next_pid=910
total=2221
```

三个读数点：各 CPU 行的 switches 持续增长（无需专门负载，系统空闲也在触发 sched_switch）；total 是全部 CPU 计数之和；last_next_pid 随每次切换变化（读的是该 CPU 最近被调度切入的进程 pid）。只打印 switches 非零的 CPU 行，全零 CPU 不出现。

**验证状态约定表**——本章全部示例与正文统一执行（前两行为全书契约原文，第三行为「预期输出」的标注规则），供 `scripts/verify-build.sh` 的编译级汇总对账：

| 标注 | 约定原文 | 适用范围 |
|---|---|---|
| 编译验证 | 已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过 | 01-trace-stat 与两个练习目录的构建 |
| 待运行验证 | 编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例 | 全部需要加载权限的操作 |
| 预期输出示例 / 运行级待验证 | 涉及加载/运行的效果一律写「预期输出示例」并标注「运行级待验证」，不得写成「已验证通过」 | 5.1/5.2 与练习中全部输出块 |

### 5.2 代码走读

**教师视角**：为什么选 sched_switch + per-CPU 数组？这是八要素全部可见的最小闭环：sched_switch 每次进程切换都触发，系统空闲也有流量，无需压测；上下文带结构化字段可读（1.6）；「每 CPU 统计调度次数」天然指向 per-CPU 数组（3.4）；loader 的加载→挂载→轮询→退出恰好走完 1.8 的六步生命周期。最容易卡住的两处先亮出来，走读中各回收一次：「为什么读回要对 per-CPU 槽位求和？」「verifier 为什么直接放行 `ctx->next_pid`？」读码顺序与执行顺序一致：先内核态，后用户态。

**核心代码原理讲解**。第一段（`book/examples/chapter-02/01-trace-stat/trace_stat.bpf.c` 第 1–17 行）——文件头、include 与 value 类型：

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
```

两条 include 是 CO-RE 工作流的依赖：`vmlinux.h` 由 bpftool 从本机 BTF 生成、替代内核头（1.6），`bpf_helpers.h` 提供 `SEC` 与 map 定义宏；`MAX_CPUS` 是两侧必须一致的教学上限契约。`struct cpu_stat` 两个字段各钉一个教学点：`switches` 是纯计数（map 本职），`last_next_pid` 记录本 CPU 最近被切入的进程 pid、来自 ctx 字段读取——一个 value 同时演示 map 交互与上下文读取。

第二段（同文件第 19–28 行）——映射定义：

```c
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
```

map 选型四问（3.9 决策表实操版）：**为什么 per-CPU**——繁忙机器上每秒上万次触发，多 CPU 写同一表项的先查后写竞争让计数悄悄偏少（3.4），per-CPU 副本把竞争面整个拿掉；**为什么 array 而非 hash**——key 可预知（CPU 下标）、无需删表，array 预分配、下标直达，不为哈希计算付费（3.1/3.2）；**为什么 key 是 CPU 下标**——统计口径就是每 CPU 计数，key 与写者一一对应，与 per-CPU 槽位构成双层去竞争；**容量为何 64**——教学上限（本机 16 CPU），超限的机器两侧同步调大。注释里「运行时定界」与显式检查的分工见下段。

第三段（同文件第 30–47 行）——程序本体与 license：

```c
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

`SEC(...)` 一个字符串定两件事：前缀 `tracepoint/` 归入 tracepoint 类型（1.4），`sched/sched_switch` 给出挂载目标（1.5），attach 因此无需参数；末行 license 声明是 4.3 加载路径的第一道检查。`bpf_get_smp_processor_id()` 选型理由在注释里——获取当前 CPU 的稳定途径，无需解析 ctx（sched_switch 上下文里没有 CPU 字段，1.6 字段表）。两道检查分工不同：数组 map 对越界 key 在 helper 内部返回 NULL，是**运行时定界**（3.2，verifier 借此证明 `p` 的使用安全）；`max_entries` 恰为 `MAX_CPUS`，显式检查通过后 NULL 理论上不再发生，留下的 `if (!p)` 是为把「下标异常」与「表项异常」分开诊断的防御式写法，非 verifier 硬性要求。再回收第二个卡点：`ctx->next_pid` 直接解引用内核内存而 verifier 放行，因为 ctx 不是任意内核指针——程序类型登记时已向 verifier 申报上下文结构与长度（1.4），对固定偏移的字段读可静态证明不越界；任意内核指针必须走 probe 系 helper，分界就在「有没有登记」（证明过程是第 3 章正题）。

顺带讲清注释点名、本示例却没用的 `bpf_probe_read_str`——知道边界，才知道为什么可以不用。`prev_comm`/`next_comm` 是 16 字节定长数组（本机 BTF 实测），长度编译期已知，直接读不越界；`__data_loc` 变长字符串（路径名、参数一类）长度运行时才知道，按猜测长度直读可能越过字段边界。变长读取交给它：把内核内存中 NUL 结尾的字符串复制进安全缓冲，最多 size 字节并保证结果 NUL 终止，返回复制长度（含终止符）或负错误码；个别旧版本内核在边界与失败行为上有差异，跨版本以所用内核的 UAPI helper 注释为准（本书口径 5.10+）。口诀：编译期已知长度直接读；运行时才知道长度，找 probe 系 helper。

再读用户态 loader（`book/examples/chapter-02/01-trace-stat/trace_stat.c`）。第一段（第 27–53 行；第 1–26 行的 license 头、include、两侧契约定义与退出信号处理见源文件）——加载与挂载：

```c
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
```

第 1–26 行里两件事先记住：`MAX_CPUS` 与 `struct cpu_stat` 在 loader 侧逐字段重新定义，不一致则读回数据被错误解释（1.2 失败点）；`exiting` 是 `volatile sig_atomic_t`，信号处理只置位、把「停止」延后到主循环检查点。main 先对账 `libbpf_num_possible_cpus()`，超限即退出——读回缓冲的大小契约依赖它。`open_and_load` 一行完成 4.3 的两条命令：open 解析 ELF，load 创建映射（`BPF_MAP_CREATE`）、提交程序过 verifier 与 JIT（`BPF_PROG_LOAD`），2.2 的 ld_imm64 重定位此刻拿到真实 map fd；注释里 `-EPERM`/`-EACCES` 分别指向权限档位（5.1 第一层）与 verifier 日志（第 3 章、排错节）。`attach` 无参调用——挂载点从 SEC 名完整推断（1.5）；产物 bpf_link 持有 prog 引用、进程持有 link fd（4.4 引用链起点）。

第二段（同文件第 55–79 行）——轮询、读回求和与退出：

```c
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

`while (!exiting)` 每秒一轮，是**事件通道的落点**（1.7 第二代「轮询 map」）：没有推送与唤醒，新鲜度等于轮询周期，语义是「最新状态快照」；好处是数据面零开销，开销全在读回侧。

循环体回收**第一个卡点**：per-CPU 映射的 `bpf_map_lookup_elem` 一次返回全部 CPU 槽位的副本数组（单份 × CPU 数、按 CPU 序），`buf` 因此开成 `buf[MAX_CPUS]`。求和为什么恰好等于该 CPU 的真实计数？双层去竞争叠加：key 让只有 CPU c 写 key c，per-CPU 槽位让写只落在写者自己的槽位——key c 的全部槽位只有下标 c 一份非零，零槽位不贡献，sum 即真实计数；`buf[cpu].last_next_pid` 直接取写者槽位，`if (sum)` 只是不打印全零行。3.4 的「聚合是义务」在本例退化成一次线性扫描。

收尾一行是**生命周期的落点**（4.4 的代码形态）：`destroy` 关闭 prog/map/link 全部 fd，引用逐级归零——link 销毁、prog 回收、prog 对 map 的引用释放、map 回收；即使 kill -9，内核统一关闭 fd 的兜底依然成立。全图见可视化节，检查点第 3 问的答案也在这里。

**学生动手**（编译级无需 root；运行级需 root 或 5.1 权限表第二档）：

- [ ] `make -C book/examples/chapter-02/01-trace-stat`：本地验证通过；vmlinux.h 自带的数条 `-Wmissing-declarations` 告警是 bpftool 导出前向声明的已知现象，与示例源码无关。
- [ ] `sudo ./trace_stat`，对照 5.1 第五层的三个读数点观察输出（运行级待验证）。
- [ ] 另开终端观察内核对象（预期输出示例 / 运行级待验证）：

```text
$ sudo bpftool prog show
124: tracepoint  name trace_stat  tag 0f1e2d3c4b5a6978  gpl
	loaded_at 2026-10-20T21:30:00+0800  uid 0
	xlated 168B  jited 132B  memlock 4096B  btf_id 63
$ sudo bpftool map dump name sched_counts
key: 00 00 00 00  value:
    cpu 0: switches=1234  last_next_pid=1823
    cpu 1: switches=0     last_next_pid=0
    cpu 2: switches=0     last_next_pid=0
    ...
```

map dump 逐 CPU 列出 key 0 的槽位：**只有 cpu 0 非零**——loader 求和输出里没有 cpu1/cpu2，正因为它们对 key 0 全零（检查点第 2 问的现场版）。

- [ ] Ctrl-C 退出 loader，再跑 `sudo bpftool prog show`：trace_stat 与 sched_counts 一并消失（运行级待验证）——引用归零回收，不留残骸。

检查点（先自答，再看提示）：

1. key 已经是 CPU 下标，为什么还要用 per-CPU 变体？（提示：两层去竞争各自挡住什么——key=CPU 让不同 CPU 写不同 key；per-CPU 槽位让可能写到同一 key 的写者各写各的。本例若缺一层，会先在哪里出问题？）
2. loader 读回的 `buf` 里为什么只有下标 `cpu` 的槽位非零？（提示：谁会写 key c？写落在哪个槽位？）
3. Ctrl-C 之后程序去了哪里？（提示：link 随进程销毁、引用计数归零——衔接 4.4；若 loader 退出前把 map pin 了，答案会不同。）

47 行内核态加 79 行用户态，八要素全部落到行上。「每条指令长什么样」2.2/2.3 已拆开，第 3 章将重读同一程序的另一端——verifier 如何证明它安全；注释里点名的失败模式，排错节收成对照表。

**独立示例**：[`book/examples/chapter-02/01-trace-stat/`](../examples/chapter-02/01-trace-stat/)——BPF 侧、loader、Makefile 与 README 全套，快速上手指引见其 README。

## 可视化：eBPF 程序生命周期与数据通路

本节把五节拆开的八要素拼回两张全链路图：程序、映射、挂载点、loader 与用户态控制面在一次完整生命周期里各站在哪、谁与谁说话。图 1 沿时间轴展开 1.8 六步，图 2 沿引用回答「对象归谁持有」。

**图 1：生命周期与数据通路全链路**（每步标注对应小节）：

```text
   用户态 loader（trace_stat.c）            内核（bpf() 入口 kernel/bpf/syscall.c，4.1）
──────────────────────────      ─────────────────────────────────────────────
open_and_load()
 ├─ bpf(BPF_MAP_CREATE) ────►  创建 sched_counts（per-CPU 数组映射，4.2）
 └─ bpf(BPF_PROG_LOAD) ─────►  license 检查（4.3）
                                  └► verifier 静态验证（第 3 章）通过
                                     → JIT 编译为本机指令（2.4）→ 返回 prog fd
attach()（SEC 名推断，1.5）─►  创建 bpf_link，挂载 tracepoint（4.4）

sched_switch 触发（1.5）──► trace_stat 执行（5.2）：读 ctx->next_pid（1.6）、
                            sched_counts[cpu] 累计（3.4）
每秒轮询 while(!exiting) ◄─ bpf(BPF_MAP_LOOKUP_ELEM)：读回槽位求和打印（1.7/5.2）
Ctrl-C ── destroy() ───────►  关闭 prog/map/link fd，引用逐级归零（4.4）：
                              link 销毁 → prog 回收 → 释放 map 引用 → map 回收
```

怎么读图 1：左列是 loader 里依次执行的 libbpf 调用，右列是内核里的对应路径，横向箭头就是 4.1 的「命令边界」：一次 `open_and_load` 背后是两条 `bpf()` 命令（先建映射、再载程序，4.3），`attach` 是第三条；自上而下即 1.8 六步的时间顺序。

中段只属于内核的区域是数据面：挂载后 loader 退居二线，事件触发与计数累计在内核自发持续；两个世界直到「每秒轮询」才经 `BPF_MAP_LOOKUP_ELEM` 再握手——1.7 第二代通道「数据面零开销、新鲜度等于轮询周期」的图形版。末尾归零有依赖顺序（4.4），前提是没人 pin 过——引出图 2。

**图 2：对象、fd 与 pin 的三方引用关系**（fd 表 / 内核对象 / bpffs 节点，呼应 4.5）：

```text
  loader 进程（fd 表）       内核对象                    bpffs（/sys/fs/bpf）
┌─────────────┐
│ prog fd ────┼──► struct bpf_prog ◄──────┐
│ map  fd ────┼──► struct bpf_map  ◄──┐   │ link 持有 prog 的引用
│ link fd ────┼──► struct bpf_link ───┼───┘ prog 持有 map 的引用
└─────────────┘          ▲            │
                         │（每条边 = 一份引用，被指对象引用计数 +1）
                bpffs 文件节点 ── 可选边：对象被 pin（BPF_OBJ_PIN，4.5）才有，节点持有活引用

路径 A（不 pin，01-trace-stat）：进程退出 → 三个 fd 全关 → 引用归零 → 对象全部回收
路径 B（pin，第 17 章阶段 5）：bpffs 节点的边不随进程消失 → 被 pin 的对象存活，
                              直到 unpin，或接管方经 BPF_OBJ_GET 取回新 fd
```

怎么读图 2：每条边都是一份引用，来源共三种：fd 表的三条边随进程生灭（dup/fork 只是多拿句柄，4.2）；link→prog、prog→map 两条边由内核在验证与挂载时建立（4.4）；bpffs 节点的边只在显式 pin 之后存在（4.5）。对象消失只有一个条件：没有任何边再指向它。

路径 A 与 B 的差别就在那条「不受进程约束」的边：01-trace-stat 一条 pin 边都没有，Ctrl-C 即左列擦除、右侧逐级熄灭；第 17 章阶段 5 的 Go 控制面把 map/prog/link 钉到 `/sys/fs/bpf/fw17` 后退出，后续进程经 `BPF_OBJ_GET` 接回 fd 接管。两图合起来就是 4.5 的结论：没有「卸载命令」，只有引用计数；检查残留用 `bpftool prog show`/`map show`。

## 常见问题与排错

本节把前文散落的失败模式收成四组「现象/原因与处理」表。共性：编译期零警告不等于运行期正确——排错第一步是分清所在层（编译级查工具链与 BTF，加载级查权限与 verifier，运行级查数据语义）。

**第一组：load 失败与权限。** `open_and_load` 返回 NULL 时先看错误码（libbpf 日志默认输出到 stderr，含义以所用版本为准）：

| 现象 | 原因与处理 |
|---|---|
| `-EPERM` | 非 root 且 `unprivileged_bpf_disabled=2`（5.1 权限矩阵第三行，本机即此档）：不能加载任何 BPF 对象。以 root 或 `CAP_BPF`+`CAP_PERFMON` 档位运行；先跑 `book/examples/chapter-02/00-env/check-env.sh` 确认档位 |
| `-EPERM`/`-ENOMEM`（已 root 仍失败） | 内核 5.11 前按 `RLIMIT_MEMLOCK` 计费，程序或映射过大撞上限即失败；5.11+ 持 `CAP_BPF` 后不再计提（本机 7.0 属此列）。libbpf 默认先尝试抬高该上限，仍失败再查 `ulimit -l` |
| `-EACCES`（verifier 拒绝） | 静态验证未通过：越界访存、无法证明会终止、调用了本类型不可用的 helper（1.3/1.6 失败点）。日志逐行是「指令号 + 执行后的寄存器状态」：找到最后一个正常推进的指令号，其后第一条就是被拒位置；结尾 `processed N insns ... OK` 是通过标志（示意见下；精读是第 3 章正题） |
| `-EACCES`（license 不兼容） | 调用了仅 GPL 兼容 license 可用的 helper，而程序未声明或声明了不兼容 license。补 `char LICENSE[] SEC("license") = "GPL";`（01-trace-stat 末行即为此，4.3 第一道检查） |
| attach 失败 | 挂载目标不存在或无权限：SEC 名拼错（1.5，运行期才暴露的典型）、tracefs 不可见（5.1 第一层 WARN 的回扣）、非 root。核对 SEC 名与 `sudo`，按 check-env.sh 分级定位 |

verifier 日志读法示意（格式随内核与 libbpf 版本不同；示例取 2.3 的同一条防御分支）：

```text
（预期输出示例 / 运行级待验证——需在可加载环境中触发一次拒绝）
0: R1_w=ctx(off=0,imm=0) R10=fp0
; if (cpu >= MAX_CPUS)
5: (25) if r0 > 0x3f goto pc+13
...
19: (95) exit
processed 21 insns (limit 1000000) ... OK
```

**第二组：映射选型误用**（3.9 教师视角点名的事故源，逐条对回决策表）：

| 现象 | 原因与处理 |
|---|---|
| 普通 hash 做高频计数，计数悄悄偏少 | 多 CPU 先查后写同一 key，后写覆盖前写（3.1）。计数场景改 per-CPU 变体，或让 key 含 CPU 维度（3.4） |
| array 当字典，「查不到」被当成功 | 数组合法下标必命中、没有「key 不存在」状态——零值副本被当成有效数据；越界下标返回 NULL，不检查则加载都过不了（3.2）。需要增删与不存在语义就选 hash（3.9 第一行） |
| LRU 满表旧表项消失，被误判「插入失败」 | 满表静默淘汰最久未用项、插入仍成功，调用方拿不到错误码（3.3）。接受「观测宁丢旧不丢新」语义，靠遍历观察淘汰；把 `max_entries` 调小可复现（练习 2 的观察实验） |
| per-CPU 读回不聚合，总数只占零头 | 读回拿到按 CPU 序排列的全部副本，不聚合只读第一份、漏算 nr−1 份（3.4）。读回缓冲开成「单份 × CPU 数」并对槽位求和——5.2 的求和循环是标准写法 |
| `BPF_NOEXIST` 插入偶发 `-EEXIST` | 先查后插两步之间另一 CPU 已插入同 key（练习 2 注释的竞争近似）：按「本事件不计数」处理，属设计内竞争而非故障；消除竞争回到 per-CPU 选型 |

**第三组：tracepoint 上下文的版本差异**：

| 现象 | 原因与处理 |
|---|---|
| 换内核后字段读到错位数据 | 字段布局随内核版本增删调序，按固定偏移硬读会读错位置（1.6）。坚持 vmlinux.h + CO-RE 工作流，加载时由 libbpf 依据目标内核 BTF 重定位字段偏移（机制第 8 章） |
| `prev_comm` 之类字符数组长度对不上 | 本机 BTF 实测 16 字节定长，其他版本或配置可能不同。长度以 vmlinux.h 类型定义为准（`sizeof(ctx->prev_comm)`），不硬编码；定长数组直接读不越界，`__data_loc` 变长字段才需要 `bpf_probe_read_str`（5.2 口诀） |
| 字段在编译期不存在 | BPF 侧类型来自本机 BTF 生成的 vmlinux.h：目标内核没有该字段，clang 直接报「无此成员」——编译期暴露，与 SEC 名写错（运行期）方向相反（1.6 失败点）。要「编译一次、多内核运行」时，用 CO-RE 的字段存在性查询构造兼容分支（第 8 章） |

**第四组：轮询与事件流的取舍**——先自问「要的是最新状态，还是每一件事」，再看 1.7 三代通道的收口对照（perf event array 的历史位置见 3.5）：

| 维度 | 轮询 map（01-trace-stat） | ring buffer 事件流（练习 1） |
|---|---|---|
| 延迟 | 新鲜度 = 轮询周期（本例 1 秒），中间变化不可见 | 每事件可见，poll 唤醒、延迟低 |
| 开销 | 数据面约等于零，代价集中在读回侧，与事件速率无关 | 每事件一次 reserve/submit，随事件速率线性增长 |
| 丢失语义 | 无「丢失」概念，只有「最新状态快照」 | 缓冲满则 reserve 返回 NULL、新事件静默丢弃——BPF 侧可当场计数（与 3.5 静默丢弃的决定性差异，3.6） |
| 实现复杂度 | 最低：lookup + 求和打印 | 中等：事件结构 8 字节对齐、回调函数、poll 超时与 Ctrl-C 协作 |

选型口诀（1.7 完整版）：要「最新状态」选轮询，要「每一件事」选事件流；选了事件流，要管理的就是丢失——加大缓冲、在 BPF 侧对丢弃计数。练习 1 正是把 01-trace-stat 改成事件流的动手对照。

## 课后扩展任务

正文五节走完一条最小闭环，两项课后扩展任务把它向两个方向各推一步：事件通道换形态（练习 1）、映射类型换选型（练习 2）——都基于 01-trace-stat 改动，先自行实现再看参考：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 事件通道 | 把 01-trace-stat 的轮询改为 ring buffer 事件流（reserve/submit + `ring_buffer__poll`） | `book/examples/chapter-02/exercises/01-ringbuf-events/` |
| 2 | 映射类型 | 用 LRU hash 记录每进程被调度次数并打印 top-N（体验容量与静默淘汰） | `book/examples/chapter-02/exercises/02-lru-top-proc/` |

验收标准（与各 README「任务」节同口径）：练习 1 加载后能看到带 CPU 号、时间戳与 pid 对的事件流，并能说明它与轮询的取舍；练习 2 的 top-N 榜单随系统负载变化，且能解释「旧 pid 从榜单消失」的两种可能——进程退出，或 LRU 满表静默淘汰。

参考实现随目录提供，构建与主示例同构（各目录 `make`，工具链与权限前提同 5.1）。验证状态标注为「编译级待验证、运行级待验证」——本章编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行，各练习 README 的「预期输出」均为教学示例（规则见 5.1 约定表）。

**防剧透约定**：每项练习的 README 按「任务在前、参考实现思路独立成节放在后部」的结构组织——先只读任务描述与提示自行实现，卡住再回 1.7、3.3、3.6 补课；实现并观察过现象之后，才翻到分界线之后的「参考实现思路」对照。总览与各目录入口见 [`book/examples/chapter-02/exercises/`](../examples/chapter-02/exercises/)。

## 总结

八要素各收成一句话：

- **程序**（1.1、二节）：经 verifier 验证的受限 C 子集——编译通过不等于能加载，静态验证才是准入关卡。
- **映射**（1.2、三节）：内核态与用户态共享状态的对象，选型回答 key、容量、写者、读回四问（3.9）。
- **辅助函数**（1.3）：内核以稳定编号暴露的受控能力面，可用面随程序类型裁剪。
- **程序类型**（1.4）：上下文结构、helper 集合与返回值语义的总开关。
- **挂载点**（1.5）：决定触发时机与频率；tracepoint 可从 SEC 名完整推断。
- **上下文对象**（1.6）：事件现场的结构化视图，跨版本一致性由 CO-RE 重定位保证。
- **事件通道**（1.7）：从 trace_pipe 到轮询 map 再到 ring buffer 的三代演进，取舍在延迟、开销与丢失语义。
- **生命周期**（1.8、四节）：没有卸载命令，只有引用计数归零；pin 是加一条不受进程约束的引用。

八要素在 01-trace-stat 里各落一行，可视化节把它们装回全链路图，排错节把失败模式收成对照表；而把它们黏合在一起的那道工序还没有展开——加载路径上的 verifier。回头看 5.2 的每条注释，其实都在回答 verifier 将要提出的问题：`if (cpu >= MAX_CPUS)` 让它证明访存有界，`if (!p)` 让它证明指针非空，SEC 与 license 声明让它核对类型与权限边界，「为什么敢直接读 `ctx->next_pid`」靠「程序类型登记过上下文结构」作答。第 3 章进入 `kernel/bpf/verifier.c`，看静态验证如何逐指令做出「安全的证明」——同一份 trace_stat，从「写它的人」视角换到「审它的机器」视角；读本章示例时养成的注释习惯（解释字段语义、边界、选型与失败模式），到那里会变成生存技能。

## 本章参考来源

- Linux kernel documentation：BPF subsystem（`Documentation/bpf/`）、instruction set（`Documentation/bpf/instruction-set.rst`）、maps（`Documentation/bpf/maps.rst`）、helpers（`include/uapi/linux/bpf.h` 内 helper 注释）、BTF（`Documentation/bpf/btf.rst`）。
- ebpf.io：What is eBPF? 与子系统生态总览。
- Andrii Nakryiko：BTF 与 CO-RE 系列文章（vmlinux.h 工作流与重定位机制的原始阐述）。
- libbpf 文档：skeleton 生成与 `open_and_load`/`attach` API、per-CPU 映射读回语义、ring buffer（`ring_buffer__new`/`ring_buffer__poll`）。
- libbpf-bootstrap：本章示例依循的「vmlinux.h → `.bpf.o` → skeleton → loader」构建模式。
- ebpf.top：BPF 入门系列相关文章（tracepoint、映射与事件通道的中文讲解）。

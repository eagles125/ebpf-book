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

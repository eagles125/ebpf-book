# 第 4 章 eBPF 程序类型与钩子点深入

> 本章独立示例已全部落地到 [`book/examples/chapter-04/`](../examples/chapter-04/)：[`00-env/`](../examples/chapter-04/00-env/) 环境自检（含本章特有的 tracefs 与 LSM 前提检查项）、[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/) 追踪类集成锚点（同一事件五副眼镜）、[`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/) 网络类集成锚点（同一报文两种 ctx）、[`03-type-gallery/`](../examples/chapter-04/03-type-gallery/) 类型画廊（8 目录 compile-only 最小实物），各目录独立可编译，含源码、Makefile 与 README（运行说明与验证状态）；两项课后扩展任务（`exercises/`）与全量编译验证脚本（`scripts/verify-build.sh`）为计划内资产，随后续批次落地。锚点与画廊的编译级验证已在本机逐目录 `make` 通过；加载级行为需特权环境，凡涉及加载与运行的效果一律标注「预期输出示例 / 运行级待验证」（约定见各 README）。

## 学习目标

读完本章后，你应当能够：

- 说出程序类型（program type）与挂载类型（attach type，`bpf_attach_type`）两层模型各回答什么问题——前者决定「程序是什么」，后者决定「同一程序类型下挂到哪一类对象、哪个方向」——并举出一个程序类型对应多种挂载语义的实例（一节 1.1）。
- 用三元组——上下文（context，ctx）布局、辅助函数（helper）可用面、返回值语义——解释「同一个 BPF 程序为什么不能到处挂」，并复述 `bpf_get_current_pid_tgid()` 在追踪类可用、在 XDP 白名单之外的完整逻辑（一节 1.2）。
- 按 1.3 的十四类总对比表，为「观测一个内核函数、过滤网络报文、写一条安全策略」三类任务初选程序类型，并说出每类的稳定边界（一节 1.3）。
- 沿 1.4 的选型决策树，把一个任务落到「程序类型＋SEC 名＋挂载方式」三元组，并解释为什么选型错误的程序多数编译通过、到加载期才暴露（一节 1.4，回指第 3 章）。
- 讲清 SEC 命名到 libbpf 解析的机制：SEC 前缀如何决定程序类型与挂载类型，哪几类 SEC 能自动挂载、哪几类必须显式挂载（二节 2.3）。
- 解释上下文的「两层皮」：程序为什么按 UAPI 偏移写字段、内核却按内部真实偏移读——改写发生在每次加载（二节 2.2）。
- 说明 `bpf_link` 挂载模型与 fentry/fexit 依赖 BTF 的原因，并解读「签名漂移导致加载被拒」的例证（二节 2.4）。
- 编译并读懂两个集成锚点（五镜对照、两 ctx 对照）的源码与预期读数；加载级实验留待特权环境（四节实战与各示例 README）。

## 本章导读

第 1、2 章你已经写过、跑过 eBPF 程序，但只用过一个类型：tracepoint。第 2 章 1.4 给过一句话总纲——「类型 = 上下文 + helper 面 + 返回值语义」，并在同一节把完整谱系留给了本章：「完整谱系（XDP、TC、cgroup、kprobe、fentry/fexit、LSM 等）随内核版本增长，第 4 章逐类展开」。本章兑现这个预告，并正面回答那个被它压住的问题：**同一个 BPF 程序为什么不能到处挂？**

答案先给一句话：因为「程序」从来不是一段裸代码——它申报的程序类型决定了上下文长什么样、辅助函数能用哪些、返回值算什么意思，而这三件事全都跟着挂载点（attachment point）走。想在 XDP 里读「当前进程的 pid」，或在 tracepoint 里丢弃报文，都不是「写法不对」，而是类型层面就不成立。让代码「到处挂」的幻想，破灭在类型系统上；本章把它从幻想变成可以逐类 diff 的实物。

先把前两章的预告逐条回收，共三点，各自标注本章兑现位置：

- 预告①（第 2 章 1.4 末）：「完整谱系（XDP、TC、cgroup、kprobe、fentry/fexit、LSM 等）随内核版本增长，第 4 章逐类展开」→ 本章一节 1.3 以十四类总对比表正面兑现；三节画廊继续逐类走读，每类配一个最小实物（`03-type-gallery/` 8 目录）。
- 预告②（第 3 章「通往第 4 章的桥」）：「第 4 章逐类展开程序类型与钩子点——读它时随身带上两件本章的行李：2.3 的类型表当字典，排错节的速查表当地图」→ 两件行李的用法：**字典**——本章读到验证器（verifier）日志里的寄存器类型名词（`PTR_TO_CTX` 一族）或「上下文访问校验」时，回查那张寄存器状态族表，把类型名词对回语义；**地图**——任何一次加载被拒，先按第 3 章排错节「信号 → 根因类别」的映射定方向，再回到本章对应小节找机制。本章一节的三元组与总表是字典的「选型面」续篇，二节把字典背后的注册与校验机制讲开。
- 预告③（第 3 章失败画廊 card ⑥ 的反例）：`bpf_get_current_pid_tgid()` 不在 XDP 白名单——本章 1.2 做它的**正面版**：同一辅助函数在追踪族类型白名单之内可用，正反两面拼起来才是「helper 可用面跟着执行现场走」的完整结论。

学习路线按「先类型系统、再内核实现、再逐类画廊、最后实战选型」推进：一节（1.1–1.5）建立两层模型与类型三元组，收拢成十四类总对比表与选型决策树；二节（2.1–2.6）进入内核看这套系统怎么实现——类型注册表、上下文的两层皮、SEC 命名解析、挂载机制与返回值校验；三节类型画廊逐类走读十四类（8 目录最小实物，锚点补齐追踪与网络两族）；四节实战带你为真实任务选对类型并挂上钩子（两个集成锚点全链编译）。可视化、常见问题与排错、课后扩展任务随后收尾。

两个阅读约定沿用全书。其一，双视角行文——加粗的「**教师视角**」段落给类比、误区与教学取舍，「**学生动手**」段落给可操作练习，一节各小节的检查点以编号自测问题收拢。其二，验证状态约定——本章编写环境为非 root（`unprivileged_bpf_disabled=2`），凡加载与运行级效果一律「预期输出示例 / 运行级待验证」；编译级结果（`.bpf.o`、skeleton、loader 是否生成）是本机可验证的事实。内核内部机制的讲解以 5.10+ 稳定行为为基准，本机内核 7.0 仅作编译口径参考，随版本演进的细节保守措辞并注明「以所用内核源码为准」。

## 一、程序类型系统：类型决定什么

第 2 章 1.4 用 tracepoint 一类展开过「类型 = 上下文 + helper 面 + 返回值语义」，但当时只在一个类型上验证了这句话。本节把镜头拉开：先看程序类型与挂载类型为什么要分两层（1.1），再逐项配最小例验证三元组（1.2），然后把十四类收进一张总对比表（1.3），最后给出从任务到类型的选型决策树（1.4）。本节纯讲解加对照阅读，动手现场在四节实战。

### 1.1 两层模型：`bpf_prog_type` 与 `bpf_attach_type`

背景问题：第 2 章 1.5 说过「SEC 前缀被 libbpf 翻译成程序类型与挂载目标」——为什么不是一个枚举管到底？

因为「程序是什么」与「程序挂在哪」是两个正交的问题，内核用两组 UAPI 枚举分别回答。**`bpf_prog_type`**（`include/uapi/linux/bpf.h` 的 `enum bpf_prog_type`）出现在加载命令 `BPF_PROG_LOAD` 的属性里，是程序的**身份**：从加载那一刻就定了，决定验证器按哪套规则检查它（第 3 章整章）、上下文是什么布局、辅助函数白名单是哪份（二节 2.1）。**`bpf_attach_type`**（同头文件的 `enum bpf_attach_type`）出现在挂载命令（`BPF_PROG_ATTACH` 一族与 `BPF_LINK_CREATE`）里，是程序的**工位**：描述挂到哪一类对象、哪个方向。两层各管一段生命周期——加载层只需要知道身份，挂载层才知道工位；枚举层上 attach type 的成员数多于 prog type，正是「同一程序类型按挂载点细分」的直接证据（具体数目随内核版本增长，以所用内核 UAPI 头为准）。

「一个程序类型、多种挂载语义」的实例在十四类里到处都是：

- **KPROBE 类型**之下，`kprobe/`（函数入口）、`kretprobe/`（函数返回处）、`uprobe/`（用户态二进制断点）三个家族共用同一个 prog type——身份相同，工位完全不同。
- **TRACING 类型**之下，fentry、fexit 与 LSM 各有自己的 attach type——同一身份，一个看函数入口、一个看返回值、一个做安全裁决（锚点① README 对此有一句话对照）。
- **CGROUP_SKB 类型**之下，ingress 与 egress 是两个 attach type（`BPF_CGROUP_INET_INGRESS`/`BPF_CGROUP_INET_EGRESS`）——方向不同，身份同一个。
- **XDP 类型**只有一套动作语义，但挂载时有三种执行模式：generic（内核 skb 路径回退，任何设备可用）、native（驱动原生路径，需驱动支持）、offload（下放网卡固件，需硬件支持）——由挂载参数选择，支持面随内核与驱动演进（锚点②在 lo 上即走 generic 回退，运行级待验证）。

**教师视角**：把两层模型类比成医师执业——`bpf_prog_type` 是**执业资格证**（写的是类别：内科），`bpf_attach_type` 是**出诊安排**（今天在哪个院区、哪个科出诊）。资格证决定你能做什么检查、开什么药（加载期校验，第 3 章正题）；出诊安排决定你在哪、面对谁（挂载期解析）。初学者最常见的两个误区都源于把这两层混为一谈：其一，以为「换挂载点只是换个参数」——多数时候换挂载点连身份一起换，代码要重写；其二，把挂载期才有的语义（方向、模式、目标对象）写进程序里——程序里只有类型定死的动作语义，方向与目标是挂载命令的事。常见失败点：期望 XDP 程序「只对 ingress 生效」——XDP 本来就在收包路径上，没有方向可挑；想分方向，该看的是 TC 或 cgroup/skb 的 ingress/egress 挂载。

### 1.2 类型三元组：ctx 布局、辅助函数可用面、返回值语义

背景问题：「类型决定什么」的最小完备回答是三元组。逐项配一个最小例，全部取自已落地的示例资产。

**第一项：上下文布局。**程序入口第一个参数的形状由程序类型定死，换类型就是换结构体。锚点①的 tracepoint 镜拿到的是带类型的事件布局：

```c
/* book/examples/chapter-04/01-multi-observe/multi_observe.bpf.c（tracepoint 镜） */
const char *filename = (const char *)ctx->args[1];
```

`ctx` 是 `struct trace_event_raw_sys_enter *`——syscall 类 tracepoint 复用这张通用布局，`args[]` 数组有名有位，`args[1]` 就是 filename。锚点②的 XDP 侧拿到的是另一副眼镜：

```c
/* book/examples/chapter-04/02-xdp-tc/xdp_count.bpf.c */
void *data = (void *)(long)ctx->data;
void *data_end = (void *)(long)ctx->data_end;
```

`ctx` 是 `struct xdp_md *`——只有数据区间与少量元数据，报文解析全手工。同一个「事件现场」，两种布局的字段面天差地别：`__sk_buff` 里现成可读的 `len`/`mark`/`protocol`，`xdp_md` 一个都没有；反过来，`xdp_md` 的驱动层时机，`__sk_buff` 也追不回来。这就是 1.1「换挂载点连身份一起换」的 ctx 侧注脚。

**第二项：辅助函数可用面。**辅助函数原型在 `<bpf/bpf_helper_defs.h>` 里对所有程序类型**全部可见**——clang 照单全收，编译期一声不吭（第 2 章 1.3 的「编译期不报错」）。真正决定能不能用的，是内核按程序类型登记的白名单（第 3 章 2.7 的机制）。第 3 章画廊 card ⑥ 给过反面：XDP 程序调用 `bpf_get_current_pid_tgid()`，加载期白名单落空。本章给正面：这个函数在追踪族类型（KPROBE/TRACEPOINT/RAW_TRACEPOINT/TRACING）的白名单之内——锚点①的五副眼镜全属追踪族，都能调用它。根源在第 3 章 3.6 已析：「当前进程」这个概念在追踪现场有意义（事件发生在某进程的系统调用路径上），在网卡收包软中断里无意义。白名单不是官僚关卡，是「这个执行现场给这个函数提供了什么前提」的登记表。

**学生动手**：用三步亲手验证正反两面（编译级本机即可，加载级预期效果待特权环境）：

1. 在 `multi_observe.bpf.c` 任一镜里加一行 `__u64 pid = (__u32)bpf_get_current_pid_tgid();`，重新 `make`——预期编译通过（原型可见，追踪族白名单内；以所用内核登记为准，运行级待验证）。
2. 把同一行搬进 `xdp_count.bpf.c`，重新 `make`——预期**同样编译通过**：clang 不做白名单检查。
3. 加载对比（需特权环境）：前者预期加载成功；后者预期加载被拒，日志 `unknown func bpf_get_current_pid_tgid#14` 一类（预期输出示例 / 运行级待验证，第 3 章画廊 card ⑥ 同口径）。

结论一句话：辅助函数可用面不在头文件里，在内核按程序类型登记的白名单里——判别方法是查 `include/uapi/linux/bpf.h` 的原型注释或 `bpftool feature probe`，而不是猜。

**第三项：返回值语义。**同一个返回语句，在三类程序里是三种意思——画廊里正好有一组同形 ctx 的对照：

- `return 1;`（[`03-type-gallery/03-cgroup-skb/`](../examples/chapter-04/03-type-gallery/03-cgroup-skb/)）——**1=放行、0=丢弃**，cgroup 维度的网络 verdict；
- `return 0;`（[`03-type-gallery/04-socket-filter/`](../examples/chapter-04/03-type-gallery/04-socket-filter/)）——**0=不向观测 socket 拷贝本包**，报文本身照常走协议栈：返回值是「拷几字节」，不是判决；
- `return 1;`（[`03-type-gallery/05-sockops/`](../examples/chapter-04/03-type-gallery/05-sockops/)）——**1=本回调已处理、0=交还内核默认路径**，TCP 状态机回调的应答，与网络 verdict 无关。注意这是画廊的教学约定：内核对 sockops 返回值的实际消费面较窄——取值类回调（本机可复核出处：`include/net/tcp.h` 的 `tcp_call_bpf()`）在程序返回 0 时取程序经 `sock_ops.reply` 字段传入的值、非 0 一律折叠为「未提供」；纯事件类回调（如示例计数的两类 ESTABLISHED_CB）的返回值多被忽略。精确消费面随内核版本演进，以所用内核源码为准。

三者里有两个的 ctx 同为 `struct __sk_buff`，一个甚至同样写 `return 1;`——含义却各不相同。返回值语义由程序类型说了算，这是三元组里最容易被忽略、也最容易出事故的一项：把「观测类惯例返回 0」带进 cgroup/skb，`return 0;` 意味着**丢弃全部流量**。画廊③的失败分支刻意返回 1（放行），就是给这个事故写的预防针。

常见失败点小结：三元组三项各对应一类错位——ctx 错位（想读的字段新类型里没有，编译期即报）、helper 错位（编译过、加载拒，card ⑥ 一族）、返回值错位（加载过、行为反，最危险）。前两类加载前有信号，第三类只能靠读类型文档与 1.3 总表预防。

### 1.3 十四类总对比表

背景问题：三元组逐项看过了，怎么一眼比较全部类型？本节把十四类收进一张六列总表。计数口径先说清：本章涉及的 SEC 前缀族共 15 个，其中 fentry 与 fexit 同属 `BPF_PROG_TYPE_TRACING`、共享 BTF 类型化原型（只差一个尾随返回值参数），合为一行；kprobe 与 kretprobe 虽同属 `BPF_PROG_TYPE_KPROBE`，但一个看函数入口、一个看返回处，触发点相反，分作两行对照；uprobe 与 uretprobe 同族合一。于是 15−1=**十四类**；perf event 不属本章逐类走读范围，单列一行带过（第 9 章主场）。

表中事实的出处：追踪五类来自锚点①（[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)）的五镜对照表与源码，XDP/TC 来自锚点②（[`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/)），其余八类来自画廊各目录（[`03-type-gallery/`](../examples/chapter-04/03-type-gallery/) 01–08）；每类的逐类走读在三节与画廊目录 README，本表是它们的总纲。

| 程序类型 | 触发源 | ctx 类型 | 返回值语义 | 稳定边界 | 典型用途 |
|---|---|---|---|---|---|
| tracepoint（`tracepoint/`，TRACEPOINT） | 内核静态埋点（事件注册于 tracefs） | 按事件 typed 布局；syscall 类复用通用 `trace_event_raw_sys_enter`/`_exit`（锚点①实证） | 被忽略，按惯例 0 | 事件字段布局跨版本基本稳定；个别事件随版本增删 | 系统调用、调度等长期计数与审计 |
| raw tracepoint（`raw_tp/`，旧写法 `raw_tracepoint/`，RAW_TRACEPOINT） | 同一静态埋点，取原始参数 | `__u64 *` 原始数组（无类型；syscall 类形状为 `(regs, id)`，依据内核源码 `kernel/trace/trace_syscalls.c`） | 被忽略 | 布局**无**稳定承诺——参数形状随内核实现演进 | 追还没有 typed 层的新事件、对照原始参数 |
| kprobe（`kprobe/`，KPROBE） | 内核函数入口（按符号动态挂载） | `pt_regs *`（`BPF_KPROBE` 宏按 C 原型解包，架构相关展开） | 被忽略 | 符号随版本内联、改名（本机 `do_unlinkat` 在 BTF 缺席即内联实证）；不依赖 tracefs 事件注册 | 临时追踪任意内核函数的入口参数 |
| kretprobe（`kretprobe/`，KPROBE） | 内核函数返回处 | 宏内部仍是 `pt_regs *`；`BPF_KRETPROBE` 读返回值（x86 由 `PT_REGS_RC` 展开，以所用架构为准） | 被忽略 | 同 kprobe；返回值按架构寄存器约定 | 读函数返回值（成败分布） |
| fentry/fexit（`fentry/`、`fexit/`，TRACING） | 内核函数入口/返回 | BTF 直接类型化原型（fexit 尾随 `int ret`） | 被忽略（fexit 读 ret） | 依赖 `CONFIG_DEBUG_INFO_BTF`；加载期按本机 BTF 校验签名，漂移即拒（2.4）；内核 5.5+ | 长期生产追踪：低开销、类型安全 |
| uprobe（`uprobe/`、`uretprobe/`，KPROBE） | 用户态 ELF 符号偏移处的断点 | `pt_regs *`（被打断处的用户态现场） | 被忽略 | SEC 串只是标记，二进制路径与偏移挂载期提供；用户内存读必须走 probe 系辅助函数 | 用户态库/二进制的函数级追踪 |
| XDP（`xdp`，XDP） | 驱动层收包路径 | `struct xdp_md`（`data`/`data_end`/`data_meta` 与少量元数据） | `enum xdp_action`（`XDP_PASS`/`XDP_DROP` 等；在 BTF） | 动作集随版本缓慢增长；generic/native/offload 三种挂载模式随驱动与硬件支持 | 驱动层最早时刻的丢包、转发、抗 DDoS |
| TC（`tc`，旧名 `classifier`，SCHED_CLS） | 协议栈 qdisc/filter（clsact 的 ingress/egress） | `struct __sk_buff`（协议栈加工后的字段现成可读） | `TC_ACT_*`（uapi `#define`，不在 BTF——锚点②手工定义实证） | `TC_ACT` 常量族 uapi 稳定；需 `CONFIG_NET_CLS_ACT` | 协议栈入口的过滤、统计、NAT 配套 |
| cgroup-skb（`cgroup/skb`，CGROUP_SKB） | cgroup 内进程的网络事件（ingress/egress 两个挂载方向） | `struct __sk_buff`（与 TC 同形） | **1=放行 / 0=丢弃**（裸二值，非 `TC_ACT_*`） | 需 `CONFIG_CGROUP_BPF`；挂载对象是 cgroup fd | 按容器/进程组做网络准入 |
| socket filter（`socket`，SOCKET_FILTER） | 挂到某个 socket 的过路报文 | `struct __sk_buff`（与 TC、cgroup-skb 同形） | **返回值=接受拷贝给该 socket 的字节数**（0=该包对它不可见）——非 verdict | 最古老的程序类型；只读观测、不干预协议栈 | tcpdump/pcap 抓包过滤 |
| sockops（`sockops`，SOCK_OPS） | TCP 状态机事件（挂载于 cgroup） | `struct bpf_sock_ops`（两端地址端口、TCP 状态、`snd_cwnd` 等 socket 级字段） | 1=本回调已处理 / 0=交还默认路径（**教学约定**——内核实际消费面较窄：取值类回调返回 0 并经 `sock_ops.reply` 传值、非 0 一律视为未提供；事件类回调多忽略，随内核版本演进，以所用内核源码为准，见 1.2） | socket 级字段随版本演进；需 cgroup v2 | TCP 调优、为 sockmap 收纳 socket |
| sk_msg（`sk_msg`，SK_MSG） | sockmap 转发路径的判决点 | `struct sk_msg_md`（报文内存区间与两端 socket 元数据） | `__SK_PASS`/`__SK_DROP`（`enum sk_action`，带下划线前缀，在 BTF） | 需 sockmap＋sockops 配对搭建，判决端才被触发 | socket 间消息重定向判决 |
| LSM（`lsm/<hook 名>`，LSM） | 内核安全决策点（hook） | hook 原型参数直列（如 `struct file *`，内核对象不经转接直接可读） | 0=允许 / 非 0=阻断（裁决语义以所用内核源码 `lsm_hook_defs.h` 为准） | 三前提：root＋`CONFIG_BPF_LSM=y`＋启动参数含 `lsm=bpf`（本机配置实证 =y、启动参数未启用，运行级待验证）；内核 5.7+ | 安全审计与准入策略 |
| netfilter（`netfilter`，NETFILTER） | netfilter 钩子点（协议族/hook 号/优先级显式指定） | `struct bpf_nf_ctx{ state, skb }`（本机 BTF 实证） | `NF_ACCEPT`/`NF_DROP`（uapi `#define`，不在 BTF） | 内核 6.6+ 新程序类型 | 用 BPF 写防火墙规则（可运行示例见第 9 章） |
| perf event（`perf_event` 一族，PERF_EVENT） | perf 事件（采样、计数溢出） | 采样现场结构（随版本演进，第 9 章展开） | 被忽略 | 与 perf 工具链配合使用 | 性能剖析采样——**第 9 章主场**，本章不展开 |

读表方法三句话：先看**返回值语义**列防误用（同形 ctx 不同语义是这张表最深的沟）；再看**稳定边界**列评估长期成本（「跨版本稳定」与「随内核实现演进」决定升级内核时要不要重验程序）；最后按**典型用途**落到 1.4 的决策树。ctx 类型列里反复出现的 `__sk_buff` 提醒一件事：ctx 同形不代表类型可互换——三元组另外两项跟着程序类型走。

### 1.4 选型决策树：从任务到三元组

背景问题：十四类都摆出来了，拿到一个真实任务怎么落？下面的文本决策树按「任务性质 → 要什么数据 → 类型＋挂载」两步分叉，每片叶子输出一个三元组（程序类型，SEC 名，挂载方式）：

```text
任务是什么？
|
├─ 观测：回答「系统里发生了什么」
|   ├─ 内核函数，长期跑、要类型安全 ........ fentry/fexit（BTF 原型；2.4 的签名校验兜底）
|   ├─ 内核函数，临时调试、符号随时可换 .... kprobe（＋kretprobe 读返回值）
|   ├─ 固定埋点，跨版本稳定优先 ............ tracepoint（typed 事件布局）
|   ├─ 新事件还没有 typed 层 ............... raw tracepoint（原始参数，接受无类型）
|   ├─ 用户态函数 .......................... uprobe/uretprobe（SEC 串只是标记）
|   └─ 性能采样剖析 ........................ perf event（第 9 章主场）
|
├─ 网络：对报文做观测或干预
|   ├─ 最早时刻、只关心报文本体 ............ XDP（驱动层；XDP_PASS/XDP_DROP）
|   ├─ 协议栈入口、要 skb 元数据 ........... TC（clsact；TC_ACT_*）
|   ├─ 按容器/cgroup 准入 .................. cgroup/skb（1 放行 / 0 丢弃）
|   ├─ 抓包交给某个 socket ................. socket filter（返回拷贝字节数）
|   └─ socket 级状态或重定向 ............... sockops＋sk_msg（sockmap 体系）
|
└─ 安全：对操作做裁决
    ├─ 内核安全 hook（文件打开、进程执行等） . LSM（0 允许 / 非 0 阻断；三前提见 1.3 表）
    └─ 网络防火墙规则 ....................... netfilter（NF_ACCEPT/NF_DROP；6.6+）
```

决策树只回答「选哪类」；「怎么挂」由 SEC 名与挂载方式补齐——自动挂载与显式挂载的边界在二节 2.3 展开，锚点①（一个 loader 用通用挂载 API 挂 5 个 link）与锚点②（XDP 走 link、TC 走 netlink）是两条路线的实物。

**教师视角**：为什么选型错误的程序多数**编译通过**？因为三道类型关卡都不在编译期。其一，辅助函数原型对所有类型可见，clang 不做白名单检查（第 3 章画廊 card ⑥）；其二，SEC 名只是 ELF 里的字符串，clang 不校验内容（第 2 章 1.5）；其三，ctx 字段访问在编译期是「对某个结构体的正常访问」——类型选错时结构体整个换掉了，编译器无从比对「你想访问的字段在新类型里是否存在」。类型检查集中在加载期：libbpf 按 SEC 解析类型（2.3），验证器按程序类型换表校验（第 3 章整章与本章二节）。教学含义很直接：练选型不能只「按编译键」，要按「`make` 全过＋加载预期」的口径练——四节实战的两个锚点与课后扩展任务（`exercises/`，随后续批次落地）里的「错位→归位」挑战都按这个口径设计。

### 1.5 检查点

先自答，再看提示：

1. TC、cgroup/skb、socket filter 三类的 ctx 同为 `struct __sk_buff`，返回值语义有什么不同？各写一行返回语句。（提示：TC `return TC_ACT_OK;`——放行；cgroup/skb `return 1;` 放行、`return 0;` **丢弃**；socket filter 返回「接受拷贝的字节数」，`return 0;` 是不投递给观测 socket 而非丢包——实物对照 [`03-type-gallery/03-cgroup-skb/`](../examples/chapter-04/03-type-gallery/03-cgroup-skb/)、[`04-socket-filter/`](../examples/chapter-04/03-type-gallery/04-socket-filter/) 与锚点②。）
2. 要长期统计 `do_sys_openat2` 的成败分布，十四类里首选哪类？kretprobe 也能做，为什么不是首选？（提示：fexit——BTF 类型化原型＋尾随 `ret` 直接读返回值，是锚点①五镜中唯一能直接看到返回值的一镜；kretprobe 读返回值靠架构寄存器展开（`BPF_KRETPROBE`/`PT_REGS_RC`），类型校验弱于 BTF 路线；fexit 的签名在加载期由本机 BTF 校验，漂移即拒（2.4）。）
3. `bpf_get_current_pid_tgid()` 在锚点①的五副眼镜里都能用吗？在锚点②的 XDP 侧呢？判别方法是什么？（提示：五镜全属追踪族，白名单之内——1.2 的正面演示；XDP 白名单之外，「当前进程」在网卡收包现场无意义（第 3 章画廊 card ⑥ 的预期日志）；判别不靠猜——查 `include/uapi/linux/bpf.h` 原型注释或 `bpftool feature probe`，以所用内核登记为准。）

**独立示例**：[`book/examples/chapter-04/03-type-gallery/`](../examples/chapter-04/03-type-gallery/)——十四类「怎么写对」的最小实物总览（8 目录 compile-only，每目录一个最小程序＋六段 README），1.3 对比表逐列的代码出处；环境自检（含 tracefs 与 LSM 前提检查）见 [`../examples/chapter-04/00-env/`](../examples/chapter-04/00-env/)。

## 二、类型与钩子的内核实现分析

一节回答了「类型决定什么」，本节回答「内核怎么把这套决定做出来」：类型注册表从枚举到验证器操作组的编译期生成（2.1）、上下文「两层皮」与加载期改写（2.2）、SEC 命名与 libbpf 解析（2.3）、`bpf_link` 挂载机制与 BTF 签名校验（2.4）、返回值的按类型校验（2.5）。口径沿全章：讲解以 5.10+ 稳定行为为基准；本机内核 7.0 与其内核源码树（`/lib/modules/$(uname -r)/build` 下的头文件）仅作检索与编译口径参考；函数名、结构体字段等细节随内核版本演进，一律以所用内核源码为准，加载期最终由 BTF 与验证器校验。

### 2.1 注册表：从 UAPI 枚举到 verifier_ops

背景问题：一节说「程序类型决定三件事」——这在内核代码里对应什么数据结构？答案是一张**编译期展开的注册表**，链路如下：

```text
include/uapi/linux/bpf.h            include/linux/bpf_types.h（宏表）
  enum bpf_prog_type  ──编号──▶  BPF_PROG_TYPE(BPF_PROG_TYPE_XDP, xdp,
                                        struct xdp_md, struct xdp_buff)
                                             |  kernel/bpf/bpf_types.c 多次展开宏表
                                             v
                        编译期生成：每类型一组 bpf_prog_ops ＋ bpf_verifier_ops
                                             |
                                             v
                        加载期：BPF_PROG_LOAD 按编号查表 → 验证器全程问这组 ops
```

第一层是 **UAPI 枚举**。`enum bpf_prog_type` 声明在 `include/uapi/linux/bpf.h`——UAPI 层意味着编号进系统调用 ABI：`bpf_attr` 里加载属性的字段直接填它，用户态与内核对「某个编号是什么类型」必须口径一致。枚举随内核版本增长（NETFILTER 一行是 6.6+ 新增），已发布编号保持稳定。

第二层是**宏表**。每个已实现类型的登记行写在 `include/linux/bpf_types.h`，一行一个 `BPF_PROG_TYPE` 宏调用，四个参数分别是：UAPI 编号、内部名、程序侧 ctx 结构、内核侧真实结构——最后一对参数就是 2.2「两层皮」的登记处。`kernel/bpf/bpf_types.c` 把这张表以不同宏定义多次展开，编译期生成两样东西：每类型一组 `bpf_prog_ops`（运行侧薄封装）与 `bpf_verifier_ops`（验证侧能力申报），外加把 UAPI 编号接到这组 ops 的查找表（`bpf_types[]` 一类数组，随版本演进）。注意表内条目按 `CONFIG_NET`/`CONFIG_CGROUP_BPF` 等内核配置分段——**枚举在 UAPI 全量存在，实现按配置在场**：发行版内核没开某个 CONFIG，对应类型就不可加载。

第三层是**能力申报**。`struct bpf_verifier_ops`（声明于 `include/linux/bpf.h`，本机内核源码树可检索）至少有两个关键回调：`get_func_proto`——「给定辅助函数编号，本类型的原型或 NULL」，这就是第 3 章 2.7 白名单机制的登记处；`is_valid_access`——「对 ctx 的偏移 off、宽 size、读/写访问是否合法」，2.2 的主角。另有 prologue/epilogue 生成等回调，随版本演进。加载路径（第 2 章 4.3，第 3 章 2.1 的 `bpf_check()`）拿到程序类型后按注册表查到这组 ops，此后验证器的每个类型相关问题——白名单、ctx 校验、prologue——都问它。「类型决定什么」落到代码里就是一句话：**这组 ops 是谁**。

本机可复核（本地验证，编写环境内核 7.0 的源码树）：`grep "BPF_PROG_TYPE(BPF_PROG_TYPE_NETFILTER" /lib/modules/$(uname -r)/build/include/linux/bpf_types.h` 能看到 NETFILTER 行在场；同一文件里可见 `SOCKET_FILTER`、`SCHED_CLS`、`XDP`、`TRACING`、`LSM` 等本章涉及的每一行。

### 2.2 ctx 的两层皮：UAPI 声明与内核改写

背景问题：`__sk_buff` 有几十个字段，内核真实的 `sk_buff` 更是庞大且随版本频繁变动——程序按前者写字段、内核按后者拿数据，两边怎么对得上？答案是**两层皮**加一次**加载期改写**。

第一层皮是 **UAPI ctx 结构**：`include/uapi/linux/bpf.h` 里定义程序看到的 `struct __sk_buff`、`struct xdp_md`、`struct bpf_sock_ops` 等，字段偏移跨版本稳定——这是对已发布程序的 ABI 承诺。第二层皮是**内核内部对象**：真实的 `struct sk_buff`、`struct xdp_buff`——字段多得多、布局随版本自由演进。宏表一行登记的「程序侧 ctx / 内核侧 ctx」一对结构（2.1），就是这两层皮的接头处。

两层皮之间由验证器收尾阶段的改写打通，机制分两步，都由 2.1 的 `bpf_verifier_ops` 提供：

1. **`is_valid_access` 白名单**：验证器在逐指令验证阶段遇到 ctx 字段访问，就问本类型回调——「偏移 off、宽 size、读还是写，合法吗」。回答合法的偏移集合，就是第 3 章 2.3 `PTR_TO_CTX` 类型说过的「按程序类型申报」的申报表本体；回答之外的字段，程序根本写不出来（编译期 C 报错）或访问不了（加载期拒）。
2. **改写为内部偏移**：验证通过后，加载路径把字节码里所有「按 UAPI 偏移读 ctx 字段」的指令，改写成「对内核内部对象真实偏移的读取」（改写函数族在 `kernel/bpf/verifier.c` 一带，网络类类型的改写规则在 `net/core/filter.c` 一带，随内核版本演进）。

「ctx 字段为什么按 UAPI 偏移稳定」由此作答：**改写发生在每次加载**。内核可以在任何版本里重排 `sk_buff` 内部布局，只要同步更新改写规则，按老 UAPI 偏移编译的存量程序照常工作——稳定的是 UAPI 契约，不是内部布局；翻译成本在加载期一次付清，运行期零额外开销（第 3 章 1.2「静态证明优先」的又一实例）。锚点②是两层皮差异的最短对照：`xdp_md` 只申报了 `data`/`data_end`/`data_meta` 与少量元数据（`xdp_count.bpf.c` 头注口径），`__sk_buff` 却申报了 `mark`/`protocol`/`len` 等协议栈加工后的现成字段——同一批报文，两层皮的可申报字段面不同，这正是 1.3 总表 ctx 列差异的机制来源。

预告①的追账在此收一笔：fentry/fexit 的 ctx 是**第三种形态**——不是结构体、没有偏移可言，而是 BTF 类型化的 C 原型直列（`multi_observe.bpf.c` 的 `BPF_PROG(fe_entry, int dfd, const char *filename, struct open_how *how)`）。它的校验与改写机制在 2.4 展开。

### 2.3 SEC 命名与 libbpf 解析

背景问题：一节的类型决策最终要写进代码。开发者标注类型的方式不是填结构体，而是在函数前写一行 `SEC("...")`——这行字符串如何变成程序类型与挂载类型？

先看字符串本身。`SEC` 宏展开为 `__attribute__((section(name), used))`（`/usr/include/bpf/bpf_helpers.h`，本机实证定义在场）——作用是把函数放进 ELF 对象文件里**以该字符串命名的 section**。clang 对字符串内容零校验：写成 `SEC("xdpp")` 编译照过，错误在 libbpf 按名解析或 attach 时才爆出来（第 2 章 1.5 的「编译通过、运行报错」最典型样本）。SEC 命名法的权威文档在 `bpf_helpers.h` 的头注释与 libbpf 源码（解析规则随 libbpf 版本演进，以所用版本文档为准）。

libbpf 打开对象文件时逐函数读 section 名，按**前缀**解析出三样东西：程序类型（prog_type）、挂载类型（attach_type）、挂载参数。本章涉及的 15 种 SEC 逐行如下——「自动」指 SEC 名携带了挂载所需的全部信息，skeleton 的 attach 函数可直接挂；「显式」指挂载参数编译期不在 SEC 名里，loader 侧必须补齐：

| SEC 前缀 | 程序类型 | 挂载信息 | 挂载方式 |
|---|---|---|---|
| `tracepoint/<分类>/<事件>` | TRACEPOINT | 事件目录即挂载目标 | 自动（`bpf_program__attach()`） |
| `raw_tp/<事件>`（旧写法 `raw_tracepoint/`） | RAW_TRACEPOINT | 事件名 | 自动 |
| `kprobe/<符号>` | KPROBE | 目标内核符号 | 自动 |
| `kretprobe/<符号>` | KPROBE（retprobe 变体） | 目标符号的返回处 | 自动 |
| `fentry/<符号>` | TRACING（attach：`BPF_TRACE_FENTRY`） | BTF 中的目标函数 | 自动 |
| `fexit/<符号>` | TRACING（attach：`BPF_TRACE_FEXIT`） | 同上＋读返回值 | 自动 |
| `uprobe/`、`uretprobe/` | KPROBE 变体 | 二进制路径＋偏移（挂载期提供，SEC 串留空） | 显式（`bpf_program__attach_uprobe()`） |
| `xdp` | XDP | 目标接口（loader 传入接口索引） | 显式（`bpf_program__attach_xdp()`，返回 link） |
| `tc`（旧名 `classifier`） | SCHED_CLS | clsact qdisc＋方向/handle/优先级 | 显式（libbpf `bpf_tc_*`，netlink 模型） |
| `cgroup/skb` | CGROUP_SKB | cgroup fd＋ingress/egress 方向 | 显式（`bpf_prog_attach()`/`attach_cgroup()`） |
| `socket` | SOCKET_FILTER | 目标 socket fd | 显式（`SO_ATTACH_BPF` setsockopt） |
| `sockops` | SOCK_OPS | cgroup fd | 显式（`bpf_prog_attach(BPF_CGROUP_SOCKOPS)`） |
| `sk_msg` | SK_MSG | sockmap/sockhash map fd | 显式（`bpf_prog_attach(BPF_SK_MSG_VERDICT)`） |
| `lsm/<hook 名>` | LSM | load 后即进入该 hook 裁决链（全局生效） | 自动（skeleton attach） |
| `netfilter` | NETFILTER | 协议族＋hook 号＋优先级 | 显式（`bpf_program__attach_netfilter()`） |

三点说明。其一，「自动/显式」是 libbpf 层面的工作方式差异，不是内核语义差别——显式类只是挂载参数必须由 loader 补齐：锚点①五个 SEC 全带挂载信息，一个通用 `bpf_program__attach()` 挂 5 个 link（`multi_observe.c`，skeleton 前缀 `multi_observe_bpf` 由对象文件名派生）；锚点②的 XDP 走 `attach_xdp`、TC 走 `bpf_tc_hook_create`＋`bpf_tc_attach`，一个 loader 里两条路线并存。其二，`SEC("tc")` 与旧名 `classifier`：libbpf 规范化后 `tc` 是现行前缀、`classifier` 为兼容旧名，两者解析到同一程序类型 SCHED_CLS——旧材料里见 `SEC("classifier")` 不必惊讶（兼容面随 libbpf 版本演进，以所用版本文档为准）。其三，raw_tp 是前缀解析的**边界样本**：前缀只决定程序类型，ctx 形状由内核事件类决定——syscall 类 raw_tp 的原始参数形状为 `(regs, id)`（依据内核源码 `kernel/trace/trace_syscalls.c`），锚点①的 raw 镜因此按 `__u64 *ctx` 只读形状处理、不拆字段（本镜只计数并存 `ctx[1]` 供对照）。ctx 无 UAPI 结构、libbpf 无从校验字段——「类型化程度谱系」里 raw_tp 排在 tracepoint 之后的机制原因。

常见失败点：SEC 名写错（如少一段、拼错事件名）时编译级一切正常，错误在 load/attach 阶段按「查无此事件/符号」类日志爆出——排错先对照本表逐字符核对 SEC 串，再查事件在内核侧是否存在（tracefs 事件目录、BTF 符号）。

### 2.4 挂载机制：`bpf_link`、自动挂载与 BTF 签名校验

背景问题：通过验证的程序只是「待命」——第 2 章 1.8 的生命周期六步（加载→验证→JIT→挂载→触发→卸载）里，挂载正是承前启后的第四步。现代内核把这一步做成什么形状？

主干是 **`bpf_link` 模型**：挂载动作经 `bpf(2)` 的 `BPF_LINK_CREATE` 命令完成，attach type 决定 link 的子操作——挂到 tracepoint、tracing（fentry/fexit）、cgroup、netfilter 等不同对象，走不同的 link 类型与内核路径（link 类型清单随内核版本演进，以所用内核源码为准）。link 的关键语义是**fd 化的挂载关系**：挂载关系本身是一个可持有、可传递的对象，进程退出或显式销毁 link 即卸载——程序的生命周期与挂载的生命周期解耦；需要跨越进程存活的场景把 link pin 到 bpffs（第 2 章 4.5 的 pin 机制回指）。2.3 表里的「显式」类型并非都不走 link：XDP 的 libbpf 挂载就返回 link（锚点②实证），而 TC 走 netlink 的 qdisc/filter 模型、没有 link 对应物——同一章内两种生命周期形态都有实物。

**fentry/fexit 为什么依赖 BTF**：fentry 系程序没有 pt_regs 中转，参数按 C 原型直传（2.2 末的第三种形态）——内核必须在目标函数入口放一段按**真实签名**生成的中转代码（trampoline 一类机制，随版本演进），参数个数与类型错一个都放不了。真实签名从哪里来？`/sys/kernel/btf/vmlinux`。加载期，内核把程序声明的原型与本机 BTF 里的函数原型核对：函数不存在（如被内联优化掉——本机 BTF 中 `do_unlinkat` 缺席即实证）报 ENOENT 类错误；签名不匹配报 EINVAL 类错误（预期日志，运行级待验证；锚点① README 排错表同口径）。

签名漂移不是假设，本章编写环境就撞到一例（冒烟结论⑤，本地验证）：`vfs_unlink` 在本机 BTF（内核 7.0）中是四参数函数，第 4 参数为 `struct delegated_inode *`；而上游 6.x 的对应位置是 `struct inode **`——同一函数的签名在两代内核之间漂移。也就是说，同一份 fexit 程序，在这个内核加载通过、换个版本就会被签名校验拒掉——**校验在干活，不是 bug**。锚点①选 `do_sys_openat2` 做五镜统一事件，正因为它的三参数签名（`int dfd, const char *filename, struct open_how *how`）在 5.10+ 稳定且本机 BTF 实证。动手核对两步（本地验证；type_id 编号随内核版本不同，以本机输出为准）：

```bash
bpftool btf dump file /sys/kernel/btf/vmlinux | grep -E "FUNC '(vfs_unlink|do_sys_openat2)'"
# [110989] FUNC 'do_sys_openat2' type_id=46196 linkage=static
# [165979] FUNC 'vfs_unlink' type_id=64208 linkage=static
bpftool btf dump file /sys/kernel/btf/vmlinux | grep -A4 "^\[64208\]"
# [64208] FUNC_PROTO '(anon)' ret_type_id=125517 vlen=4
#   'idmap' … 'dir' … 'dentry' … 'delegated_inode' …（本机四参，漂移实证）
```

把 2.3 的「自动挂载」补完：skeleton 生成的 attach 函数逐程序调用 libbpf 的挂载路径——SEC 名带全信息的程序自动解析参数，任何一个挂载失败即报错返回，loader 侧统一清理已挂的 link（锚点①源码的失败分支口径）。这条「逐个挂载、失败即清理」的路径，就是 1.4 决策树里「自动」叶子的全部实现。

### 2.5 返回值校验：类型定死的输出通道

背景问题：程序与挂载点之间唯一的输出通道是返回值——写错了怎么办？内核的答案是把「约定」升级成「加载期校验」：验证器在验证收尾对返回寄存器 R0 做按类型的值域检查（`check_return_code` 一类回调，`kernel/bpf/verifier.c`，名称与精确规则随版本演进，以所用内核源码为准）。enforce 规则由 `bpf_prog_type` 决定，1.3 总表的返回值语义列在机制面上收口：

- **XDP**：返回值必须落在 `enum xdp_action` 值域——返回越界动作在加载期被拒。`XDP_PASS` 在 BTF（`enum xdp_action` 成员）而 `TC_ACT_OK` 不在（uapi `#define`，锚点②手工定义实证）——常量在场性的差异不影响校验本身：校验对象是 R0 的**值域**，不是符号。
- **cgroup/skb**：0/1 值域——「1 放行、0 丢弃」不是文档约定而是校验规则；写 `return 2;` 加载期即拒（预期行为，运行级待验证）。
- **LSM**：裁决语义 0=允许、非 0=阻断（逐 hook 的语义以 `lsm_hook_defs.h` 为准，画廊⑦口径）；值域的精确 enforce 随版本演进。
- **追踪族与观测类**（tracepoint/raw_tp/kprobe/uprobe/fentry/fexit/perf event）：返回值被内核忽略，按惯例返回 0；fexit 的返回值语义（含修改目标函数返回值一类的演进能力，内核较新版本引入）随版本演进，以源码为准。
- **socket filter**：返回值是「拷贝字节数」语义——值域是字节量而不是 verdict 集合，1.3 表里它那一格的「非 verdict」由此而来。

值得停一步看这个设计的全貌：第 3 章的验证器按「这一版程序」证明**输入侧**安全（访存、终止、白名单）；本节是同一台验证器按程序类型换一张表，再证明**输出侧**合法（动作值域）。输入输出两侧都过，程序才被放行——「类型决定返回值语义」从一句文档话，变成了加载期可执行的检查。

### 2.6 检查点

先自答，再看提示：

1. 为什么内核重排 `sk_buff` 内部布局不会破坏已发布的 TC 程序，而改 `vfs_unlink` 的签名会让 fexit 程序加载失败？（提示：两层皮——TC 程序按 UAPI 偏移写 `__sk_buff` 字段，加载期由验证器改写为内部真实偏移，改写规则每次加载现算、跟着内核走（2.2）；fentry/fexit 的原型不经 UAPI 中转、直接依赖本机 BTF 中的目标函数签名，签名漂移即被校验拒绝（2.4 的 `vfs_unlink` 例证）。）
2. `SEC("tc")` 与旧名 `SEC("classifier")` 编出的对象，加载行为有区别吗？程序类型是谁决定的、在哪个环节？（提示：无区别——两者被 libbpf 解析到同一程序类型 SCHED_CLS；解析发生在 libbpf 打开对象文件、逐函数读 section 名的环节（2.3），clang 只负责把字符串放进 ELF section、不校验内容——「类型检查不在编译期」的 SEC 版。）

**延伸路径**：本节涉及的源码检索与 bpftool 命令索引见附录 B（[`book/appendix/b-tool-command-index.md`](../appendix/b-tool-command-index.md)）；BTF 缺失环境下的内核自建路径见附录 C（[`book/appendix/c-distro-btf-build-guide.md`](../appendix/c-distro-btf-build-guide.md)）。

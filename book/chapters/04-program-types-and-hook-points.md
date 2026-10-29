# 第 4 章 eBPF 程序类型与钩子点深入

> 本章独立示例已全部落地到 [`book/examples/chapter-04/`](../examples/chapter-04/)：[`00-env/`](../examples/chapter-04/00-env/) 环境自检（含本章特有的 tracefs 与 LSM 前提检查项）、[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/) 追踪类集成锚点（同一事件五副眼镜）、[`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/) 网络类集成锚点（同一报文两种 ctx）、[`03-type-gallery/`](../examples/chapter-04/03-type-gallery/) 类型画廊（8 目录 compile-only 最小实物），各目录独立可编译，含源码、Makefile 与 README（运行说明与验证状态）；两项课后扩展任务（`exercises/`）与全量编译验证脚本（`scripts/verify-build.sh`）均已落地。锚点与画廊的编译级验证已在本机逐目录 `make` 通过；加载级行为需特权环境，凡涉及加载与运行的效果一律标注「预期输出示例 / 运行级待验证」（约定见各 README）。

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

背景问题：第 2 章 1.4/1.5 说过「SEC 前缀被 libbpf 翻译成程序类型与挂载目标」——为什么不是一个枚举管到底？

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

决策树只回答「选哪类」；「怎么挂」由 SEC 名与挂载方式补齐——自动挂载与显式挂载的边界在二节 2.3 展开，锚点①（一个 loader 用通用挂载 API 挂 5 个 link，raw_tp 镜可挂性见 3.1 保守口径）与锚点②（XDP 走 link、TC 走 netlink）是两条路线的实物。

**教师视角**：为什么选型错误的程序多数**编译通过**？因为三道类型关卡都不在编译期。其一，辅助函数原型对所有类型可见，clang 不做白名单检查（第 3 章画廊 card ⑥）；其二，SEC 名只是 ELF 里的字符串，clang 不校验内容（第 2 章 1.5）；其三，ctx 字段访问在编译期是「对某个结构体的正常访问」——类型选错时结构体整个换掉了，编译器无从比对「你想访问的字段在新类型里是否存在」。类型检查集中在加载期：libbpf 按 SEC 解析类型（2.3），验证器按程序类型换表校验（第 3 章整章与本章二节）。教学含义很直接：练选型不能只「按编译键」，要按「`make` 全过＋加载预期」的口径练——四节实战的两个锚点与课后扩展任务（`exercises/`，已落地）里的「错位→归位」挑战都按这个口径设计。

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

「ctx 字段为什么按 UAPI 偏移稳定」由此作答：**改写发生在每次加载**。内核可以在任何版本里重排 `sk_buff` 内部布局，只要同步更新改写规则，按老 UAPI 偏移编译的存量程序照常工作——稳定的是 UAPI 契约，不是内部布局；翻译成本在加载期一次付清，运行期零额外开销（第 3 章 1.2「能静态证明的绝不下沉到运行时」的又一实例）。锚点②是两层皮差异的最短对照：`xdp_md` 只申报了 `data`/`data_end`/`data_meta` 与少量元数据（`xdp_count.bpf.c` 头注口径），`__sk_buff` 却申报了 `mark`/`protocol`/`len` 等协议栈加工后的现成字段——同一批报文，两层皮的可申报字段面不同，这正是 1.3 总表 ctx 列差异的机制来源。

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

三点说明。其一，「自动/显式」是 libbpf 层面的工作方式差异，不是内核语义差别——显式类只是挂载参数必须由 loader 补齐：锚点①五个 SEC 全带挂载信息，一个通用 `bpf_program__attach()` 挂 5 个 link（raw_tp 镜可挂性见 3.1 保守口径；`multi_observe.c`，skeleton 前缀 `multi_observe_bpf` 由对象文件名派生）；锚点②的 XDP 走 `attach_xdp`、TC 走 `bpf_tc_hook_create`＋`bpf_tc_attach`，一个 loader 里两条路线并存。其二，`SEC("tc")` 与旧名 `classifier`：libbpf 规范化后 `tc` 是现行前缀、`classifier` 为兼容旧名，两者解析到同一程序类型 SCHED_CLS——旧材料里见 `SEC("classifier")` 不必惊讶（兼容面随 libbpf 版本演进，以所用版本文档为准）。其三，raw_tp 是前缀解析的**边界样本**：前缀只决定程序类型，ctx 形状由内核事件类决定——syscall 类 raw_tp 的原始参数形状为 `(regs, id)`（依据内核源码 `kernel/trace/trace_syscalls.c`），锚点①的 raw 镜因此按 `__u64 *ctx` 只读形状处理、不拆字段（本镜只计数并存 `ctx[1]` 供对照）。ctx 无 UAPI 结构、libbpf 无从校验字段——「类型化程度谱系」里 raw_tp 排在 tracepoint 之后的机制原因。

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
- **追踪族与观测类**（tracepoint/raw_tp/kprobe/uprobe/fentry/fexit/perf event）：返回值被内核忽略，按惯例返回 0；fexit 的返回值语义随版本演进（修改目标函数返回值走 `fmod_ret`/`BPF_MODIFY_RETURN` 独立通道，内核较新版本引入），以源码为准。
- **socket filter**：返回值是「拷贝字节数」语义——值域是字节量而不是 verdict 集合，1.3 表里它那一格的「非 verdict」由此而来。

值得停一步看这个设计的全貌：第 3 章的验证器按「这一版程序」证明**输入侧**安全（访存、终止、白名单）；本节是同一台验证器按程序类型换一张表，再证明**输出侧**合法（动作值域）。输入输出两侧都过，程序才被放行——「类型决定返回值语义」从一句文档话，变成了加载期可执行的检查。

### 2.6 检查点

先自答，再看提示：

1. 为什么内核重排 `sk_buff` 内部布局不会破坏已发布的 TC 程序，而改 `vfs_unlink` 的签名会让 fexit 程序加载失败？（提示：两层皮——TC 程序按 UAPI 偏移写 `__sk_buff` 字段，加载期由验证器改写为内部真实偏移，改写规则每次加载现算、跟着内核走（2.2）；fentry/fexit 的原型不经 UAPI 中转、直接依赖本机 BTF 中的目标函数签名，签名漂移即被校验拒绝（2.4 的 `vfs_unlink` 例证）。）
2. `SEC("tc")` 与旧名 `SEC("classifier")` 编出的对象，加载行为有区别吗？程序类型是谁决定的、在哪个环节？（提示：无区别——两者被 libbpf 解析到同一程序类型 SCHED_CLS；解析发生在 libbpf 打开对象文件、逐函数读 section 名的环节（2.3），clang 只负责把字符串放进 ELF section、不校验内容——「类型检查不在编译期」的 SEC 版。）

**延伸路径**：本节涉及的源码检索与 bpftool 命令索引见附录 B（[`book/appendix/b-tool-command-index.md`](../appendix/b-tool-command-index.md)）；BTF 缺失环境下的内核自建路径见附录 C（[`book/appendix/c-distro-btf-build-guide.md`](../appendix/c-distro-btf-build-guide.md)）。

## 三、类型画廊：十四类逐类走读

一节立起三元组与总表，二节拆开内核机制，本节把十四类逐一请上展台。每类一张 card，体例固定六段：**背景问题** → **代码**（贴全文或关键段）→ **预期输出/挂载形态** → **类型要点**（三元组哪个维度被点亮）→ **与相邻类型的选择边界** → **独立示例**；失败形态不展开，各 card 只回指第 3 章「三、失败画廊：七类典型拒绝」（[`book/examples/chapter-03/02-fail-gallery/`](../examples/chapter-03/02-fail-gallery/)）。

计数口径先互认：1.3 总表按**程序类型**合并——fentry/fexit 同属 `BPF_PROG_TYPE_TRACING`，合为一行（15−1=14）；本节 card 按**触发时机**拆卡——fentry 站函数入口、fexit 站返回处，各占一张（3.4/3.5），LSM 与 netfilter 同为「verdict 决策点」合并对照（3.14）。数 card 得 14、数表行（perf event 单列行不计）也得 14——**总数相同，合并维度不同**，对位看维度、勿数条数；perf event 不入画廊（第 9 章主场）。

卡片分三组：3.1–3.5 五镜共用锚点①对照讲解（全文与读数在 3.1 上贴）；3.6/3.7 网络双 ctx 共用锚点②；3.8–3.14 走读画廊 8 实物（3.14 覆盖 07/08）。

### 3.1 tracepoint：带类型的事件布局（镜 0）

**背景问题**：想在系统调用发生的瞬间记一笔，又不想随内核版本追符号——挂哪最稳？tracepoint 的答案：内核静态埋点，事件在 tracefs 注册，字段布局跨版本基本稳定（1.3 表）。

**代码**（贴全文）：锚点① 五个程序共享分镜映射，观察同一事件 `do_sys_openat2(dfd, filename, how)`（签名 5.10+ 稳定、本机 BTF 实证，2.4）；下贴 `book/examples/chapter-04/01-multi-observe/multi_observe.bpf.c` 全文，3.2–3.5 逐镜指认：

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

五镜对照讲解——同一事件现场，类型化程度排成谱系：typed 布局（tracepoint）→ 原始 `__u64` 数组（raw_tp）→ `pt_regs` 解包（kprobe）→ BTF 原型（fentry）→ 加尾随返回值（fexit）。分镜读数（预期输出示例 / 运行级待验证，摘自锚点① README）：

```text
  [tracepoint] count=42     last_ret=0    name=/etc/ld.so.cache
  [raw_tp   ] count=42     last_ret=437  name=(本镜不读文件名)
  [kprobe   ] count=42     last_ret=0    name=/usr/bin/ls
  [fentry   ] count=42     last_ret=0    name=/etc/ld.so.cache
  [fexit    ] count=42     last_ret=3    name=(本镜不读文件名)
```

挂载成功的镜 `count` 一致——同一事件；`last_ret` 同列不同义（fexit 存真返回值、raw_tp 存原始 `ctx[1]`，syscall 类为系统调用号 openat2=437）；`name` 列只有三镜有值——**各镜只取自己类型下「拿得到」的信息**（两值不同是快照错位：各镜存**最近一次**打开，不必同一笔）。保守口径：raw_tp 镜的目标事件是否存在以本机 tracepoint 列表为准——per-syscall 事件若无 raw_tp 对应物（静态 tracepoint 与 ftrace 事件是两条注册路径），attach 按名查找即报 ENOENT；这本身是挂载查找路径差异的教学点，其余四镜读数不受影响。

**预期输出/挂载形态**：五个 SEC 名携带全部挂载信息，loader 一个通用 `bpf_program__attach()` 挂 5 个 link（2.3「自动」列；raw_tp 镜可挂性保守口径见上方）；tracepoint 镜读数即上方第 1 行（预期输出示例 / 运行级待验证）。

**类型要点**：ctx 维度被点亮——typed 事件布局，字段有名有型；返回值被忽略、按惯例 0；helper 面属追踪族白名单（1.2 正面例）。

**与相邻类型的选择边界**：事件已有 typed 层、稳定优先选 tracepoint；新事件无 typed 层退 raw_tp（3.2）；挂点不在 tracefs 事件表用 kprobe（3.3）。**失败形态回指**：`args[]` 数值强转指针解引用＝第 3 章失败画廊 card ⑦。**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（镜 0）。

### 3.2 raw tracepoint：原始参数无类型（镜 1）

**背景问题**：事件在埋点表里但 typed 层未覆盖，或想核对 typed 层转换有无偏差——同一埋点的**原始参数**去哪拿？

**代码**：上贴全文镜 1——`SEC("raw_tp/sys_enter_openat2")` 的 `int raw_entry(__u64 *ctx)`，核心行 `v->last_ret = (__s64)ctx[1];`：只读形状、不拆字段。

**预期输出/挂载形态**：自动挂载（SEC 名即事件名；目标事件在场的保守口径见 3.1）；读数即 3.1 第 2 行——`last_ret=437` 是系统调用号**不是**返回值（预期输出示例 / 运行级待验证）。

**类型要点**：ctx 维度的**反例**被点亮——`__u64 *` 无字段可名，syscall 类原始参数形状 `(regs, id)`（内核源码 `kernel/trace/trace_syscalls.c`）；返回值被忽略；helper 面同追踪族。

**与相邻类型的选择边界**：typed 层在选 3.1；raw_tp 布局无稳定承诺才退而求其次（1.3 表）。**失败形态回指**：第 3 章失败画廊 card ⑦——原始数值更没有指针资格。**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（镜 1）。

### 3.3 kprobe：按符号动态挂入口（镜 2）

**背景问题**：要看的函数不在 tracefs 事件表里，或只是临时调试一个符号——能按名字挂到任意内核函数入口吗？

**代码**：上贴全文镜 2——`SEC("kprobe/do_sys_openat2")` 的 `int BPF_KPROBE(kp_entry, int dfd, const char *filename, struct open_how *how)`：宏按 C 原型从 pt_regs 解包参数（x86 展开，以所用架构为准）。

**预期输出/挂载形态**：自动挂载，挂载点按内核符号解析；读数即 3.1 第 3 行（预期输出示例 / 运行级待验证）；符号被内联/不存在时 attach 报错（锚点① README 同口径）。

**类型要点**：ctx 是 `pt_regs *`，宏把寄存器解包成有类型形参——「手工捞寄存器」变成「写 C 原型」；返回值被忽略；helper 面同追踪族，不依赖事件注册。

**与相邻类型的选择边界**：不依赖事件注册、符号随时可换，代价是会内联/改名（本机 BTF 中 `do_unlinkat` 缺席实证）；临时调试选它，长期生产选 fentry（3.4）。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（镜 2）。

### 3.4 fentry：BTF 类型化直入函数入口（镜 3）

**背景问题**：临时探针要转正为长期生产追踪——开销与类型安全能不能都要？

**代码**：上贴全文镜 3——`SEC("fentry/do_sys_openat2")` 的 `int BPF_PROG(fe_entry, int dfd, const char *filename, struct open_how *how)`：无 pt_regs 中转，原型即函数原型。

**预期输出/挂载形态**：tracing link 自动挂载；签名加载期按本机 BTF 核对——不存在报 ENOENT 类、不匹配报 EINVAL 类（2.4 漂移例证）；读数即 3.1 第 4 行（预期输出示例 / 运行级待验证）。

**类型要点**：ctx 的**第三种形态**（2.2 末）——BTF 类型化的 C 原型直列；类型精确**不豁免 probe 界限**，`filename` 照走 `bpf_probe_read_user_str`。

**与相邻类型的选择边界**：对 3.3——多要 `CONFIG_DEBUG_INFO_BTF` 与签名稳定，换来低开销与加载期校验；读成败看 3.5。**失败形态回指**：第 3 章失败画廊 card ⑦；签名漂移见 2.4。**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（镜 3）。

### 3.5 fexit：同型参数＋尾随返回值（镜 4）

**背景问题**：「这次调用办成了没有」——五镜里谁能直接看到返回值？

**代码**：上贴全文镜 4——`SEC("fexit/do_sys_openat2")` 的 `int BPF_PROG(fx_entry, …, struct open_how *how, int ret)`：尾随 `ret` 即目标函数返回值，`v->last_ret = ret` 落账。

**预期输出/挂载形态**：同 3.4 的 BTF 校验与挂载；读数即 3.1 第 5 行——`last_ret=3`（成功 fd）（预期输出示例 / 运行级待验证）。

**类型要点**：fentry 全部＋尾随 `int ret`——同 prog type、只差一个尾随参数（表合行/本节拆卡的理由见节首口径互认）。

**与相邻类型的选择边界**：长期统计成败分布首选 fexit（1.5 第 2 问）；kretprobe 也能读返回值，但靠架构寄存器展开、校验弱（3.8）。**失败形态回指**：同 3.4。**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（镜 4）。

### 3.6 XDP：驱动层最早时刻

**背景问题**：报文刚进网卡就要裁决——丢包、转发、抗 DDoS，协议栈加工等不起。最早的裁决点在哪？

**代码**（下贴 `book/examples/chapter-04/02-xdp-tc/xdp_count.bpf.c` 关键段；解析逻辑与 3.7 的 TC 侧逐行同构）：`SEC("xdp")` 的 `int xdp_entry(struct xdp_md *ctx)`：开头取 `ctx->data`/`ctx->data_end` 两端指针，此后每次指针推进都先 `(void *)(eth + 1) > data_end` 再解引用，恒 `return XDP_PASS;`（只观测不干预）。

**预期输出/挂载形态**：挂载走 `bpf_program__attach_xdp(prog, ifindex)`（返回 link）；flags=0 按 native 尝试、失败并不自动回退 generic（SKB）模式（显式 SKB 模式改用 `bpf_xdp_attach`，保守口径同 5.1；随内核版本以实测为准）；读数为锚点② `[xdp]` 行（预期输出示例 / 运行级待验证）；实验只应挂 veth 或 lo（章级 README 安全警告）。

**类型要点**：ctx 极简——`xdp_md` 只有 `data`/`data_end`/`data_meta` 与少量元数据，解析全手工，边界检查是 verifier 硬要求（card ② 正面用法）；返回值 `enum xdp_action`（`XDP_PASS` 在 BTF）；helper 面**没有**「当前进程」一族（card ⑥ 出处，1.2）。

**与相邻类型的选择边界**：最早时刻、只关心报文本体选 XDP；要 skb 元数据或分方向退 TC（3.7）。**失败形态回指**：第 3 章失败画廊 card ② 与 card ⑥。**独立示例**：[`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/)（XDP 侧）。

### 3.7 TC：协议栈入口与 skb 元数据

**背景问题**：要协议栈加工后的 skb 元数据（`mark`/`len`/`protocol`），或者要分 ingress/egress 方向——报文已进协议栈，挂哪？

**代码**（下贴 `book/examples/chapter-04/02-xdp-tc/tc_count.bpf.c` 关键段；与 `xdp_count.bpf.c` 逐行同构，差异只剩 SEC、ctx 与返回值）：程序自带 `#define TC_ACT_OK 0`（uapi `pkt_cls.h` 的 #define，不在 BTF/vmlinux.h），`SEC("tc")` 的 `int tc_entry(struct __sk_buff *skb)` 解析逻辑与 XDP 侧逐行同构，恒 `return TC_ACT_OK;`（与 `XDP_PASS` 对照）。

**预期输出/挂载形态**：TC 没有 link 对应物——走 netlink 模型（`bpf_tc_hook_create` 建 clsact＋`bpf_tc_attach` 绑 ingress，2.4）；读数为锚点②两行并排，`udp` 一致、`packets` 接近但不必然相等（预期输出示例 / 运行级待验证）。

**类型要点**：ctx 换 `__sk_buff`——两层皮（2.2）申报面差异：协议栈字段现成可读，`xdp_md` 里没有；返回值 `TC_ACT_*` 是 uapi `#define` 不在 BTF（2.5 值域校验按值不按符号）。

**与相邻类型的选择边界**：对 3.6——时机换字段；对 3.10——同为 `__sk_buff`，TC 挂 qdisc/filter、cgroup/skb 挂 cgroup fd。**失败形态回指**：第 3 章失败画廊 card ②。**独立示例**：[`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/)（TC 侧）。

### 3.8 kretprobe：返回处探针与寄存器读数（画廊①）

**背景问题**：入口探针看不见「办成了没有」——想给一条系统调用做成败分布，返回值去哪读？

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/01-kretprobe/kretprobe.bpf.c` 关键段）：`SEC("kretprobe/do_sys_openat2")` 的 `int BPF_KRETPROBE(krp, int ret)`，分桶行 `__u32 key = ret >= 0 ? 0 : 1;`——0=成功（fd）、1=失败（负错误码）。

**预期输出/挂载形态**：自动挂载（KPROBE 的 retprobe 变体，2.3；tracefs 生成 `<事件>_ret`）；跑几条 `ls`/`cat` 看成功 key 增长，构造失败调用看失败 key（预期输出示例 / 运行级待验证）。

**类型要点**：KPROBE 类型、返回处触发——与 3.3 同身份不同工位（1.1 实例）；返回值经 `BPF_KRETPROBE` 按架构展开读取（x86 由 `PT_REGS_RC`，以所用架构为准）；`ret>=0` 为 fd、负值为错误码。

**与相邻类型的选择边界**：对 3.5——同样读返回值，不依赖 BTF 校验、类型校验弱；与 3.3 入口/返回互补。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`03-type-gallery/01-kretprobe/`](../examples/chapter-04/03-type-gallery/01-kretprobe/)。

### 3.9 uprobe：探针下探用户态（画廊②）

**背景问题**：问题不在内核，在用户态库或二进制的某个函数——探针能出内核吗？

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/02-uprobe/uprobe.bpf.c` 关键段）：`SEC("uprobe")` 的 `int up(struct pt_regs *ctx)`——SEC 完整形态是 `uprobe/<二进制路径>:<偏移>`，路径与偏移属挂载期信息，编译期 SEC 串只是标记。

**预期输出/挂载形态**：显式挂载 `bpf_program__attach_uprobe(…, pid, binary_path, func_offset)`（偏移可由 `nm -D`/`readelf -s` 查）；命中计数随挂点函数调用增长（预期输出示例 / 运行级待验证）。

**类型要点**：KPROBE 的用户态变体——ctx 仍是 `pt_regs`（被打断处的用户态现场）；用户内存读必须走 probe 系辅助函数。

**与相邻类型的选择边界**：对 3.3——用户态 vs 内核态符号；`uretprobe` 是其「办成了没有」的对称（画廊不单列）。**失败形态回指**：第 3 章失败画廊 card ⑦；路径/偏移错 attach 才报错（2.3）。**独立示例**：[`03-type-gallery/02-uprobe/`](../examples/chapter-04/03-type-gallery/02-uprobe/)。

### 3.10 cgroup/skb：按容器划线的网络 verdict（画廊③）

**背景问题**：准入要按容器/进程组划线而非按网卡——报文判决能跟着 cgroup 走吗？

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/03-cgroup-skb/cgroup_skb.bpf.c` 关键段）：`SEC("cgroup/skb")` 的 `int cg(struct __sk_buff *skb)`——`bpf_get_current_cgroup_id()` 在场（独有维度见要点），恒 `return 1;`（1=放行 / 0=丢弃，非 `TC_ACT_*`）。

**预期输出/挂载形态**：attach 对象是 cgroup fd（`bpf_program__attach_cgroup`，ingress/egress 两方向）；per-CPU map 读数按 CPU 分列、总数＝各 CPU 相加（预期输出示例 / 运行级待验证）。

**类型要点**：三项同时被点亮——ctx 与 TC 同形 `__sk_buff`；返回值裸二值且是**加载期校验规则**（2.5：`return 2;` 加载即拒）；helper 面 `bpf_get_current_cgroup_id()` 为本类型独有。

**与相邻类型的选择边界**：对 3.7——挂载对象 cgroup vs qdisc；对 3.11——同 ctx、verdict vs 拷贝字节数（1.2 对照本体）。**失败形态回指**：第 3 章失败画廊 card ①；「`return 0;` 进 verdict 类＝全丢弃」见 1.2。**独立示例**：[`03-type-gallery/03-cgroup-skb/`](../examples/chapter-04/03-type-gallery/03-cgroup-skb/)。

### 3.11 socket filter：返回「拷几字节」的观测类（画廊④）

**背景问题**：tcpdump 怎么实现？过滤程序挂到某个 socket，内核把过路报文先喂给它——返回值决定拷什么给观察者。

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/04-socket-filter/socket_filter.bpf.c` 关键段）：`SEC("socket")` 的 `int sf(struct __sk_buff *skb)`，恒 `return 0;`——0=不投递给观测 socket，包本身不受影响。

**预期输出/挂载形态**：`setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd, …)`——特有的 setsockopt 挂载，无 bpf_link 对应物；命中计数与实际投递无关（预期输出示例 / 运行级待验证）。

**类型要点**：最古老的程序类型；ctx 与 TC/cgroup-skb 同形；返回值＝**接受拷贝的字节数**而非 verdict（0=该包对它不可见，报文照常走协议栈，1.3 表「非 verdict」本体）；只读观测，不能丢包不能改包。

**与相邻类型的选择边界**：对 3.10——同 ctx 不同返回语义的最短对照；想在 socket 维度干预走 3.12/3.13。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`03-type-gallery/04-socket-filter/`](../examples/chapter-04/03-type-gallery/04-socket-filter/)。

### 3.12 sockops：socket 级生命周期回调（画廊⑤）

**背景问题**：XDP/TC 站在报文级，socket 级状态——两端地址端口、TCP 状态、`snd_cwnd`——谁看得见？

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/05-sockops/sockops.bpf.c` 关键段）：`SEC("sockops")` 的 `int so(struct bpf_sock_ops *ops)` 按事件码 `switch (ops->op)` 判别——`BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB`/`BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB`（服务端/客户端连接建立）各计一个 key，`default: return 0;` 其余事件交还默认路径。

**预期输出/挂载形态**：attach 到 cgroup（`BPF_CGROUP_SOCKOPS`），事件由内核 TCP 状态机驱动；`nc`/`curl` 触发两类建立计数（预期输出示例 / 运行级待验证）。

**类型要点**：ctx 维度被点亮——`bpf_sock_ops` 的 socket 级字段直接可读（报文级类型没有的视角）；返回值「1=已处理 / 0=交还默认」是**画廊教学约定**：内核实际消费面较窄——取值类回调（出处 `include/net/tcp.h` 的 `tcp_call_bpf()`）返回 0 时取 `sock_ops.reply` 传入值、非 0 一律视为「未提供」，纯事件类回调多忽略；随内核版本演进以所用内核源码为准——完整保守标注见 1.2/1.3 表，此处精简回指。

**与相邻类型的选择边界**：对 3.13——sockops 收 socket 进 sockmap、sk_msg 判决转发，配对成体系；对 3.10——socket 级 vs 报文级。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`03-type-gallery/05-sockops/`](../examples/chapter-04/03-type-gallery/05-sockops/)。

### 3.13 sk_msg：sockmap 上的转发判决（画廊⑥）

**背景问题**：sockmap 把两端 socket 收进 map 之后，报文在 socket 间转发时谁给放行判决？

**代码**（下贴 `book/examples/chapter-04/03-type-gallery/06-sk-msg/sk_msg.bpf.c` 关键段）：`SEC("sk_msg")` 的 `int sm(struct sk_msg_md *msg)`——ctx 是数据内存区间与两端 socket 元数据，恒 `return __SK_PASS;`（真实重定向走 `bpf_msg_redirect_hash`，本例从简）。

**预期输出/挂载形态**：attach 对象是 sockmap/sockhash **map** 而非 cgroup（`bpf_prog_attach(…, BPF_SK_MSG_VERDICT)`）；**sockmap 未挂对应 sockops、socket 未收进 map 前不会被触发**——体系性前提而非程序缺陷；判决计数读数（预期输出示例 / 运行级待验证）。

**类型要点**：返回值维度被点亮——`__SK_PASS`/`__SK_DROP`（uapi `enum sk_action`，在 BTF）；判决类失败分支回 `__SK_DROP`（fail-closed），与观测类「失败不干预」（画廊③④）对照——失败分支返回什么跟着职责走。

**与相邻类型的选择边界**：与 3.12 配对才生效；体系深入见第 22/23 章。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`03-type-gallery/06-sk-msg/`](../examples/chapter-04/03-type-gallery/06-sk-msg/)。

### 3.14 LSM 与 netfilter：两类 verdict 决策点（画廊⑦⑧）

**背景问题**：「打开这个文件行不行」「过这条防火墙规则行不行」——对操作与对过点报文做裁决的是哪两类？同为 verdict 决策点，合并对照。

**代码**：下贴 [`07-lsm/lsm.bpf.c`](../examples/chapter-04/03-type-gallery/07-lsm/) 关键段——`SEC("lsm/file_open")` 的 `int BPF_PROG(lf, struct file *file)`，恒 `return 0;`（0=允许，只审计不拦截）；下贴 [`08-netfilter/netfilter.bpf.c`](../examples/chapter-04/03-type-gallery/08-netfilter/) 关键段——程序自带 `#define NF_ACCEPT 1`，`SEC("netfilter")` 的 `int nf(struct bpf_nf_ctx *ctx)` 恒 `return NF_ACCEPT;`。

**预期输出/挂载形态**：LSM 三前提缺一 load 即拒——root＋`CONFIG_BPF_LSM=y`＋启动参数含 `lsm=bpf`（本机配置实证 =y、启动参数未启用，运行级待验证；`00-env/` 自检含检查项），load 后即进入该 hook 裁决链；netfilter 前提最轻（root＋内核 6.6+），`bpf_program__attach_netfilter(opts)` 显式指定协议族/hook 号/优先级；两目录读数各一行（预期输出示例 / 运行级待验证）。

**类型要点**：LSM 的 ctx 是 hook 原型参数直列——`struct file *` 不经转接、内核对象直接可读，安全类的最大差别；与 fentry 镜共用 `BPF_PROG` 宏；逐 hook 裁决语义以 `include/linux/lsm_hook_defs.h` 为准。netfilter 的 ctx 是结构体 `bpf_nf_ctx{state, skb}`（本机 BTF 实证）；`NF_ACCEPT`/`NF_DROP` 是 uapi `#define` 不在 BTF（同 `TC_ACT_OK`）。

**与相邻类型的选择边界**：合卡理由即同为 verdict 决策点——LSM 裁决「内核对象上的操作」（hook 全局生效），netfilter 裁决「过钩子点的报文」（显式选点）；按容器划线回 3.10；完整示例见第 9 章 `09-netfilter-go/`。**失败形态回指**：第 3 章失败画廊 card ①。**独立示例**：[`03-type-gallery/07-lsm/`](../examples/chapter-04/03-type-gallery/07-lsm/) 与 [`08-netfilter/`](../examples/chapter-04/03-type-gallery/08-netfilter/)。

**教师视角**：画廊为什么用「同一事件五副眼镜」开篇？1.3 总表把差异压进了表格，而 ctx 差异、宏展开差异、读数差异不看实物永远停在表格层面——五个程序贴着同一事件逐镜 diff，三元组才从「背诵项」变成「看得见的字段」。读者最容易卡住的地方，是把「SEC 名字写错」当「类型选错」排错：SEC 串是 ELF 里的字符串，clang 不校验内容（2.3），拼错事件名、少一段、路径与偏移缺失时编译全过，load/attach 才报「查无此事件/符号」——现象与「类型选错」高度相似。分辨三步：先逐字符核对 SEC 串与 2.3 前缀表，再核对挂载目标是否存在（tracefs 事件目录、BTF 符号、二进制路径与偏移），最后才回头怀疑类型选型。

**学生动手**：

1. 逐目录 `make` 画廊 8 类（[`03-type-gallery/`](../examples/chapter-04/03-type-gallery/) `01-`–`08-`，compile-only，验收＝8 个 `.bpf.o` 产出）；读各目录 README「挂载方式与前提」，把加载前提抄进笔记。
2. 跑锚点①读分镜计数：`make && sudo ./multi_observe`，另开终端跑几条 `ls`/`cat`，核对各行 `count` 一致、`last_ret` 三种语义（raw_tp 挂载性保守口径见 3.1）（预期输出示例 / 运行级待验证）。
3. 把 14 类的返回值语义抄进笔记——每类一行「返回什么算放行/算拷贝/被忽略」，对照 1.3 表自查，口径对不上即理解有洞。

检查点——先自答，再看提示：

1. 同一个 `return 1;`，在 cgroup/skb 与 sockops 里含义分别是什么？（提示：前者是网络 verdict——1 放行/0 丢弃，加载期值域校验（2.5）；后者是回调应答——1=已处理、0=交还默认，画廊教学约定、内核实际消费面较窄（3.12 与 1.2）。）
2. 3.4/3.5 拆两卡而表合一行，3.14 合一卡而表各一行——本章靠什么保证「数 card 得 14、数表行（perf event 单列行不计）也得 14」？（提示：完整口径见三节开头互认——表按程序类型合、card 按触发时机拆/按职责合，总数相同、维度不同，对位看维度勿数条数。）
3. 一个 sk_msg 程序加载成功却从不触发，第一步查什么？（提示：体系性前提——sockmap 里有没有 socket（sockops 是否已收进连接）、是否 attach 到正确的 sockmap/sockhash（3.13）；「报文没到判决点」先于「程序写错」。）

## 四、实战：为任务选对类型并挂上钩子

一至三节走完类型系统、内核机制与十四类实物，本节收拢成本章最终产出：**为任务选对类型，并把它挂上钩子**。5.1 按五层把两个集成锚点的环境备齐（层序沿第 3 章四节先例）；5.2 以四段式走读两个锚点的 loader 代码与读数，把 1.4 决策树兑现成可操作的三步法。

### 5.1 五层环境：两个锚点的运行前提

**第一层：依赖环境。**内核 5.10+ 且 `CONFIG_DEBUG_INFO_BTF=y`（`vmlinux.h` 由 BTF 生成，缺失则编译停步，自建见附录 C）；tracefs（`/sys/kernel/tracing`）是 tracepoint/raw_tp 挂载点的运行时载体，需 root 读取；LSM 画廊条目另有 `CONFIG_BPF_LSM=y`＋启动参数含 `lsm=bpf` 双前提（本机前者=y、后者未启用）；netfilter 需内核 6.6+。权限矩阵两档：非 root 只能编译级（本机 `unprivileged_bpf_disabled=2`）；root 可加载与挂载（sudo 默认已含）；非 root 特权档需 `CAP_BPF`＋`CAP_NET_ADMIN`。自检：`bash book/examples/chapter-04/00-env/check-env.sh`（[`00-env/`](../examples/chapter-04/00-env/)，无 root 也可运行）；前两段本机全 OK，后两段实测如下（本地验证）：

```text
== 权限分级（加载级/运行级） ==
WARN  非 root 且无 CAP_BPF（unprivileged_bpf_disabled=2）：
      - 可以完成：编译级验证（make / scripts/verify-build.sh）
      - 不能完成：加载与挂载 XDP/TC/tracepoint、LSM 程序、bpftool prog/map 观察（需 sudo）
OK    lo 接口存在（XDP/TC 锚点程序的推荐挂载目标）
== 第 4 章特有检查（tracefs 与 LSM） ==
WARN  tracefs 需 root 读取，加载级验证时使用（/sys/kernel/tracing/events 不可见：未挂载或非 root 不可遍历；编译级不受影响）
OK    CONFIG_BPF_LSM=y：LSM 类型运行前提在内核侧满足（还需 lsm=bpf 启动参数与 root）
INFO  启动参数未见 lsm=bpf：LSM 程序运行级还需以 lsm=bpf 启动内核并配合 root（不影响编译级）

结论：编译级依赖齐备；加载级/运行级能力（含 tracefs 可读性与 lsm=bpf）见上方分级输出。
```

**第二层：编译环境。**四件工具一条链：clang/LLVM 18.x 以 `-target bpf` 编 BPF C，libbpf-dev 提供头文件与库，bpftool 生成 `vmlinux.h` 与 skeleton，gcc＋make 构建 loader。版本自检（本地验证）：

```bash
clang --version | head -1          # Ubuntu clang version 18.1.3
bpftool version                    # bpftool v7.7.0（using libbpf v1.7 为其自带运行时）
dpkg -s libbpf-dev | grep ^Version # Version: 1:1.3.0-2build2——编译 loader 以此为准
```

**第三层：部署环境。**追踪类是「挂载即部署」：tracepoint/raw_tp/kprobe/fentry/fexit 的 SEC 名携带全部挂载信息（raw_tp 可挂性保守口径见 3.1），load 通过后 attach 即生效（2.3「自动」列）——没有规则下发，loader 进程本身就是全部控制面。XDP/TC 需网络接口作挂载目标，本章固定 lo（或自建 veth）。安全警告（本章示例统一措辞）：

> XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。

XDP 挂载模式按保守口径：flags=0 在主流内核按 native 尝试、失败并不自动回退 generic（SKB）模式——lo 有无驱动原生路径随内核版本，实验时需显式指定 SKB 模式，或以所用内核实测为准。可操作路径：现有锚点 loader 用的 `bpf_program__attach_xdp()` 不带 flags 入口，需显式 SKB 模式时应改用 `bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_SKB_MODE, NULL)`（libbpf 提供，flags 即 `XDP_FLAGS_*` 位图；随 libbpf 版本演进，以所用头文件为准）。多镜同挂注意点：fentry/fexit 按本机 BTF 核签名、漂移即拒（2.4）；raw_tp 的 per-syscall 事件可能无对应物、attach 报 ENOENT（3.1 保守口径）；锚点②同接口同模式已挂他程序、TC 同 handle/priority 均会 attach 失败（锚点② README）。

**第四层：运行环境。**两锚点各一条命令链（Ctrl-C 退出即卸载：link 逐个销毁、TC filter 显式 detach——第 2 章 4.4 生命周期回指）：

```bash
# book/examples/chapter-04/01-multi-observe/：追踪类锚点，五镜对照
cd book/examples/chapter-04/01-multi-observe && make && sudo ./multi_observe
# book/examples/chapter-04/02-xdp-tc/：网络类锚点，XDP+TC 同挂 lo
cd book/examples/chapter-04/02-xdp-tc && make && sudo ./xdp_tc
```

制造流量：锚点①另开终端跑几条 `ls`/`cat`（凡打开文件的调用都走统一事件）；锚点②用走 lo 的流量，如 `ping 127.0.0.1`、`curl 本机服务`。

**第五层：运行效果。**锚点①分镜读数（预期输出示例 / 运行级待验证，摘自锚点① README）：

```text
五副眼镜已挂上 openat2，随便跑几条命令（如 ls、cat）观察计数……
  [tracepoint] count=42     last_ret=0    name=/etc/ld.so.cache
  [raw_tp   ] count=42     last_ret=437  name=(本镜不读文件名)
  [kprobe   ] count=42     last_ret=0    name=/usr/bin/ls
  [fentry   ] count=42     last_ret=0    name=/etc/ld.so.cache
  [fexit    ] count=42     last_ret=3    name=(本镜不读文件名)
----
```

解读按 3.1 保守口径条件化：**挂载成功的镜** `count` 一致——同一事件；raw_tp 镜若因第三层的 ENOENT 未挂上，loader 失败分支即中止——把该镜移出 `progs[]`/`LENS_NAMES` 并将两处循环上界同步减一（即学生动手第 2 项的逆操作），其余四镜读数不受影响；`last_ret` 同列不同义、`name` 列只有三镜有值——各镜只取自己类型下拿得到的信息。锚点②双计数对照（预期输出示例 / 运行级待验证，摘自锚点② README）：

```text
XDP 与 TC 已同挂 lo，制造流量（如 ping 127.0.0.1、curl 本机服务）观察对照……
  [xdp      ] packets=128      udp=12
  [tc       ] packets=126      udp=12
----
```

`udp` 两行应一致（同一段流量两挂载点各记一笔）；`packets` 接近但不必然相等——两挂载点可见报文的时机与集合有细微差别（检查点第 2 问）。验证状态约定沿用章首：凡加载与运行的效果一律「预期输出示例 / 运行级待验证」，编译级结果才是本机可验证的事实。

### 5.2 代码走读：三步法与两条挂载路线

**教师视角**：选型是本章的最终产出，但「选对」不靠背表——1.4 决策树给的是路线，落地靠三步法：**要什么数据**（事件字段、函数返回值、报文本体还是 skb 元数据）→ **谁产生它**（内核函数、静态埋点、网卡驱动还是协议栈入口）→ **哪个类型的 ctx 给它**（回 1.3 表 ctx 列对三元组）。两个锚点即两次完整演练：观测 openat2 是追踪任务，落到锚点①的五镜对照；数 lo 上的包是网络任务，落到锚点②的双 ctx 对照。读者容易卡住的地方：选型只回答「程序是什么」，第二问「怎么挂」紧随其后——2.3 的自动/显式之分正有两个极端实物：锚点①五个 SEC 全自动、一循环挂完；锚点②一 loader 并存两条挂载路线。

**核心代码走读。**先看锚点①的 attach 循环（`book/examples/chapter-04/01-multi-observe/multi_observe.c`，progs[] 五项赋值略）：

```c
    for (int i = 0; i < 5; i++) {
        links[i] = bpf_program__attach(progs[i]);
        if (!links[i]) {
            fprintf(stderr, "attach %s 失败（需 root/特权档；fentry 还需签名匹配）\n",
                    LENS_NAMES[i]);
            err = 1;
            goto out;
        }
    }
```

五个 SEC 全在 2.3 的「自动」列：挂载信息编译期就在 SEC 名里，loader 不含任何逐类型挂载代码——一个通用 `bpf_program__attach()` 让 libbpf 按 SEC 前缀分派到各自路径（tracefs 事件、内核符号、tracing link、原始 tracepoint）；任何一镜失败即 `goto out` 统一销毁已挂 link，是 2.4 末「逐个挂载、失败即清理」的实现本体。选型收益在此兑现：换观测对象只需改 SEC 名，循环一行不动——「类型决定挂载方式」从表格变成五行循环。

再看锚点②的两条 attach 路径（`book/examples/chapter-04/02-xdp-tc/xdp_tc.c`，错误分支略）：

```c
    /* XDP 挂载：驱动层 hook，返回 bpf_link */
    xdp_link = bpf_program__attach_xdp(xdp->progs.xdp_entry, (int)ifindex);

    /* ……中略：XDP link 判空与报错分支…… */

    /* TC 挂载：先建 clsact qdisc（已存在返回 -EEXIST，视为成功），再绑 ingress */
    {
        LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = ifindex,
                    .attach_point = BPF_TC_INGRESS);
        LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 1,
                    .prog_fd = bpf_program__fd(tc->progs.tc_entry));
        if (bpf_tc_hook_create(&hook) != 0 && errno != EEXIST) { …略… }
        hook.attach_point = BPF_TC_INGRESS;
        if (bpf_tc_attach(&hook, &opts)) { …略… }
    }
```

两侧解析逻辑逐行同构（3.6/3.7），差异全在「站在哪、怎么挂」：XDP 一次调用返回 `bpf_link`——fd 化的挂载关系，进程退出即卸载（2.4）；TC 先 `bpf_tc_hook_create` 建 clsact、再 `bpf_tc_attach` 绑 ingress，退出时 `bpf_tc_detach` 显式摘 filter、qdisc 留置无害——末尾不对称的清理代码就是两条路线生命周期的实物对照。三步法第③步在网络类的直接后果：ctx 选 `xdp_md` 还是 `__sk_buff`，挂载 API 跟着分叉。

**学生动手**：

1. 跑通两锚点并读数：按 5.1 第四层命令链运行两锚点并制造流量，对照第五层两块输出读数（预期输出示例 / 运行级待验证）。
2. 给锚点①加第六面镜：把 3.8 的 kretprobe 镜并入 `multi_observe.bpf.c`——新增 `SEC("kretprobe/do_sys_openat2")` 的 `BPF_KRETPROBE` 镜（key=5，`v->last_ret = ret`），分镜映射 `max_entries` 改 6，loader 侧三处数组 `LENS_NAMES`/`progs[]`/`links[]` 各加一项，attach 与读数两处循环上界同步加一。预期编译通过（本机可验）；加载级经 tracefs 生成返回事件（3.8，运行级待验证）。
3. 观察失败路径：跑一条必败的打开（如 `cat /no-such-file`），fexit 镜 `last_ret` 变负错误码（-2=ENOENT）、成功路径为 fd 正值（预期输出示例 / 运行级待验证）——画廊①的 `ret >= 0` 分桶在这里即「盯住 fexit 一列」。

检查点——先自答，再看提示：

1. 锚点①跑起来后，为什么五镜计数不完全相等？（提示：分列作答。`count` 列：只有**挂载成功的镜**才计数——raw_tp 镜可能因事件无对应物而挂载失败（3.1 保守口径）；挂载成功的镜同读一事件、读稳后一致，瞬时个位数差来自**计数时点**错位（每秒快照）。`name` 列：raw_tp/fexit 两镜**不读文件名**；`last_ret` 列：同列不同义——「不完全相等」多数在这两列。）
2. 锚点②的 `udp` 两行一致、`packets` 接近但不必然相等，各说明什么？（提示：`udp` 一致——同一段流量两个挂载点各记一笔；`packets` 有差——XDP 站驱动层、TC 站协议栈入口，可见报文的时机与集合有细微差别（3.6/3.7），以实测为准。）
3. 任务改为「统计本机 TCP 连接建立次数」，用三步法落到哪里？（提示：要什么数据——连接建立事件；谁产生它——内核协议栈 connect 路径；哪个 ctx——追踪族看内核函数，fentry/kprobe 挂 `tcp_v4_connect` 一类符号（1.4「内核函数」叶子），符号以本机 BTF 为准。）

**独立示例**：[`01-multi-observe/`](../examples/chapter-04/01-multi-observe/)（追踪类锚点：五镜对照与排错表）与 [`02-xdp-tc/`](../examples/chapter-04/02-xdp-tc/)（网络类锚点：两种 ctx 对照与两条挂载路线）；环境自检见 [`00-env/`](../examples/chapter-04/00-env/)；cgroup/LSM 等画廊类的部署前提已在三节各 card 的「预期输出/挂载形态」覆盖（[`03-type-gallery/`](../examples/chapter-04/03-type-gallery/)），不重复展开。

## 可视化：类型谱系与挂载路径

一节的两层模型与四节的五镜实操，各收成一张文本图。两图均为示意，图内读数与形态沿章首验证状态约定（预期输出示例 / 运行级待验证）。

**图 1：`bpf_prog_type → bpf_attach_type → 挂载点` 三层图**（1.1 两层模型的图形版）：

```text
第一层  bpf_prog_type（身份）：ctx 布局＋辅助函数面＋返回值语义
═══════════ 加载层（BPF_PROG_LOAD，第 3 章验证器守门）═══════════
第二层  bpf_attach_type＋挂载参数（工位）：哪类对象、哪个方向、什么模式
═══════════ 挂载层（BPF_LINK_CREATE／attach 一族，2.4）═══════════
第三层  挂载点（事件现场）：tracefs 事件｜内核符号｜cgroup｜网卡｜sockmap｜安全 hook
```

**kprobe 族展开例——一个身份、三条挂载路线**（1.1 实例第一条）：

```text
BPF_PROG_TYPE_KPROBE（ctx 同为 pt_regs，1.3 表）
  ├─ SEC("kprobe/<符号>")     → 内核函数入口（按符号解析，3.3）
  ├─ SEC("kretprobe/<符号>")  → 同一符号的返回处（tracefs 生成返回事件，3.8）
  └─ SEC("uprobe")            → 用户态二进制＋偏移（挂载期补参数，显式，3.9）
```

怎么读图 1：三层自上而下对应挂载的三个问题——「程序是什么」（第一层，加载进闸）、「挂到哪类对象、哪个方向」（第二层，挂载命令的属性）、「事件现场在哪」（第三层，触发源）。两条分隔线就是两次拒绝的落点：身份错在第一层被验证器拒掉（第 3 章），工位错在第二层才报（排错节）——「加载层只需要知道身份，挂载层才知道工位」（1.1）在图上即两条线。多数「换挂载点」在图上是跨行移动：身份跟着换、代码重写；kprobe 族是同一身份内的例外面——三条路线只动第二、三层，ctx 一个字不改。

**图 2：同一事件五副眼镜的数据流**（锚点①，3.1 全文代码的图形版）：

```text
用户进程 openat("/etc/ld.so.cache")
   ▼
进入内核 → do_sys_openat2(dfd, filename, how)（签名本机 BTF 实证，2.4）
   ▼
同一调用路径上的五个钩子（SEC 名携带挂载信息，自动挂载，2.3）
   [tracepoint] sys_enter_openat2   ctx=typed 布局（args[1]=filename）
   [raw_tp    ] sys_enter_openat2   ctx=原始 u64 数组（syscall 类 (regs, id)）
   [kprobe    ] do_sys_openat2      ctx=pt_regs（BPF_KPROBE 解包）
   [fentry    ] do_sys_openat2      ctx=BTF 原型直列
   [fexit     ] do_sys_openat2      ctx=BTF 原型＋尾随 ret
   ▼
五镜各写分镜映射自己的槽（ARRAY 5 槽：count／last_name／last_ret）
   ▼
loader（multi_observe.c）每秒快照打印五镜读数（5.1 第五层）
```

怎么读图 2：五个钩子是**并列的五次挂载**，不是流水线五站——同一次 openat2 让五段程序各执行一遍、互不干扰，唯一交汇是分镜映射，loader 只从 map 读数。五段代码读同一现场、拿五种 ctx——1.3 表 ctx 列的图形版，类型化程度排成谱系（3.1 五镜对照讲解）；`count` 一致与 `name` 三镜有值都是**条件口径**——挂载成功的镜（raw_tp 镜的保守口径见 3.1）。

## 常见问题与排错

5.1 备齐环境、5.2 走通正常路径，本节收「非正常路径」：把全章触碰过的类型规则反转成速查表。口径沿第 3 章排错节：**表中日志均为预期日志 / 运行级待验证**（具体文本随内核与 libbpf 版本演进），查表认「症状 → 规则 → 修复」的映射，不背文本。

| 症状（预期日志 / 运行级待验证） | 触碰的类型规则 | 修复方向 |
|---|---|---|
| SEC 名写错，加载报 `unknown prog type`／`unknown attach type` 一类（教学示例口径） | SEC 前缀决定程序类型与挂载类型（2.3）——前缀解析不出合法值，clang 零校验、加载期才爆 | 逐字符对照 2.3 前缀表核对 SEC 串 |
| ctx 字段访问被拒：`invalid bpf_context access off=… size=…` | 字段不在本类型 `is_valid_access` 申报表内——两层皮申报面（2.2；机制正典第 3 章） | 回 1.3 表 ctx 列核对字段面；越界数据换 probe 系辅助函数（第 3 章界限） |
| fentry/fexit 加载报 ENOENT 类：目标函数不在本机 BTF——`do_unlinkat` 因内联缺席即本机实证 | fentry/fexit 原型直接依赖本机 BTF 签名（2.4） | `bpftool btf dump` 核对在场性（2.4 两步命令）；换未内联符号 |
| fentry/fexit 加载报 EINVAL 类：签名不匹配——`vfs_unlink` 第 4 参本机为 `struct delegated_inode *`、上游 6.x 为 `struct inode **`，漂移实证 | 同上：签名漂移即拒——校验在干活，不是 bug（2.4） | 按本机 BTF 改写程序原型；跨版本部署预期重验 |
| attach 报 ENOENT／「查无此事件」：tracefs 缺对应事件；raw_tp 的 per-syscall 事件可能无 raw_tp 对应物（静态 tracepoint 与 ftrace 事件两条注册路径） | 挂载点按名查找，未注册即找不到——若遇 ENOENT，正是挂载查找路径差异的教学点（条件化口径，正典 3.1） | 挑本机 tracefs 事件表实际存在的事件，或换 tracepoint 路线 |
| LSM 程序 load 被拒（本机：`CONFIG_BPF_LSM=y` 已满足、启动参数未含 `lsm=bpf`） | 三前提缺一不可：root＋`CONFIG_BPF_LSM=y`＋`lsm=bpf`（1.3 表；`00-env/` 检查项） | 以 `lsm=bpf` 启动内核并配合 root；无特权退回编译级 |
| XDP attach 失败（驱动无 native 支持一类） | flags=0 按 native 尝试，失败**并不自动回退** generic（SKB）模式（5.1 保守口径） | 显式 SKB 模式：`bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_SKB_MODE, NULL)`（libbpf 1.3 头文件实证，随版本演进以所用头文件为准） |
| 加载报 `-EPERM`（非 root 且 `unprivileged_bpf_disabled=2`） | 权限档位先于一切类型规则——程序没进验证（第 3 章排错节第③组判别流程） | root／特权档；无特权环境退回编译级自查（5.1 权限矩阵） |

判别流程一句话：先问权限（`-EPERM`——没进验证）、再问挂载目标（SEC 前缀与事件／符号在场性——「查无此物」）、最后读类型规则（ctx 申报、白名单、BTF 签名）。与第 3 章排错节叠用：那边精读「验证器拒绝」的日志信号，这边把「类型选错／挂错」的症状归位；「SEC 写错」与「类型选错」现象高度相似，分辨三步见三节末教师视角。

## 课后扩展任务

四节的三步法走通了「选对」的路径，两项课后扩展任务再练两个方向：任务 1 先选型后论证，任务 2 从错位诊断归位——与第 3 章课后任务「修复＋诊断」同构，考察面换成类型系统。任务表如下（与 [`exercises/`](../examples/chapter-04/exercises/) 总览表逐字一致；两项参考实现均已落地，总览见 [`README`](../examples/chapter-04/README.md)）：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 选型决策 | 三个场景（容器内文件打开审计、TCP 连接建立观测、内核函数入口参数抓取）各选程序类型与挂载点并给出选型论证；参考实现按正确选型合并为单个可编译对象 | `book/examples/chapter-04/exercises/01-type-selection/` |
| 2 | 类型错位修复 | 一段报文解析逻辑被放进 kprobe 程序且 ctx 声明为 `xdp_md`——编译通过但加载预期被拒；诊断并把类型与 SEC 归位 | `book/examples/chapter-04/exercises/02-type-mismatch/` |

**任务 1（选型决策）**：三个场景各对应 1.4 决策树的不同叶子，先写选型论证再动代码——论证须过两道工序：三元组三问（1.2）与「怎么挂」（2.3 自动／显式之分）；「TCP 连接建立观测」与 5.2 检查点第 3 问同源，可直接对照三步法落点。参考实现见 [`exercises/01-type-selection/`](../examples/chapter-04/exercises/01-type-selection/)。

**任务 2（类型错位修复）**：报文解析逻辑放进 kprobe 程序、ctx 却声明为 `xdp_md`——三元组 ctx 项错位（1.2 常见失败点小结）：字段面对不上，编译器无从比对、加载期才拒（1.4 教师视角「三道关卡都不在编译期」）；按排错节判别流程定位，把类型与 SEC 一并归位。参考实现见 [`exercises/02-type-mismatch/`](../examples/chapter-04/exercises/02-type-mismatch/)。

**防剧透约定**沿第 3 章先例：任务描述与提示在前、参考实现思路独立成节放在后部；构建与主示例同构（各目录 `make`），验证状态以各目录 README 为准（编译级／运行级口径见 5.1）。

## 总结

本章正面回答了导读提出的问题——**同一个 BPF 程序为什么不能到处挂？** 答案落在类型系统上：程序类型决定三元组（ctx 布局、辅助函数面、返回值语义，1.2），三元组跟着挂载点走；attach type 再把同一类型下的「方向与工位」细分（1.1）。这套决定不在文档约定里、在内核实现里：注册表编译期展开每类型的 ops（2.1）、两层皮＋加载期改写保住 UAPI 稳定（2.2）、SEC 前缀解析落进 ELF（2.3）、`bpf_link` 与 BTF 签名校验管住挂载（2.4）、返回值校验收口输出侧（2.5）。现场与产出各就其位：三节十四类画廊逐类 diff，四节把选型收成三步法，可视化与排错收成两张图、一张速查表。

第 2、3 章埋下的预告至此全部收口：

| 预告 | 埋线 | 本章兑现 |
|---|---|---|
| ① | 第 2 章 1.4 末：完整谱系随内核版本增长，「第 4 章逐类展开」 | 1.3 十四类总对比表＋三节画廊逐类走读 |
| ② | 第 3 章末桥：逐类展开，随身带 2.3 类型表（字典）与排错速查表（地图） | 字典＝1.3 表＋2.3 前缀表；地图＝排错节速查表与第 3 章「信号 → 根因」映射叠用 |
| ③ | 第 3 章失败画廊 card ⑥：`bpf_get_current_pid_tgid()` 不在 XDP 白名单的反例 | 1.2 正面版——追踪族白名单之内可用，正反拼合「helper 面跟着执行现场走」 |

通往后续章节的桥，本章留了五座：**第 5 章**（eBPF 子系统内核代码结构）把 2.1 的一行宏展开成源码地图；**第 6 章**（JIT 编译与性能优化）接手生命周期第三步——验证通过的字节码变成本机指令；**第 15 章**（XDP 深度实战）在 3.6 只开一扇门处展开驱动层模式、性能与真实部署；**第 16 章**（TC 与 eBPF）沿锚点②的 netlink 路线深入 clsact 体系；**第 20 章**（CO-RE）补全 `vmlinux.h` 与 BTF 的完整故事——fentry/fexit 依赖的本机 BTF 只是它的一个应用面。

## 本章参考来源

- Linux kernel documentation：BPF program types 与 attach 相关篇目（`Documentation/bpf/`——程序类型、挂载语义与 `verifier.rst` 等相邻文档的上游基准；随内核版本演进，以所用内核源码为准）；`include/uapi/linux/bpf.h` 枚举与辅助函数原型注释。
- libbpf 头文档：`bpf_helpers.h`（SEC 命名法权威头注释，2.3 引）、`bpf_tracing.h`（`BPF_PROG`／`BPF_KPROBE`／`BPF_KRETPROBE` 宏文档）、`libbpf.h`（挂载 API——`bpf_program__attach_*` 一族与 `bpf_xdp_attach`，本机 1.3.0 头文件实证）。
- LWN.net：XDP、LSM（BPF LSM）、fentry/fexit（BPF trampoline）系列文章（lwn.net）。
- Andrii Nakryiko：libbpf 系列文章（SEC 命名与 libbpf 编程、`bpf_link` 挂载模型、libbpf 1.0 迁移、BTF/CO-RE；nakryiko.com）。
- ebpf.top：BPF 入门系列相关文章（程序类型与挂载点的中文讲解）。

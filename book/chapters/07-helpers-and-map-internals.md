# 第 7 章 辅助函数与映射的内部实现

> 本章独立示例已落地到 [`book/examples/chapter-07/`](../examples/chapter-07/):[`00-env/`](../examples/chapter-07/00-env/) 环境自检、运行型示例三件——[`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/) hash 查找/更新闭环、[`02-percpu-counter/`](../examples/chapter-07/02-percpu-counter/) per-CPU 计数器、[`03-event-output/`](../examples/chapter-07/03-event-output/) 事件输出双变体(ringbuf vs perf buffer)——切片器 [`04-helper-dispatch/`](../examples/chapter-07/04-helper-dispatch/) 与 [`05-map-internals/`](../examples/chapter-07/05-map-internals/)、机检 [`scripts/`](../examples/chapter-07/scripts/)。本章是运行章:示例编译级已验证(make 全链零告警),加载与运行为预期档(需 root);行号引用一律以 v6.6.78 走读树实测为准。

## 学习目标

读完本章后,你应当能够:

- 沿一条 `call` 指令讲全 helper 的分发路径:`__bpf_call_base` 基址、验证期 `get_func_proto` 解析链、`bpf_base_func_proto` 公共兜底,并解释「helper 地址为什么是固定的」(一节 1.1 与 1.2)。
- 解释白名单如何「随程序类型滚动」:同一 helper id 在不同类型下的可见性由各 `ops->get_func_proto` 决定,并能对着源码给出 tracing 族实例(一节 1.3)。
- 对着 `hashtab.c` 说出 hash 映射的并发模型:读侧无锁 RCU、写侧 per-CPU 桶锁、`bpf_mem_alloc` 的 RCU 时代回收,以及 prealloc 与 `BPF_F_NO_PREALLOC` 的取舍(二节)。
- 用「分身计数、用户态聚合」的语义解释 per-CPU 映射,并说出 v6.6 映射内存记账的 memcg 形态与 JIT 内存配额的分工(三节)。
- 从实现层对比 perf buffer 与 ring buffer 的五点差异,并根据场景选型;三个示例在本机构建复现(四节)。

## 本章导读

第 6 章拆完「程序怎么跑」,本章拆「程序依赖的两样东西怎么实现」:BPF 程序自己不干活的地方全靠**辅助函数**(helper)代劳——读映射、输出事件、取时间;程序有状态要记,靠**映射**(map)——它是内核态与用户态共享数据的唯一常规通道。第 4 章讲过「辅助函数面跟着执行现场走」(类型三元组之一),第 6 章站五见过 helper 调用的「固定地址」谜面(`__bpf_call_base + imm`,kernel/bpf/core.c:1243,v6.6.78)——本章先把这个谜底挖透(一节),再进映射的文件内部:第 5 章三节已经把八类映射对回了实现文件,本章不再铺地图,而是**进文件看并发与内存**:hash 的两级锁与 RCU(二节)、per-CPU 与 LRU 与记账(三节)、两条事件通道(四节)。

本章是运行章,验证口径在走读章之上多了一档。走读档沿用:切片器 [`04-helper-dispatch/`](../examples/chapter-07/04-helper-dispatch/)(五站)与 [`05-map-internals/`](../examples/chapter-07/05-map-internals/)(六站)对走读树实跑,行号标「(v6.6.78)」、机检可复现。运行档是新的:三个示例(XDP hash 计数、per-CPU 计数器、事件输出双变体)在本机**编译级已验证**——`make` 全链(vmlinux.h 生成 → BPF 侧编译 → skeleton 生成 → gcc 链接)零告警通过;但**加载与运行为预期档**(需 root,本机 `unprivileged_bpf_disabled=2`),所有运行输出标注「预期输出示例 / 运行级待验证」。「编译过的」与「跑起来的」是两个验证等级——这个区分本身就是本章要立的规矩。

最后把前文埋的预告逐条回收,共四点:

- 预告①(第 4 章 1.2):「辅助函数面跟着执行现场走」→ 一节 1.3 兑现:白名单的内核机制是每个程序类型一张 `get_func_proto` 查询函数,「面」的大小由此逐类型决定。
- 预告②(第 6 章站五):「helper 调用地址恒定——`__bpf_call_base + imm`」→ 一节 1.2 兑现:214 个 helper 全在核心内核、以 `__bpf_call_base` 为基址相对寻址,「固定」的机制与代价(为什么 kfunc 要例外)都从这段代码读出。
- 预告③(第 5 章三节):映射生命周期「从 fd 到一块内存」→ 二/三节接棒:内存这块怎么并发地读、写、回收、记账。
- 预告④(示例 01 的并发窗口):01-hash-lookup-update README 提到「lookup 与 update 之间存在并发窗口」→ 二节 2.3 在源码层兑现:窗口两端的语义与竞态结果。

三个示例各司其职,先对号再上车:

| 示例 | 演示什么 | 正文落点 |
|---|---|---|
| [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/) | hash 查找/更新闭环与并发窗口 | 二节 2.1/2.3 |
| [`02-percpu-counter/`](../examples/chapter-07/02-percpu-counter/) | per-CPU 零锁计数与用户态聚合 | 三节 3.1 |
| [`03-event-output/`](../examples/chapter-07/03-event-output/) | ringbuf/perf 双通道同源对照 | 四节 4.1–4.3 |

学法建议一条:本章的每个机制都配了**运行型示例或切片器**——读到任何一个「为什么这么设计」,先去对应资产里跑一眼源码现场再回来,比纯读正文快一倍。本章也是全书从「读内核」走向「写程序」的转折点:第 8 章 BTF/CO-RE 之后,第 9 章起就是动手写程序了——本章的三个示例就是热身赛。

路线图:一节走 helper 分发全链(带切片器五站);二节进 hash 内部(切片器六站前五站);三节 per-CPU、LRU 与记账(切片器第六站);四节两条事件通道(示例 03 实证);可视化给六类映射与两条通道的两张对照图;排错、课后、总结收尾。

## 一、辅助函数:从 call 指令到白名单

### 1.1 一条 call 指令的旅程

背景问题:BPF 程序里写 `bpf_map_lookup_elem(&map, key)`,编译成一条 `call` 指令(`imm` 字段 = helper id)。CPU 执行到它时发生什么?答案取决于程序以哪种形态在跑(第 6 章 1.1 的三形态),但两种形态的**目的地**是同一个函数指针表:

- **解释器形态**:`___bpf_prog_run` 的调度循环里,`JMP_CALL_ARGS` 标签处(`kernel/bpf/core.c:1975`,v6.6.78)执行 `BPF_R0 = (__bpf_call_base_args + insn->imm)(BPF_R1, BPF_R2, BPF_R3, BPF_R4, BPF_R5, next_insn)`——用 helper id 当下标,从 `__bpf_call_base_args` 数组取函数指针,五个寄存器加一条「下一条指令」指针直接调用。
- **JIT 形态**:第 6 章站五讲过,`bpf_jit_get_func_addr`(kernel/bpf/core.c:1210,v6.6.78)按 `__bpf_call_base + imm` 算出目标地址,直接编成一条 x86 `call` 相对转移(站三的贴码)。

两条路的分工一张表看清:

| | 解释器形态 | JIT 形态 |
|---|---|---|
| 出口 | `JMP_CALL_ARGS` 标签(core.c:1975) | 一条 x86 `call` 相对转移(第 6 章站五) |
| 寻址 | `__bpf_call_base_args[imm]` 查表 | `__bpf_call_base + imm` 相对偏移 |
| 额外参数 | 第 6 参传 `next_insn`(CALL_ARGS 内部操作码) | 无 |
| 编译时机 | 运行时逐条 | 加载时一次 |

解释器那条路还有一层折算细节值得看:`bpf_patch_call_args`(kernel/bpf/core.c:2240,v6.6.78)。带栈长的解释器入口(第 6 章 1.1 的 16 档)接受六个参数(五个寄存器加栈底指针),内核用内部操作码 `CALL_ARGS` 走带参出口;这个函数把「栈长档位选中的解释器入口」折算成相对 `__bpf_call_base_args` 的偏移,塞回指令的 imm 字段——解释器从此把这类调用当成普通 helper 调用分发,不用在调度循环里特判:

```c
/* kernel/bpf/core.c:2240-2248(v6.6.78) */
void bpf_patch_call_args(struct bpf_insn *insn, u32 stack_depth)
{
	stack_depth = max_t(u32, stack_depth, 1);
	insn->off = (s16) insn->imm;
	insn->imm = interpreters_args[(round_up(stack_depth, 32) / 32) - 1] -
		__bpf_call_base_args;
	insn->code = BPF_JMP | BPF_CALL_ARGS;
}
```

两条路殊途同归的关键,是那块**基石**:`__bpf_call_base`(kernel/bpf/core.c:1500,v6.6.78)——一个恒返回 0 的 `noinline` 函数,本体只值三行,却是整个 helper 体系的锚点:

```c
/* kernel/bpf/core.c:1500-1505(v6.6.78) */
noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	return 0;
}
EXPORT_SYMBOL_GPL(__bpf_call_base);
```

切片器 [`04-helper-dispatch/`](../examples/chapter-07/04-helper-dispatch/) 站 1 切的就是这三段:基址、解释器出口、以及把「带栈长的解释器入口」折算成相对地址的 `bpf_patch_call_args`(:2240)。

### 1.2 「固定地址」之谜:为什么 JIT 敢一次编死

第 6 章留过一个悬念:为什么普通 helper 调用第一趟编译就能填对目标地址,而 bpf2bpf 子程序调用必须等第二趟?谜底在 214 个 helper 的**安放位置**:它们全部实现在核心内核(`kernel/bpf/helpers.c` 及各子系统),不加载、不卸载——只要内核在跑,`__bpf_call_base` 与每个 helper 之间的相对偏移**永不改变**。于是:

- helper 调用:`目标地址 = __bpf_call_base + imm`,imm 是编译进指令的常量,第一趟即准;
- 子程序调用:目标是**另一段 JIT 出来的机器码**,地址取决于编译结果与映像分配——只能等 `addrs[]` 收敛(第 6 章站五)。

`noinline` 也值得停一下:它禁止编译器把 `__bpf_call_base` 内联进任何调用点,保证这个符号在最终内核映像里有**唯一确定的地址**——基址的「基」字由此而来。代价与收益的账本:一次间接的固定偏移,换来了 JIT 单趟编译 helper 调用、也换来了恶意程序无法把调用目标改到任意内核地址(imm 是验证器查表合法的 id,不是裸地址,回指第 3 章参数校验)。

214 个 helper 里有一个特殊成员值得单独标记:**`bpf_tail_call`**(公共兜底白名单内,一节 1.3 贴码的 `case BPF_FUNC_tail_call` 可见)。它是唯一一个**改写控制流**的 helper——不是调用后返回,而是跳进另一张 prog 数组里的另一个程序、且不占用新的栈帧。第 6 章站五见过它的 JIT 两形态(直接/间接)与 33 层计数上限;从 helper 机制的视角看,它的特殊在于破坏了「call 之后必然回来」的约定,所以验证器对它的参数(prog 数组映射指针 + 索引)与后续指令可达性有专门分析(第 3 章)。记住这个特例,能帮你在读其他 213 个 helper 时守住「helper 不改控制流」的默认心智模型。

「固定」的边界在哪里?反例是 **kfunc**。kfunc(内核函数直调,`BPF_PSEUDO_KFUNC_CALL`)的目标可能住在**可加载模块**里——模块的符号地址要等模块真正加载后才确定,「恒定偏移」的前提塌了。所以第 6 章站五贴过的 `bpf_jit_get_func_addr` 里有第三个分支:先问 `bpf_jit_supports_far_kfunc_call()`(内核是否支持远距 kfunc 调用),支持则走 `bpf_get_kfunc_addr` 按符号解析。helper 与 kfunc 的这条分界线,恰好把「核心内核 = 固定地址 = 基址相对寻址」这个论证从正反两面讲完整。

**教师视角**。可以把 helper 体系想成一栋只有一层门卫的房子:214 个房间编号固定(imm),门卫(`__bpf_call_base`)坐在 0 号位,访客报编号、门卫指路——编号合法即领路,不合法在验证期就被拒之门外。常见误区有两个:其一,以为 helper 是「系统调用」——不,它只是内核函数直调,没有上下文切换(这正是 BPF 快的原因之一);其二,以为 imm 可以当跳板——imm 只能是 1..214 的 id,查不到 proto 的 id 过不了验证器,这条通道从一开始就没有「任意地址」的口子。

### 1.3 白名单:214 个 helper,每个程序各见一面

helper 的「函数面」不是全量 214 个都放行,而是**随程序类型滚动**。机制在验证器:`check_helper_call` 里一句委托(kernel/bpf/verifier.c:9965,v6.6.78):

```c
/* kernel/bpf/verifier.c:9965-9975(v6.6.78) */
	if (env->ops->get_func_proto)
		fn = env->ops->get_func_proto(func_id, env->prog);
	if (!fn) {
		verbose(env, "unknown func %s#%d\n", func_id_name(func_id),
			func_id);
		return -EINVAL;
	}

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	if (!env->prog->gpl_compatible && fn->gpl_only) {
		verbose(env, "cannot call GPL-restricted function from non-GPL compatible program\n");
```

`env->ops` 是**程序类型**的验证器操作表——白名单问题的全部答案就是「该类型的 `get_func_proto` 怎么答」。三个实例(切片器站 2/3/4 逐个切出):

- **公共兜底**:`bpf_base_func_proto`(kernel/bpf/helpers.c:1757,v6.6.78)——映射操作五件套(lookup/update/delete/push/pop/peek/lookup_percpu_elem)、随机数、CPU 号、tail call 等**所有类型通用**的 helper;
- **类型追加**:各类型 ops 的 `get_func_proto` 先答「本类型专属」,答不上再回落公共兜底(如 `syscall_prog_func_proto`,kernel/bpf/syscall.c:5713,v6.6.78);
- ** tracing 族实例**:`tracing_prog_func_proto`(kernel/trace/bpf_trace.c:1948,v6.6.78)——先按 prog 细分(tp_btf/fentry 走不同分支),再叠 `CONFIG_NET` 等编译期开关,同一 id 的可见性随两者变化。

失败出口两层:`get_func_proto` 返回 NULL → `unknown func`,-EINVAL 加载终止;查到 proto 但程序 license 不兼容 `gpl_only` helper → 同样终止(上面贴码的后半)。第 3 章失败画廊里「`bpf_get_current_pid_tgid()` 不在 XDP 白名单」的反例,机制上就是这一行 `verbose(env, "unknown func ...")`。

这张 ops 表本身从哪来?第 5 章 1.2 的双展开第二次亮相:`include/linux/bpf_types.h` 那份清单按 `BPF_PROG_TYPE` 宏在 `kernel/bpf/syscall.c` 展开出**程序类型注册表**,每种类型一行 `.get_func_proto = <本类型白名单函数>`——程序侧与映射侧共用一份清单、各自展开(切片器 [`05-map-internals/`](../examples/chapter-07/05-map-internals/) 站 1 是映射侧的展开现场)。所以白名单的「层级」读出来是三级:类型专属(`get_func_proto` 开头)→ 编译期开关(如 tracing 族里的 `#ifdef CONFIG_NET`)→ 公共兜底(`bpf_base_func_proto`)——第一级决定「这类程序是什么身份」,第二级决定「这个内核编译进了什么」,第三级兜住所有人都要的那几个。

### 1.4 proto:helper 的说明书

`get_func_proto` 返回的不是函数指针,是一份**说明书**——`struct bpf_func_proto`(include/linux/bpf.h:797,v6.6.78;切片器站 5):

- `func`:真正的 C 函数指针(五寄存器约定,即 1.1 两条路调用的终点);
- `gpl_only`:license 门槛(1.3 的第二道出口);
- `pkt_access`:能否直接持有包指针(直接包访问,direct packet access 的闸);
- `might_sleep`:能否在原子上下文调用(不许睡的上下文里调它会被告警);
- `ret_type` + `arg1_type..arg5_type`:返回值与五参的类型——**验证器逐参检查的依据**(第 3 章「helper 参数校验」的机制落点,例如 ARG_PTR_TO_MAP 检查第一个参必须是映射指针、ARG_CONST_SIZE 检查长度上限)。

一个最小的实例:`bpf_map_lookup_elem_proto`(kernel/bpf/helpers.c:44,v6.6.78)——`ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL`(返回值可能是 NULL,验证器强制判空)、`arg1_type = ARG_CONST_MAP_PTR`、`arg2_type = ARG_PTR_TO_MAP_KEY`。你写 BPF 代码时验证器逼你判空的那条报错,源头就是这一个字段。

先给一张**家族全景**——214 个 helper 并非一盘散沙,按用途可归六族(每族举代表 id;全表见 include/uapi/linux/bpf.h 的 `___BPF_FUNC_MAPPER`,v6.6.78 计 214 个 FN):

| 家族 | 代表 helper | 正文位置 |
|---|---|---|
| 映射操作 | `bpf_map_lookup_elem`/`update_elem`/`delete_elem`/`push/pop/peek_elem`/`lookup_percpu_elem` | 一节 1.4、二/三节 |
| 事件输出 | `bpf_perf_event_output`/`ringbuf_output`/`ringbuf_reserve`/`ringbuf_submit`/`ringbuf_discard` | 四节 |
| 尾调用与跳转 | `bpf_tail_call`(需同一张 prog 数组,见第 6 章站五 33 层计数) | 第 6 章 |
| 程序上下文 | `bpf_get_current_pid_tgid`/`get_current_comm`/`get_smp_processor_id` | 示例 03 |
| 取数与随机 | `bpf_ktime_get_ns`/`get_prandom_u32`/`get_numa_node_id`(公共兜底族) | 一节 1.3 |
| 打印与追踪 | `bpf_trace_printk`/`bpf_trace_vprintf`(调试用,生产慎用) | 排错节 |

这不是备忘录,是白名单机制的产品视角:每族在公共兜底或类型 `get_func_proto` 里的出现与否,直接决定「哪类程序能干什么」——四节 4.1 的 `bpf_perf_event_output` 之所以只在追踪族可见,答案不在这张表,在 `tracing_prog_func_proto` 的 switch 里。

五个字段各管一道闸:`pkt_access = true` 的 helper(如 lookup_elem)允许把包指针当参数传——直接包访问(第 2 章 2.4)的合法通道;`might_sleep = true` 的 helper 只能睡在有调度的上下文里,软中断/XDP 这类原子上下文调用它会被验证器或运行时告警拦下。再看一眼 `bpf_map_update_elem` 的 proto(`bpf_map_update_elem_proto`,同文件):`arg4_type = ARG_ANYTHING` 之外多一个 `BPF_ANY/NOEXIST/EXIST` 的 flags 参——语义是「说明书里的一个整型参数」,真正的语义表在实现函数里,这正是二节 2.3 的入口。

### 1.5 学生动手:切片器五站

```bash
# ①五站全量(行号即本节引用事实)
bash book/examples/chapter-07/04-helper-dispatch/trace-helpers.sh
# ②只看解析链一站
bash book/examples/chapter-07/04-helper-dispatch/trace-helpers.sh | sed -n '/^## 站 3/,/^## 站 4/p'
# ③负路径自检(应报错 exit 1)
WALK_TREE=/nonexistent bash book/examples/chapter-07/04-helper-dispatch/trace-helpers.sh
```

预期输出(均本机实测):①五站各带「定位/代码段/职责」,汇总 `TREE_VERSION=v6.6.78 STATIONS=5`,exit 0;②站 3 的解析链与两层失败出口;③`走读树目录不存在:/nonexistent`,exit 1。

再加一条**自己动手的一节收尾**:示例 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/) 编出的 `.bpf.o` 里,两条 helper 调用长这样(本机 llvm-objdump -d 实跑,编译级已验证):

```text
      12:	85 00 00 00 01 00 00 00	call 0x1     ← bpf_map_lookup_elem(id=1)
      23:	85 00 00 00 02 00 00 00	call 0x2     ← bpf_map_update_elem(id=2)
```

操作码 `0x85` 就是 `BPF_JMP | BPF_CALL`,立即数字段 `0x1`/`0x2` 正是一节 1.1 说的 helper id——本节从「一条 call 指令」开始讲起,到亲手编出它结束;这个 imm 将在一节 1.3 的白名单里被查、在 1.2 的基址上被折算成地址、最终在二节的 ops 表里落到实现。

**检查点**(自测后看提示):

1. 为什么 helper 调用不需要第二趟编译,而 bpf2bpf 子程序调用需要?——提示:回看 1.2 的「安放位置」论证与第 6 章站五的 `func_addr_fixed` 分支。
2. 同一个 `bpf_skb_output`,为什么 XDP 程序里是 `xdp_output` 的不同变体可见?——提示:白名单的粒度在「类型的 get_func_proto」,不在 id 本身;1.3 的 tracing 族是同构案例。
3. `gpl_only` 闸门检查的是「程序」还是「helper」的哪个属性?——提示:1.3 贴码最后一行的两个条件相与;许可证语义见第 1 章。

**独立示例**:本节五站已工具化——[`04-helper-dispatch/`](../examples/chapter-07/04-helper-dispatch/);helper 在 hash 映射上的两个消费现场见 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/)(BPF 侧 lookup/update)。

## 二、hash 映射内部:并发与内存

hash 映射(BPF_MAP_TYPE_HASH)是使用频率最高、内部也最讲究的一类映射。本节与切片器 [`05-map-internals/`](../examples/chapter-07/05-map-internals/) 的六站一一对应,走读对象是 `kernel/bpf/hashtab.c`(2630 行,v6.6.78)。每站四拍照旧:签名→职责→关键代码段→失败出口。

### 2.1 注册表与接口表:两种「表」先分清

hash 映射出现在两张表里,别混。第一张是**类型注册表** `bpf_map_types[]`(kernel/bpf/syscall.c:63,v6.6.78;切片器站 1)——`bpf_types.h` 那份清单第三次展开的映射侧,`BPF_MAP_CREATE` 拿着 `attr->map_type` 在这里查到本类型的 `bpf_map_ops`(第 5 章三节生命周期入口的查表处,本章不重复生命周期,只看 ops 指向的实现)。第二张是**接口表** `struct bpf_map_ops`(include/linux/bpf.h:75,v6.6.78;切片器站 2)——每种映射类型一张函数表,用户态可达的 `alloc_check/alloc/free/get_next_key/lookup 系/batch 系` 在前,程序侧(BPF 内可调)的 `map_lookup_elem/map_update_elem/map_delete_elem` 在后。示例 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/) 里 BPF 侧那两个 helper 调用,经一节 1.3 的白名单查到 proto、再由运行时落到这张表的 `htab_map_update_elem` 上——两张表 + 一个 ops,就是「一个 helper 调用到达实现」的完整链。

### 2.2 查找:为什么敢无锁

hash 查找的实现 `__htab_map_lookup_elem`(kernel/bpf/hashtab.c:677,v6.6.78):

```c
/* kernel/bpf/hashtab.c:677-695(v6.6.78) */
static void *__htab_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct htab_elem *l;
	u32 hash, key_size;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	head = select_bucket(htab, hash);

	l = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);

	return l;
}
```

没有一把锁:算哈希 → 选桶 → 沿链表逐个比对。它的合法性有两根支柱。其一,**RCU 读侧临界区**(`WARN_ON_ONCE` 断言调用者持有 rcu_read_lock/BPF 程序天然处于 RCU 读侧)——写侧删改都走「摘链后延迟回收」(2.5),读者手里的指针要么指向完整元素、要么指向合法链表节点,永远不会踩到已归还的内存。其二,**rcu 发布语义**:`hlist_nulls_add_head_rcu`(2.3 的替换动作)保证新元素对读者「要么完整可见、要么完全不可见」,不存在半个节点。

还有一枚彩蛋:JIT 阶段 hash 查找会被**内联**——`htab_map_gen_lookup`(kernel/bpf/hashtab.c:722,v6.6.78)把 `bpf_map_lookup_elem` 的 call 指令改写成对 `__htab_map_lookup_elem` 的直接调用,跳过 helper 包装层(参数检查在验证期已完成)。一节 1.1 的「两条路」到这里变成「一条短路」:helper 机制是通用底座,JIT 有权对热点路径走捷径——这是第 6 章与本章在代码上的一次会师。

### 2.3 更新:三语义、两级锁与一个并发窗口

更新的实现 `htab_map_update_elem`(kernel/bpf/hashtab.c:1098,v6.6.78;切片器站 4 切的是头部),三语义由 `check_flags`(:1083)把关:`BPF_NOEXIST` 要求键不存在(否则 -EEXIST)、`BPF_EXIST` 要求键存在(否则 -ENOENT)、`BPF_ANY` 不挑。加锁后的主干(kernel/bpf/hashtab.c:1146-1162,v6.6.78):

```c
/* kernel/bpf/hashtab.c:1146-1162(v6.6.78) */
	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		return ret;

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	if (unlikely(l_old && (map_flags & BPF_F_LOCK))) {
		/* first lookup without the bucket lock didn't find the element,
		 * but second lookup with the bucket lock found it.
		 * This case is highly unlikely, but has to be dealt with:
		 * grab the element lock in addition to the bucket lock
		 * and update element in place
		 */
```

**两级锁**是 hash 并发设计的核心(切片器站 3,`htab_lock_bucket`,kernel/bpf/hashtab.c:149,v6.6.78):第一级是 **per-CPU 计数锁**(`__this_cpu_inc_return(*(htab->map_locked[hash]))`)——哈希值决定落在哪个 CPU 的计数槽上,同 CPU 重入直接 -EBUSY,把死锁风险挡在自旋之前;第二级才是**桶级 `raw_spin_lock`**(粒度:一个桶,不是整表),配合 `local_irq_save` 关中断——自旋锁在中断上下文也可用,RT 内核上同样安全。粒度选择的账:整表一把锁最简单,但所有 CPU 抢同一 cacheline;桶级锁把争用按哈希打散;per-CPU 计数再解决「同 CPU 上中断再入同桶」的自旋死锁。三层叠加,写侧并发安全、且临界区只有链表操作几条指令。

**并发窗口的兑现**(示例 01 的 README 埋过):示例里「先 lookup、不命中再 update(NOEXIST)」两步之间没有原子性——两个 CPU 同时首包,可能双双 lookup 未命中,随后一个 NOEXIST 成功、一个撞上 -EEXIST 返回 -E2BIG 之外的第一类失败(表满则是 -E2BIG)。源码层看,`check_flags` 在**桶锁内**复查,`alloc_htab_elem` 对 NOEXIST 的裁决也在锁内——所以窗口只存在于「程序侧的两步」之间,内核侧的单次 update 原子无误。这解释了示例的处置:把 -EEXIST/-E2BIG 当正常事,或者改用 `BPF_ANY`。

**第三种更新:`BPF_F_LOCK`**(值里带 `struct bpf_spin_lock` 的映射)。它有一个硬前提值得注意:内核怎么知道「值的哪个偏移是锁」?答案是 **BTF 字段记录**——建映射时校验值类型注解(`__attribute__((btf_type_tag))` 之外的 btf_record 机制),贴码里那句 `btf_record_has_field(map->record, BPF_SPIN_LOCK)` 检查不过直接 -EINVAL。这是 BTF 作为「内核类型说明书」第一次在本章出場:它不只服务 CO-RE(第 8 章正题),也是运行期字段级能力的登记簿。
值得单独一提:它走**元素级锁**在原位改值(不摘链、不分配),更新路径开头(:1124 起)有专门的快路——先无锁找元素、抓元素锁原位改、99.9% 到此为止;极小概率的「无锁时没找到、锁内找到了」再回落桶锁路径(上面贴码的后半就是这个兜底)。两级锁之外的第三级,粒度细到一个值。

**失败出口**汇总:未知 flags → -EINVAL;NOEXIST 撞已有键 → -EEXIST;EXIST 没找到 → -ENOENT;prealloc 表满 → -E2BIG(2.4);内存耗尽 → -ENOMEM。

### 2.4 prealloc:内存换延迟的一笔账

建表时的关键抉择在 `htab_map_alloc`(kernel/bpf/hashtab.c:422,v6.6.78):`bool prealloc = !(attr->map_flags & BPF_F_NO_PREALLOC);`——默认(不加 NO_PREALLOC 标志)**预分配全部元素**(`htab_is_prealloc` 判定,:131),配合 `htab->extra_elems` 备用池:

- **prealloc(默认)**:建表时一次性分配 max_entries 个元素,运行期更新只做链表摘挂,不碰分配器——更新延迟恒定、无内存分配失败,代价是建表慢、哪怕用不满也占着内存;
- **BPF_F_NO_PREALLOC**:按需分配——省内存、建表快,但每次插入走 `bpf_mem_alloc` 分配路径,延迟抖动大,且容量上限不再由建表保证。

选型一句话:数据面高频更新选默认 prealloc(延迟可预算);键空间稀疏或巨大的(LRU 场景除外,见三节)才考虑 NO_PREALLOC。注意 LRU 语义与 NO_PREALLOC 互斥(:440 的检查),内核用一张 `htab_lru_map_ops`(三节)给出第三种答案。

### 2.5 删除与回收:RCU 时代的「延迟归 library」

删除 `htab_map_delete_elem`(kernel/bpf/hashtab.c:1424,v6.6.78)与更新同构:RCU 读者断言 → 锁桶 → `lookup_elem_raw` 原位找 → 摘链。真正的讲究在**回收**这一步(切片器站 5 的第二段,kernel/bpf/hashtab.c:892-901,v6.6.78):

```c
/* kernel/bpf/hashtab.c:892-901(v6.6.78) */
static void htab_elem_free(struct bpf_htab *htab, struct htab_elem *l)
{
	check_and_free_fields(htab, l);

	migrate_disable();
	if (htab->map.map_type == BPF_MAP_TYPE_PERCPU_HASH)
		bpf_mem_cache_free(&htab->pcpu_ma, l->ptr_to_pptr);
	bpf_mem_cache_free(&htab->ma, l);
	migrate_enable();
}
```

内存不直接归还页分配器,而是交给 `bpf_mem_alloc`(建表时配好的 ma 与 pcpu_ma 双分配器,:88-89)——它内部按 **RCU 时代(grace period)**攒批释放:元素摘链后先「逻辑死亡」,等所有在读者都越过临界区后才物理归还。这就是 2.2「读侧敢无锁」的另一半:写侧不省这几次延迟,换读者永远不见悬空指针。per-CPU hash 的元素是两块(per-CPU 值区 + 元素本体),所以 free 成对调两次——`pcpu_ma` 与 `ma` 的分工在回收侧最直观。

### 二节收束:双视角与检查点

**教师视角。**hash 映射的并发模型可以压缩成一句话:**读免锁靠 RCU、写串行靠桶锁、死锁防靠 per-CPU、内存晚还靠 mem_alloc**。学生最容易卡住的是「为什么敢无锁读」——把 2.2 与 2.5 连起来读一遍就通了:无锁的底气不在读侧,在写侧的纪律(发布用 RCU 写入、回收走 grace period)。另一个误区是把 `bpf_map_lookup_elem` 当「函数调用」逐次展开:实际 JIT 常内联成几条指令(2.2 的彩蛋),「helper 调用贵」的直觉在 hash 查找上并不成立。

**学生动手。**三条命令把六站切出来(预期输出均本机实测):

```bash
# ①六站全量
bash book/examples/chapter-07/05-map-internals/trace-map-internals.sh
# ②对读示例:并发窗口的源码现场(站 4)
bash book/examples/chapter-07/05-map-internals/trace-map-internals.sh | sed -n '/^## 站 4/,/^## 站 5/p'
# ③负路径自检
WALK_TREE=/nonexistent bash book/examples/chapter-07/05-map-internals/trace-map-internals.sh
```

①汇总 `TREE_VERSION=v6.6.78 STATIONS=6` exit 0;②站 4 的更新头部与 `htab_lock_bucket` 调用;③exit 1。

**检查点**(自测后看提示):

1. 示例 01 的「lookup→update(NOEXIST)」窗口,内核能不能替你关掉?——提示:单次 update 在桶锁内原子(2.3);关窗口的办法是改成 `BPF_ANY` 单调用,或 `BPF_F_LOCK` 原位改。
2. 同一 CPU 上,中断处理里的 BPF 程序更新同一个 hash 映射,为什么会撞 -EBUSY?——提示:站 3 的第一级 per-CPU 计数锁就是干这个的(重入保护),不是性能优化。
3. prealloc 表更新为什么不会有 -ENOMEM?——提示:元素早在建表时分配;运行期只挂链。那 -E2BIG 从哪来(2.3 失败出口)?

### 2.6 补全 ops 表的两类成员:批量与一次性票据

二节主干只走了 lookup/update/delete 三个 ops,接口表(站 2)里还有两类值得点名。**batch 系**(`map_lookup_batch`/`map_update_batch`/`map_delete_batch`):一次系统调用处理一批键,配 `elem_flags` 与用户态批头——把 N 次 `bpf_map_*_elem` 的固定开销(上下文切换 + 锁路费)摊成一次;ops 表里没实现 batch 的类型会回落到逐键循环,语义等价、开销不同。**lookup_and_delete 系**(`map_lookup_and_delete_elem`,kernel/bpf/syscall.c:1905,v6.6.78;ops 表同名成员):读出即删除的原子票据——任务队列「取件」语义的正确实现,手写 lookup+delete 两步会重新打开 2.3 讨论过的并发窗口。三节 3.2 的 LRU 家族为它准备了专用变体(`htab_lru_map_lookup_and_delete_elem`):从 LRU 表取走即逐出,票据语义与容量回收一步到位。

**独立示例**:hash 闭环见 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/);本节源码现场工具化在 [`05-map-internals/`](../examples/chapter-07/05-map-internals/)(六站中的前五站)。


## 三、per-CPU、LRU 与内存记账

二节聚焦「一个 hash 映射内部的并发」,本节把镜头拉远:同一种「键值存储」语义在三个维度的变体——per-CPU(并发维度)、LRU(容量维度)、记账(内存维度)。三个变体共享二节的 ops 框架,**换的是 ops 表,不变的是框架**——这是第 5 章「两层皮」设计在映射家族里的体现。

### 3.1 per-CPU:分身计数,聚合归还

per-CPU 映射把「一份共享值」变成「每 CPU 一份分身」:`BPF_MAP_TYPE_PERCPU_ARRAY`(include/uapi/linux/bpf.h:930,v6.6.78)与 `BPF_MAP_TYPE_PERCPU_HASH`,ops 分别是 `percpu_array_map_ops`(kernel/bpf/arraymap.c:775,v6.6.78)等。语义三条:

- **BPF 侧只摸本 CPU**:`bpf_map_lookup_elem` 返回**当前 CPU** 槽位指针——示例 [`02-percpu-counter/`](../examples/chapter-07/02-percpu-counter/) 的收包计数因此零锁零争用;
- **用户态读是整块拷回**:同一条 `bpf_map_lookup_elem` 从用户态发出,一次拷回 `CPU 数 × value_size` 的全部槽位,聚合(求和/取最大)由用户自己做——读出的是**近似快照**(逐槽拷贝之间计数还在变);
- **跨 CPU 读有专用 helper**:`bpf_map_lookup_percpu_elem`(在公共兜底 `bpf_base_func_proto` 里,一节 1.3 贴码可见)带 CPU 号参数,程序可读**指定 CPU** 的分身——per-CPU 聚合统计的补丁通道。

代价模型一句话:**用 `CPU 数 × value_size` 的内存与用户态聚合步骤,换 BPF 侧的零锁**。高频计数(perf 场景)近乎总是划算;键空间巨大时(`ncpu × max_entries × value_size`)要算一算内存账——这正好引出 3.3。

选型再细一层:**per-CPU array 还是 per-CPU hash?**键空间小而固定(CPU 号、队列号)用 array——下标即寻址、无哈希开销;键空间动态(exec 计数按二进制名)用 per-CPU hash——代价是每 CPU 独立的哈希表与桶锁(per-CPU hash 的桶锁 per-CPU 化,争用面进一步缩小)。示例 02 选 array 是因为「一个计数器」只有一个键;把它扩成「每个可执行文件一个计数器」,就该换 per-CPU hash 了。

### 3.2 LRU:满员之后的第三种答案

hash 表满了怎么办?二节 2.4 给了两条路:prealloc 表报 -E2BIG,`NO_PREALLOC` 表看分配器脸色。**LRU**(least recently used,最近最少使用)给第三种答案:满员时**自动逐出最旧的键**,新键永远进得来。实现上它仍是 hash——`htab_lru_map_ops`(kernel/bpf/hashtab.c:2301,v6.6.78;切片器站 1 的注册表里同族注册)把 update/delete 换成 `htab_lru_map_update_elem`/`htab_lru_map_delete_elem`:更新前若满,先按 LRU 挑一个牺牲者、经 `bpf_lru_pop_free`(kernel/bpf/bpf_lru_list.c:499,v6.6.78)拿回一个空闲元素,再走与普通 hash 相同的锁内挂链。

逐出的组织结构(与二节同思想:per-CPU 化避免争用):

- **全局三链**:`BPF_LRU_LIST_T_ACTIVE / INACTIVE / FREE`(kernel/bpf/bpf_lru_list.h:17-19,v6.6.78)——活跃/不活跃/空闲,近似两时钟算法;全局链用一把 `raw_spinlock_t` 且 `____cacheline_aligned_in_smp`(:37)独占 cacheline;
- **per-CPU 本地链**:`bpf_lru_locallist`(bpf_lru_list.h:40-48,v6.6.78,FREE/PENDING 两态)——各 CPU 先在本地攒「待归还」,攒够或需要时再汇入全局,把逐出操作的锁争用摊薄到各 CPU。

使用注意两条:其一,LRU 逐出的是**整个键值对**,值里有引用计数类资源(map_of_maps 的内层映射、sock 引用)时由 ops 的 put_ptr 回调释放(二节 2.3 贴码的 `map_fd_put_ptr` 分支)——逐出不等于泄漏;其二,`bpf_map_lookup_elem` 会为命中键「续命」,只想窥视不续命的负载要考虑 `peek` 语义或自建时间戳,这是 LRU 缓存类负载最常见的语义坑。
`lookup_and_delete` 变体(`htab_lru_map_lookup_and_delete_elem`,2.3 的 ops 表可见)支持「读出即逐出」的一次性票据语义。

### 3.3 记账:内存记在谁的名下

映射是长生命周期对象,内存必须可问责。v6.6 的答案是 **memcg(memory cgroup,内存控制组)记账**——映射的全部内存记在**创建者所在 cgroup** 名下。三个落点(切片器站 6):

```c
/* kernel/bpf/syscall.c:389-398(v6.6.78) */
static void bpf_map_save_memcg(struct bpf_map *map)
{
	/* Currently if a map is created by a process belonging to the root
	 * memory cgroup, get_obj_cgroup_from_current() will return NULL.
	 * So we have to check map->objcg for being NULL each time it's
	 * being used.
	 */
	if (memcg_bpf_enabled())
		map->objcg = get_obj_cgroup_from_current();
}
```

- **挂靠**:创建时 `map->objcg` 记下创建者 cgroup(:389);此后 `bpf_map_area_alloc`(:312)/`bpf_map_kmalloc` 一族分配都带 memcg 标志——账记在 objcg 上,而非调用者或内核自身;
- **释放**:`bpf_map_release_memcg`(kernel/bpf/syscall.c:400,v6.6.78)在映射销毁时归还引用;
- **root cgroup 例外**:贴码注释原话——创建者属于 root cgroup 时 objcg 为 NULL,使用处逐次判空。

边界照例声明:memcg 记账是 6.x 的形态(此前的 memlock rlimit 记账方案已退役),随内核版本演进,以所用内核源码为准。JIT 侧另有独立账本:`bpf_jit_charge_modmem`/`bpf_jit_uncharge_modmem`(kernel/bpf/core.c:1008/:1020,v6.6.78)为每段 JIT 映像记账,配额正是第 6 章一节 1.3 的 `bpf_jit_limit`——**两本账、两个配额体系,但「长生命周期内核内存必须可问责」是同一个设计原则**。

### 3.4 收口:内存生命周期三段

「内存生命周期」至此收成三段:**分配**(建表,prealloc/NO_PREALLOC 决定一次性还是按需)→ **记账**(memcg 挂靠,JIT 侧 charge)→ **回收**(引用计数归零触发,元素经 RCU 时代延迟归还——二节 2.5)。与第 2 章八要素的「卸载」对上:最后一个引用释放时,free ops 沿这张账本把内存逐笔还给 objcg。

**教师视角。**三个变体是同一道题的三种答卷:「并发怎么解」per-CPU 用分身绕开共享;「容量怎么解」LRU 用牺牲者腾位置;「内存怎么解」memcg 用记账挂靠 cgroup。共同的设计指纹是**不改变使用者的 API**——用户态还是那几个 `bpf_map_*` 命令,变化全部藏在 ops 表后面(2.1)。学生误区:把 per-CPU 的「合计」当精确值(3.1 的快照语义)、把 LRU 的「逐出」当错误(-E2BIG 不会出现在 LRU 表上,这是它与普通 hash 的行为差)。

**学生动手。**

```bash
# per-CPU 语义实证:构建并(在 root 档)运行示例 02
make -C book/examples/chapter-07/02-percpu-counter    # 编译级已验证
# 记账与逐出现场:切片器站 6 + LRU ops(站 1 注册表可查)
bash book/examples/chapter-07/05-map-internals/trace-map-internals.sh | sed -n '/^## 站 6/,$p'
```

预期输出(均本机实测):前者产物 pcpu_count;后者站 6 的 save_memcg 贴码与职责注。

**检查点**(自测后看提示):

1. 示例 02 的「合计」为什么是近似值?把用户态聚合改成逐 CPU 调 `bpf_map_lookup_percpu_elem` 会更准吗?——提示:3.1 的快照语义;跨多次系统调用的读数之间同样在变,准是相对的。
2. LRU 表上 `bpf_map_update_elem` 返回 -E2BIG 可能吗?——提示:3.2 逐出机制;但 `BPF_F_NO_PREALLOC` 与 LRU 互斥(二节 2.4),想想为什么逐出需要预分配的元素池。
3. 一个 cgroup 里的进程创建的映射,内存账记在哪?若该 cgroup 被删除,映射还在被使用会怎样?——提示:3.3 的 objcg 挂靠与释放时机;账本跟着映射,不跟着进程。

**独立示例**:per-CPU 语义见 [`02-percpu-counter/`](../examples/chapter-07/02-percpu-counter/);本节源码现场在 [`05-map-internals/`](../examples/chapter-07/05-map-internals/) 站 1(注册表里的 LRU ops)与站 6(记账)。

## 四、事件输出:perf buffer 与 ring buffer

计数器是「用户来查」;事件输出是「内核来报」—— tracepoint 命中、包到达、安全事件发生,程序把一条记录推给用户态。BPF 有两条成熟通道:老资格的 **perf buffer**(BPF_MAP_TYPE_PERF_EVENT_ARRAY)与后来居上的 **ring buffer**(BPF_MAP_TYPE_RINGBUF,include/uapi/linux/bpf.h:958,v6.6.78)。本节沿源码把两条路各走一遍,再给五点对比与选型;示例 [`03-event-output/`](../examples/chapter-07/03-event-output/) 用同一事件源把两条通道各实现一遍,是本节的实证面。

### 4.1 perf buffer:每 CPU 一环,fd 为桥

BPF 侧的入口是 `bpf_perf_event_output`(kernel/trace/bpf_trace.c:660,v6.6.78;proto `bpf_perf_event_output_proto` :698,一节 1.4 的说明书家族)。它要一张 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`(ops:`perf_event_array_map_ops`,kernel/bpf/arraymap.c:1245,v6.6.78)——key 是 **CPU 号**,value 是用户态预先挂到该 CPU 上的 perf 事件 fd。内层实现 `__bpf_perf_event_output`(kernel/trace/bpf_trace.c:618,v6.6.78):

```c
/* kernel/trace/bpf_trace.c:618-635(v6.6.78) */
__bpf_perf_event_output(struct pt_regs *regs, struct bpf_map *map,
			u64 flags, struct perf_raw_record *raw,
			struct perf_sample_data *sd)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	unsigned int cpu = smp_processor_id();
	u64 index = flags & BPF_F_INDEX_MASK;
	struct bpf_event_entry *ee;
	struct perf_event *event;

	if (index == BPF_F_CURRENT_CPU)
		index = cpu;
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;
```

四个出口读出四个事实:`BPF_F_INDEX_MASK` 允许指定 CPU(默认 `BPF_F_CURRENT_CPU` 走本 CPU,:625-626);索引越界 -E2BIG;该 CPU 还没挂事件 fd → -ENOENT(常见新手指错:数组开了 64 格但只有 4 个 CPU 挂了 fd);后段(:637-647)还要验证 fd 确是 `PERF_COUNT_SW_BPF_OUTPUT` 类型的软件事件、且必须在本 CPU 上消费(-EOPNOTSUPP)——**每 CPU 隔离**是 perf 通道的安全设计。通过后 `perf_event_output` 把记录推进当前 CPU 的 perf 环;环满产生**丢失**,丢失计数走用户态 `on_lost` 回调(示例 03 的 perf.c)。内核侧的丢失记账挂在每个 perf 环上,`on_lost` 的 cnt 参数就是该环累计丢失数——它让「丢了多少」从猜测变成可查询的数字,这是 perf 通道在可观测性上仍然占优的一点。

用户态侧是 libbpf 的 `perf_buffer__new`(逐 CPU `perf_event_open` 挂事件)+ `perf_buffer__poll` 轮询——示例 03 的 perf.c 十几行就是全部。

### 4.2 ring buffer:一张共享大页,两段式提交

ring buffer 把「每 CPU 一环」换成**单一共享环形缓冲**:`struct bpf_ringbuf`(kernel/bpf/ringbuf.c:26,v6.6.78)——一页元数据(消费者/生产者位置)+ 双倍数据区(环形回绕免拷贝),用户态 `mmap` 直达数据(`ringbuf_map_mmap_kern`,:261)。BPF 侧两段式:

```c
/* kernel/bpf/ringbuf.c:408-432(v6.6.78) */
static void *__bpf_ringbuf_reserve(struct bpf_ringbuf *rb, u64 size)
{
	unsigned long cons_pos, prod_pos, new_prod_pos, pend_pos, flags;
	struct bpf_ringbuf_hdr *hdr;
	u32 len, pg_off, tmp_size, hdr_len;

	if (unlikely(size > RINGBUF_MAX_RECORD_SZ))
		return NULL;

	len = round_up(size + BPF_RINGBUF_HDR_SZ, 8);
	if (len > ringbuf_total_data_sz(rb))
		return NULL;

	cons_pos = smp_load_acquire(&rb->consumer_pos);

	if (in_nmi()) {
		if (!spin_trylock_irqsave(&rb->spinlock, flags))
			return NULL;
	} else {
		spin_lock_irqsave(&rb->spinlock, flags);
	}

	pend_pos = rb->pending_pos;
	prod_pos = rb->producer_pos;
	new_prod_pos = prod_pos + len;
```

读法四拍:单条记录上限 `RINGBUF_MAX_RECORD_SZ`;空间检查(消费进度 `cons_pos` 决定可用空间——**慢消费者直接限流生产者**,这是丢失语义的来源);NMI 上下文用 trylock(抢不到立刻 NULL,绝不自旋);`pending_pos` 扫描跳过**已预留未提交**的记录——这正是两段式的关键:reserve 拿到的是「已占位但未发布」的私有空间,填完 `bpf_ringbuf_commit`(:489)才发布;中途可以 `bpf_ringbuf_discard` 丢弃(:536 的 proto)。一次性写法 `bpf_ringbuf_output`(:543,`BPF_CALL_4`)是 reserve+memcpy+commit 的糖。提交只推进 `producer_pos`(原子释放语义);用户态 `ring_buffer__poll` 读 `cons/prod` 差值收事件,确认后推进 `consumer_pos`。事件**就绪唤醒**由 irq_work 完成(`bpf_ringbuf_notify`,:150)——轮询之外还能即时醒。

### 4.3 五点对比与选型

| 维度 | perf buffer | ring buffer |
|---|---|---|
| 缓冲所有权 | 每 CPU 一环(PERF_EVENT_ARRAY 按 CPU 号索引) | 单一共享环(元数据页 + 双数据区) |
| BPF 侧写法 | 一次性拷贝(`bpf_perf_event_output`) | reserve→填→submit/discard(两段式,:408/:489) |
| 跨 CPU 顺序 | 各环独立,无全局顺序 | 提交顺序即全局顺序(单 producer_pos 定序) |
| 丢失语义 | 环满丢本 CPU 事件,lost 计数可观测 | reserve 返回 NULL 静默丢弃(慢消费者限流) |
| 用户态接口 | `perf_buffer__new/poll`(依赖 perf_event_open 档位) | `ring_buffer__new/poll` + mmap 直读(无 perf 依赖) |

补一个 6.x 新成员:**user ring buffer**(`BPF_MAP_TYPE_USER_RINGBUF`,include/uapi/linux/bpf.h:962,v6.6.78;ops `user_ringbuf_map_ops`,kernel/bpf/ringbuf.c:370)——方向反转的第三条通道:**用户态写、BPF 读**(`bpf_user_ringbuf_peek` 一族,带 busy 位防并发),配置下发、控制面到数据面的批量传令可用它;与本章两条「BPF 写、用户态读」通道合起来,共享环形缓冲的双向版图就齐了。

选型结论:**默认 ring buffer**——内存开销随实际事件量、全局顺序、无 perf_event_open 档位依赖(示例 03 README 的对照表同款);三种例外选 perf buffer:需要与 perf 生态对齐(采样、`perf report` 消费)、需要逐 CPU 硬隔离、或目标内核版本尚未引入 ringbuf(<5.8,随版本演进,以所用内核为准)。

**教师视角。**两条通道是「共享资源组织方式」的一对镜像:perf 按 CPU 切分(隔离换无争用,代价是排序与 fd 管理),ringbuf 全局共享(顺序与简洁,代价是自旋锁串行化 reserve——NMI 里宁可丢也不等)。学生误区:以为 ringbuf 的 `reserve/submit` 只是 API 风味——它是**发布语义**:submit 之前记录对消费者不存在,这让「复杂事件分步填充」天然安全;perf 的一次性拷贝没有这个中间态。

**学生动手。**

```bash
# 编译级:双变体一次构建(零告警,本机已验证)
make -C book/examples/chapter-07/03-event-output
# 运行级待验证(root 档):同源双跑,对照输出形态
sudo ./book/examples/chapter-07/03-event-output/event_ringbuf   # 另一终端执行任意命令产生事件
sudo ./book/examples/chapter-07/03-event-output/event_perf
```

预期输出示例(运行级待验证):`[ringbuf] pid=… comm=…` 与 `[perf] pid=… comm=… (cpuN)`——事件内容相同,唤醒与轮询形态不同(见示例 README)。

**检查点**(自测后看提示):

1. ring buffer 里一条记录 submit 前半途返回,消费者会读到半成品吗?——提示:4.2 的发布语义与 pending_pos 扫描;忘了 submit 的记录最终由谁回收(超时机制,随内核演进)。
2. 64 核机器上 PERF_EVENT_ARRAY 开 64 格、但用户态只给 cpu0 挂了 fd,cpu3 上的事件会怎样?——提示:4.1 的四个出口之一;-ENOENT 不是「丢」,是「没发出去」。
3. 为什么 ringbuf 的 reserve 在 NMI 上下文用 trylock?——提示:4.2 读法第三拍;自旋等锁在 NMI 里的后果,对照二节站 3 的 per-CPU 判重入。

**独立示例**:两条通道的完整双实现见 [`03-event-output/`](../examples/chapter-07/03-event-output/)(编译级已验证);运行级验证步骤与预期输出在其 README。

## 可视化:六类映射与两条事件通道

**图 1:六类映射速查(矩阵⑥)。**

```text
            寻址方式      并发策略            内存策略           典型用途
hash        任意键→哈希    读 RCU+写桶锁       prealloc/按需      通用 KV(示例 01)
array       下标=顺序      下标独立,几乎免锁   建表一次配齐       配置表/统计槽(示例 02 的骨架)
LRU hash    任意键+逐出    同 hash             预分配元素池+牺牲者 连接追踪/缓存(三节 3.2)
per-CPU     下标×CPU 分身  本 CPU 独占,零锁    ncpu×value_size    高频计数(示例 02)
perf event  CPU 号→事件 fd 读侧轮询/写侧入环   每 CPU perf 环     事件通道(四节 4.1)
ring buffer 单共享环      单 prod_pos 定序    mmap 共享大页      事件通道(四节 4.2)
```

**解读(≥150 字)**:六类的差异可以压成两个问题——「键怎么找到值」与「并发怎么解」。前四行是存储类:hash 用任意键换灵活性,array 用下标换顺序与速度,LRU 在 hash 上加逐出策略,per-CPU 在两者之上按 CPU 复制值;后两行是通道类,不存储、只流转。内存列是第 5 章「fd→内存」链条的类型化展开:prealloc 一次性配齐、per-CPU 按核翻倍、ring buffer 的双数据区是为回绕免拷贝付的固定租金。教学建议:把这张表当索引用——每行都能在切片器五/六站或对应示例里找到源码现场。

**图 2:两条事件通道的数据流。**

```text
 BPF 程序(某 tracepoint 命中)
   │ bpf_perf_event_output          │ bpf_ringbuf_reserve→填→submit
   ▼                                ▼
 [每 CPU perf 环]×N          [单一 ring buffer(prod_pos 前移)]
   │ 各环独立、可能丢失              │ 全局有序、满则 reserve 失败
   ▼                                ▼
 用户态 perf_buffer__poll         用户态 ring_buffer__poll
 (perf_event_open 档位)          (mmap 直读,确认后推 cons_pos)
   │                                │
   └────────► 应用处理回调 ◄────────┘
```

**解读(≥150 字)**:图的左列是「按 CPU 分而治之」——事件留在发生的 CPU 上,用户态逐环轮询,好处是无争用、坏处是全局顺序不存在;右列是「单点定序」——`producer_pos` 一个原子量决定全局次序,用户态确认消费后推进 `consumer_pos`,空间归还即限流解除。两列在「应用处理回调」汇合,对上层完全等价——这就是示例 03 能用同一事件源双实现的原因。丢失语义的差异在图上看是两处小字:左列环满「丢并计数」,右列「reserve 失败」——前者丢在内核侧,后者丢在 BPF 程序侧,排错时的第一分叉点(排错节)。

## 常见问题与排错

### 运行类

**Q:示例 01/02 的计数一直是 0?**
A:三步排查:程序加载了吗(`bpftool prog list` 可见,需 root);attach 到的口对不对(XDP 挂 lo 后要有真实流量——另一终端 `ping 127.0.0.1`);BPF 侧边界检查是否让包提前返回了 XDP_ABORTED(帧长不足时计数被跳过,预期行为)。运行级内容,全部属预期档复现步骤。

**Q:`bpf_map_update_elem` 返回 -E2BIG?**
A:hash 表满。两条出路:加大 `max_entries`(评估内存,三节 3.3 的账本);或换 `BPF_MAP_TYPE_LRU_HASH` 让内核逐出旧键(三节 3.2)。注意 LRU 与 `BPF_F_NO_PREALLOC` 互斥(二节 2.4)。

**Q:加载报 Operation not permitted / EPERM?**
A:本机 `unprivileged_bpf_disabled=2`(00-env ③ 段实测)——非特权加载被内核直接拒绝,`sudo` 是唯一正道;root 下仍 EPERM 则查 cgroup/LSM 与 `RLIMIT_MEMLOCK`(libbpf 1.3 会先尝试抬 limit,失败时打印提示——本章三个示例的运行档都从这里起步)。

### 并发类

**Q:per-CPU 计数的「合计」每次都不一样大,是 bug 吗?**
A:不是。3.1 的快照语义:用户态整块拷回的瞬间计数仍在变,合计是近似值。要更「稳」的读数,把窗口拉长取趋势,或改读 per-CPU hash 的 `bpf_map_lookup_percpu_elem` 定点采样——精确性永远是相对某个时刻的。

**Q:多线程用户态读 hash,偶尔读到「旧值」?**
A:映射读是单键原子,但两次 `bpf_map_lookup_elem` 之间世界在变——这不是内核问题,是应用层一致性模型没想清楚。需要「读改写回」的原子性,要么搬进 BPF 侧(桶锁内完成,二节 2.3),要么用 `BPF_F_LOCK` 的元素级锁。

**Q:LRU 表越用越「年轻」?**
A:LRU 逐出的是「最近最少使用」,热键会被反复续命——观察到的现象通常是冷键消失、热键常驻,这正是设计目标。若热键也被逐出,先查 `max_entries` 是否远小于实际工作集(逐出风暴)。

### 走读类

**Q:切片器报 `STATION n: NOT FOUND`?**
A:锚点漂移(内核改名/移动/内联),处置同第 5/6 章:回树 grep 新位置、改锚点与偏移、同步更新校准注释。机检三类失败(文件不在树/符号未命中/格式错)的完整处置流程见第 5 章四节,不重复展开。

**Q:正文行号与我手头的树对不上?**
A:版本漂移。本章行号全部锚定 v6.6.78;换树后以符号 grep 重定位(机检按符号不按行号,`verify-citations.sh` 仍应全绿——它查的是符号在场,不是行号一致)。

### 编译类

**Q:`bpftool gen skeleton` 失败或 skeleton 与 .bpf.c 不同步?**
A:先 `make clean` 再全链重跑——skeleton 是 `.bpf.o` 的「影子头文件」,改了 BPF 侧代码而不重新生成是新手高频坑;映射名改了之后 loader 里 `skel->maps.<旧名>` 的编译错也源于此。

**Q:libbpf 链接错误(undefined reference to `bpf_*__open` 等)?**
A:三步:`-lbpf -lelf -lz` 三库都在;头文件版本与库版本一致(`pkg-config --modversion libbpf`);strict 模式符号(`_skeleton` 后缀族)需要 libbpf ≥0.6,本机 1.3.0 实测可用(00-env ② 段)。工具命令速查见[附录 B](../appendix/b-tool-command-index.md),多发行版安装指引见[附录 C](../appendix/c-distro-btf-build-guide.md)。

### 排错速查表

| 症状 | 第一疑点 | 出处 |
|---|---|---|
| 计数恒 0 | attach 口/流量/BPF 侧提前返回 | 运行类 |
| -E2BIG | 表满 → 扩容或换 LRU | 二节 2.4、三节 3.2 |
| 加载 EPERM | unprivileged_bpf_disabled=2 → sudo | 运行类 |
| 合计数跳动 | per-CPU 快照语义,非 bug | 三节 3.1 |
| STATION n: NOT FOUND | 锚点漂移 → 回树 grep | 走读类 |
| XDP attach 失败 | 口不支持 native XDP 或驱动限制 → 换 lo/veth(generic 模式降级) | 运行类 |
| 事件通道无输出 | perf 档位 / ringbuf 未 poll / 事件源未触发 | 四节 4.1/4.2 |

## 课后扩展任务

两项练习的完整任务书、提示与机检脚本在 `book/examples/chapter-07/exercises/`(任务与提示在前、参考思路在后;先修条件:示例 01/02 编译级跑通、两个切片器实跑过):

- **练习① 映射操作链查证**([`exercises/01-map-op-chain/`](../examples/chapter-07/exercises/01-map-op-chain/)):产出「从 `bpf(2)` 的 `map_update_elem` 到 `htab_map_update_elem`」的完整调用链表——每一跳给出文件:行号;`check-chain.sh` 对链上锚点逐个 grep 断言。练的是第 5 章「沿调用链走读」的手艺在 ops 时代的进阶:链条里要穿过类型注册表与接口表两张表。
- **练习② ringbuf 切片扩展**([`exercises/02-extend-ringbuf/`](../examples/chapter-07/exercises/02-extend-ringbuf/)):仿 `trace-map-internals.sh` 写 `trace-ringbuf.sh` 两站——`__bpf_ringbuf_reserve`(:408)区间与 `bpf_ringbuf_commit`+`bpf_ringbuf_notify`(:489/:150);`check-ringbuf.sh` 验收(锚点断言 + `STATIONS=2`)。练的是四节 4.2 的源码现场亲手再切一遍。

## 总结

五点内化,对应学习目标五条:

1. **helper 分发全链**:call 指令以 imm 为 id,经 `get_func_proto` 白名单查到 `bpf_func_proto` 说明书,最终落到以 `__bpf_call_base` 为基址的固定地址——「白名单随程序类型滚动」与「固定地址」是同一段机制的两面(一节)。
2. **hash 并发模型**:读免锁靠 RCU、写串行靠桶级自旋锁、重入防靠 per-CPU 计数、内存晚还靠 mem_alloc 的时代机制——无锁读的底气全在写侧纪律(二节)。
3. **更新三语义与窗口**:NOEXIST/EXIST/ANY 由桶锁内的 `check_flags` 原子裁决;示例 01 的「lookup→update」窗口是程序侧两步之间的,内核单次调用原子(二节 2.3、示例 01)。
4. **三个维度的变体**:per-CPU 用分身换零锁(近似快照读)、LRU 用逐出换容量弹性、memcg 用挂靠换内存问责——API 不变、ops 表后见真章(三节)。
5. **两条事件通道**:perf 按 CPU 分环(隔离、无全局序、perf 生态)、ringbuf 单环定序(全局顺序、两段式发布、mmap 直读)——默认 ringbuf,三个例外场景选 perf(四节、示例 03)。

第 7 章之后,程序「用」的东西全部拆过了:执行形态(第 6 章)、辅助函数与映射(本章)。第 8 章补最后一块基础设施——BTF 与 CO-RE:vmlinux.h 从哪来、类型 ID 怎么编址、`bpf_core_read` 的重定位如何在加载期完成(本章示例构建链里的 `bpftool btf dump` 就是它的入口)。

## 本章参考来源

- Linux v6.6.78 源码树(走读树,行号引用基准):`kernel/bpf/helpers.c`(helper 实现)、`kernel/bpf/hashtab.c`(hash/LRU 映射)、`kernel/bpf/arraymap.c`(数组族与 perf 事件数组)、`kernel/bpf/ringbuf.c`(环形缓冲区)、`kernel/bpf/bpf_lru_list.c`(LRU 逐出)、`kernel/trace/bpf_trace.c`(追踪族 helper 与 perf 输出)、`kernel/bpf/syscall.c`(注册表与记账)、`include/linux/bpf.h`(`bpf_map_ops`/`bpf_func_proto`)。
- 内核文档 `Documentation/bpf/`:`maps.rst`(各映射类型语义与标志的官方口径)、`ringbuf.rst`(环形缓冲区设计与 reserve/commit 语义)、`bpf_design_QA.rst`(helper 设计问答)。
- UAPI:`include/uapi/linux/bpf.h`(helper id 表 214 项、映射类型枚举、`BPF_F_NO_PREALLOC` 等标志)。
- libbpf:`bpf.h`(`bpf_map_get_next_key`/`bpf_map_lookup_elem` 等 syscall 包装)、`libbpf.h`(`perf_buffer__new`/`ring_buffer__new` 用户态接口)、`bpf_helpers.h`(SEC 命名)。
- 章节回指:第 2 章 4.1(八类映射速查)、第 3 章(helper 参数校验与失败画廊)、第 4 章 1.2(白名单随类型)、第 5 章三节(映射生命周期与文件地图)、第 6 章站五(helper 固定地址与 get_func_addr)、第 8 章(BTF/CO-RE,本章示例构建链的前瞻)。


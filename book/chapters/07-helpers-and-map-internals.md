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

**检查点**(自测后看提示):

1. 为什么 helper 调用不需要第二趟编译,而 bpf2bpf 子程序调用需要?——提示:回看 1.2 的「安放位置」论证与第 6 章站五的 `func_addr_fixed` 分支。
2. 同一个 `bpf_skb_output`,为什么 XDP 程序里是 `xdp_output` 的不同变体可见?——提示:白名单的粒度在「类型的 get_func_proto」,不在 id 本身;1.3 的 tracing 族是同构案例。
3. `gpl_only` 闸门检查的是「程序」还是「helper」的哪个属性?——提示:1.3 贴码最后一行的两个条件相与;许可证语义见第 1 章。

**独立示例**:本节五站已工具化——[`04-helper-dispatch/`](../examples/chapter-07/04-helper-dispatch/);helper 在 hash 映射上的两个消费现场见 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/)(BPF 侧 lookup/update)。

<!-- ============================================================
     Task 9 占位:二、hash 映射内部:并发与内存
     布局/无锁 RCU 查找/更新三语义与桶锁/prealloc/bpf_mem_alloc 回收
     消费:05-map-internals 站 1-5;示例 01
============================================================ -->

<!-- ============================================================
     Task 10 占位:三、per-CPU、LRU 与内存记账
     per-CPU 语义与聚合/percpu_array_map_ops/LRU 三链/memcg 记账
     消费:05-map-internals 站 6;示例 02
============================================================ -->

<!-- ============================================================
     Task 11 占位:四、事件输出:perf buffer 与 ring buffer(含可视化两图)
     消费:示例 03 双变体
============================================================ -->

<!-- ============================================================
     Task 12 占位:常见问题与排错、课后扩展任务、总结、本章参考来源
============================================================ -->

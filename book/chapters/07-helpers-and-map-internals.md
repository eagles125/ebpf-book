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

**第三种更新:`BPF_F_LOCK`**(值里带 `struct bpf_spin_lock` 的映射)值得单独一提:它走**元素级锁**在原位改值(不摘链、不分配),更新路径开头(:1124 起)有专门的快路——先无锁找元素、抓元素锁原位改、99.9% 到此为止;极小概率的「无锁时没找到、锁内找到了」再回落桶锁路径(上面贴码的后半就是这个兜底)。两级锁之外的第三级,粒度细到一个值。

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

**独立示例**:hash 闭环见 [`01-hash-lookup-update/`](../examples/chapter-07/01-hash-lookup-update/);本节源码现场工具化在 [`05-map-internals/`](../examples/chapter-07/05-map-internals/)(六站中的前五站)。


## 三、per-CPU、LRU 与内存记账

二节聚焦「一个 hash 映射内部的并发」,本节把镜头拉远:同一种「键值存储」语义在三个维度的变体——per-CPU(并发维度)、LRU(容量维度)、记账(内存维度)。三个变体共享二节的 ops 框架,**换的是 ops 表,不变的是框架**——这是第 5 章「两层皮」设计在映射家族里的体现。

### 3.1 per-CPU:分身计数,聚合归还

per-CPU 映射把「一份共享值」变成「每 CPU 一份分身」:`BPF_MAP_TYPE_PERCPU_ARRAY`(include/uapi/linux/bpf.h:930,v6.6.78)与 `BPF_MAP_TYPE_PERCPU_HASH`,ops 分别是 `percpu_array_map_ops`(kernel/bpf/arraymap.c:775,v6.6.78)等。语义三条:

- **BPF 侧只摸本 CPU**:`bpf_map_lookup_elem` 返回**当前 CPU** 槽位指针——示例 [`02-percpu-counter/`](../examples/chapter-07/02-percpu-counter/) 的收包计数因此零锁零争用;
- **用户态读是整块拷回**:同一条 `bpf_map_lookup_elem` 从用户态发出,一次拷回 `CPU 数 × value_size` 的全部槽位,聚合(求和/取最大)由用户自己做——读出的是**近似快照**(逐槽拷贝之间计数还在变);
- **跨 CPU 读有专用 helper**:`bpf_map_lookup_percpu_elem`(在公共兜底 `bpf_base_func_proto` 里,一节 1.3 贴码可见)带 CPU 号参数,程序可读**指定 CPU** 的分身——per-CPU 聚合统计的补丁通道。

代价模型一句话:**用 `CPU 数 × value_size` 的内存与用户态聚合步骤,换 BPF 侧的零锁**。高频计数(perf 场景)近乎总是划算;键空间巨大时(`ncpu × max_entries × value_size`)要算一算内存账——这正好引出 3.3。

### 3.2 LRU:满员之后的第三种答案

hash 表满了怎么办?二节 2.4 给了两条路:prealloc 表报 -E2BIG,`NO_PREALLOC` 表看分配器脸色。**LRU**(least recently used,最近最少使用)给第三种答案:满员时**自动逐出最旧的键**,新键永远进得来。实现上它仍是 hash——`htab_lru_map_ops`(kernel/bpf/hashtab.c:2301,v6.6.78;切片器站 1 的注册表里同族注册)把 update/delete 换成 `htab_lru_map_update_elem`/`htab_lru_map_delete_elem`:更新前若满,先按 LRU 挑一个牺牲者、经 `bpf_lru_pop_free`(kernel/bpf/bpf_lru_list.c:499,v6.6.78)拿回一个空闲元素,再走与普通 hash 相同的锁内挂链。

逐出的组织结构(与二节同思想:per-CPU 化避免争用):

- **全局三链**:`BPF_LRU_LIST_T_ACTIVE / INACTIVE / FREE`(kernel/bpf/bpf_lru_list.h:17-19,v6.6.78)——活跃/不活跃/空闲,近似两时钟算法;全局链用一把 `raw_spinlock_t` 且 `____cacheline_aligned_in_smp`(:37)独占 cacheline;
- **per-CPU 本地链**:`bpf_lru_locallist`(bpf_lru_list.h:40-48,v6.6.78,FREE/PENDING 两态)——各 CPU 先在本地攒「待归还」,攒够或需要时再汇入全局,把逐出操作的锁争用摊薄到各 CPU。

`BPF_MAP_TYPE_LRU_HASH`(include/uapi/linux/bpf.h:933,v6.6.78)是连接追踪、限流、缓存类负载的默认选型;`lookup_and_delete` 变体(`htab_lru_map_lookup_and_delete_elem`,2.3 的 ops 表可见)支持「读出即逐出」的一次性票据语义。

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

<!-- ============================================================
     Task 11 占位:四、事件输出:perf buffer 与 ring buffer(含可视化两图)
     消费:示例 03 双变体
============================================================ -->

<!-- ============================================================
     Task 12 占位:常见问题与排错、课后扩展任务、总结、本章参考来源
============================================================ -->

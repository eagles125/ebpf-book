# 第 6 章 JIT 编译与性能优化

> 本章独立示例已落地到 [`book/examples/chapter-06/`](../examples/chapter-06/):[`00-env/`](../examples/chapter-06/00-env/) 环境自检(sysctl 可读性分级诊断)、[`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/) 反汇编锚点(源码→BPF 字节码,只编译不加载)、[`02-jit-x86/`](../examples/chapter-06/02-jit-x86/) 与 [`03-jit-arm64/`](../examples/chapter-06/03-jit-arm64/) 两个源码切片器、[`scripts/`](../examples/chapter-06/scripts/) 机检脚本(`exercises/` 课后练习随章节推进落地,状态见该目录 README)。本章是 JIT 源码走读章:源码摘录与行号以 v6.6.78 走读树实测为准;唯一的构建动作是 01-bpf-asm 的 `clang -target bpf` 编译——只编译、不加载,无挂载风险。

## 学习目标

读完本章后,你应当能够:

- 说出程序通过验证后在内核里的三种执行形态——解释器(interpreter)、JIT(just-in-time,即时编译)、offload(硬件卸载)——以及选通入口 `bpf_prog_select_runtime` 的判定顺序(一节 1.1)。
- 讲清解释器的两个实现技巧:256 项跳转表的调度循环(computed goto),以及「按栈长分 16 档、宏展开特化」的解释器入口表(一节 1.1)。
- 用成本结构解释 JIT 为什么值得默认开启:一次编译与每条解码的对比、间接跳转对分支预测的伤害、固定开销前移对高频路径的意义(一节 1.2)。
- 对照源码说出 `bpf_jit_enable`/`bpf_jit_kallsyms`/`bpf_jit_harden`/`bpf_jit_limit` 四个 sysctl 的内核出处、语义与默认值来源,并解读本机 `CONFIG_BPF_JIT_ALWAYS_ON=y` 对这排开关的改写(一节 1.3 与 1.4)。
- 用 `00-env/check-env.sh` 对本机做只读自检,把 sysctl 的「读得到/读不到」分级对回文件权限,并分清走读树(6.6.78)与运行内核(7.0.0-38)两个对象(一节 1.5、四节)。

## 本章导读

第 5 章把 eBPF 子系统的源码地图铺开时,「发动机」区只开到门口:通用选通骨架 `bpf_prog_select_runtime`(`kernel/bpf/core.c:2357`,v6.6.78)与 `__weak` 兜底桩看过,`__weak` 的链接语义也讲了——架构提供同名强符号时,链接期换掉兜底。但强符号那一侧的房间一直没进:架构后端拿到验证过的字节码之后,究竟怎样把它变成 x86 或 arm64 的机器码?这就是本章的正题。放在程序生命周期里说(第 4 章总结的桥):加载→验证之后是第三步——**验证通过的字节码变成本机指令**;这一步发生什么、花什么代价、受哪些开关管,本章逐层拆开。

走读对象与验证口径沿用第 5 章:走读树 v6.6.78,行号一律标注「(v6.6.78)」,每条行号都应能用一条 grep 重现。本章比第 5 章多一件东西——一件**编译级真实物**:[`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/) 把一个 30 余行的 XDP 程序编译成 BPF 目标码并反汇编,14 条指令逐行对回源码。第 5 章的贴码全是「内核里别人写的代码」,这 14 条指令则是**你自己编译出来的字节码**——第 6 章要读的 JIT,输入就是它们。验证状态三档沿用:源码摘录与行号是静态事实;`check-env.sh` 输出、机检与反汇编是本机实测;凡涉及加载与运行的描述一律「预期输出示例 / 运行级待验证」。

最后把前文埋的预告逐条回收,共三点:

- 预告①(第 4 章总结的桥):「第 6 章(JIT 编译与性能优化)接手生命周期第三步——验证通过的字节码变成本机指令」→ 一节 1.1 讲清三种执行形态与选通逻辑,二节进 x86 后端六站,看「变成」二字如何发生。
- 预告②(第 2 章 1.8):「JIT 编译为本机指令,**架构相关**(第 6 章,源码地图见第 5 章)」→ 二节走 x86_64 后端、三节走 arm64 后端,「架构相关」四个字落到两套后端源码上:同构的框架、各异的指令选择。
- 预告③(第 5 章):「eBPF 指令如何逐条翻成本机码,深挖留第 6 章」「带着图 1 去读『发动机』区里你还没进过的那个房间」→ 二节六站兑现:从选通入口到映像分配,一次走完。

路线图:一节回答「程序有哪几种跑法、受哪排开关管」;二节沿 x86_64 JIT 从字节码到机器码走六站,是本章主重;三节用 arm64 做高层对照,验证「框架同构、后端各异」;四节实战——机检、反汇编、切片器三件套亲手跑通,并给出 `bpftool prog dump xlated/jited` 与性能对比的方法论;可视化、常见问题与排错、课后扩展任务随后收尾。

给两条读法提示。其一,**先动手再进二节**:把 [`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/) 的 `make && ./disasm.sh` 跑一遍,拿到那 14 条指令的二进制与反汇编输出——二节开头就要拿它当 JIT 的输入样本,先见过实物,后面贴的每一页内核代码都有了着落。其二,**三节允许先跳**:arm64 对照是给「想验证同构性」的读者的;赶时间的读者从二节直接跳四节也成立,但建议至少回看三节的对照表——「架构相关」四个字值得亲眼确认一遍。

## 一、三种执行形态与那排开关

### 1.1 三种执行形态:程序在内核里以什么样子跑

背景问题:验证器放行之后,`struct bpf_prog` 里装着的是一串 8 字节一组的 BPF 指令(第 2 章 2.1 的 RISC 虚拟机模型)。CPU 不认识 BPF 指令——接下来它是怎么被执行的?答案是三选一:**解释器**逐条取指执行、**JIT** 在加载时翻译成本机指令、**offload** 交给网卡固件去执行。选谁,由 `bpf_prog_select_runtime`(`kernel/bpf/core.c:2357`,v6.6.78;第 5 章二节站 4 已贴过它的门面)说了算,判定顺序写死在代码里:offload 优先(`bpf_prog_is_offloaded(fp->aux)` 为真时整个 JIT 分支被跳过,程序由驱动侧交给硬件,本节一句带过);否则尝试 JIT(`fp = bpf_int_jit_compile(fp)`,`core.c:2384`);JIT 不可用且无必须 JIT 的理由,回退解释器。

**形态一:解释器——内核里的虚拟 CPU。**解释器的本体是 `___bpf_prog_run`(`core.c:1679`,v6.6.78):一个手写的调度循环,注释一句话说清职责——"Decode and execute eBPF instructions"。它的核心数据结构是一张 256 项的跳转表:

```c
/* kernel/bpf/core.c:1679-1692(v6.6.78) */
static u64 ___bpf_prog_run(u64 *regs, const struct bpf_insn *insn)
{
#define BPF_INSN_2_LBL(x, y)    [BPF_##x | BPF_##y] = &&x##_##y
#define BPF_INSN_3_LBL(x, y, z) [BPF_##x | BPF_##y | BPF_##z] = &&x##_##y##_##z
	static const void * const jumptable[256] __annotate_jump_table = {
		[0 ... 255] = &&default_label,
		/* Now overwrite non-defaults ... */
		BPF_INSN_MAP(BPF_INSN_2_LBL, BPF_INSN_3_LBL),
		/* Non-UAPI available opcodes. */
		[BPF_JMP | BPF_CALL_ARGS] = &&JMP_CALL_ARGS,
		[BPF_JMP | BPF_TAIL_CALL] = &&JMP_TAIL_CALL,
		[BPF_ST  | BPF_NOSPEC] = &&ST_NOSPEC,
		[BPF_LDX | BPF_PROBE_MEM | BPF_B] = &&LDX_PROBE_MEM_B,
		[BPF_LDX | BPF_PROBE_MEM | BPF_H] = &&LDX_PROBE_MEM_H,
		[BPF_LDX | BPF_PROBE_MEM | BPF_W] = &&LDX_PROBE_MEM_W,
		[BPF_LDX | BPF_PROBE_MEM | BPF_DW] = &&LDX_PROBE_MEM_DW,
```

每一项把一个 BPF 操作码映射到一个**代码标签的地址**(`&&label` 是 GCC 的 computed goto 扩展——取标签地址当值用)。调度点在 `core.c:1707`(v6.6.78):`goto *jumptable[insn->code];`——取指、查表、跳转、执行、取下一条,循环往复。表底 `[0 ... 255] = &&default_label` 先全部填成默认标签,再用 `BPF_INSN_MAP` 把 UAPI 里每个合法操作码覆写成自己的处理标签:非法操作码落进默认出口,这是解释器的安全网之一。返回值就是虚拟寄存器 `BPF_R0` 的值——注释写明 "Return: whatever value is in %BPF_R0 at program exit"。

这个循环前面还有一层技巧值得看:**按栈长分档的解释器入口**。解释器在 C 栈上给程序开一块栈空间(`u64 stack[stack_size / sizeof(u64)]`),这块数组的大小在编译期就定死了——而每个程序的栈深(`aux->stack_depth`,验证器算出)要到加载期才知道。内核的做法是为 32 到 512 字节共 16 个栈长档,用宏批量特化 16 个入口函数,再聚成一张数组:

```c
/* kernel/bpf/core.c:2220-2230(v6.6.78) */
#define PROG_NAME_LIST(stack_size) PROG_NAME(stack_size),

static unsigned int (*interpreters[])(const void *ctx,
				      const struct bpf_insn *insn) = {
EVAL6(PROG_NAME_LIST, 32, 64, 96, 128, 160, 192)
EVAL6(PROG_NAME_LIST, 224, 256, 288, 320, 352, 384)
EVAL4(PROG_NAME_LIST, 416, 448, 480, 512)
};
```

`EVAL6`/`EVAL4` 是把宏套宏展开的计数工具,展开后这张数组就是 `__bpf_prog_run32` 到 `__bpf_prog_run512` 十六个函数的集合——每个函数的 C 栈数组尺寸恰好等于档位。选档在 `bpf_prog_select_func`(`core.c:2335`,v6.6.78):`interpreters[(round_up(stack_depth, 32) / 32) - 1]`,把验证器算出的栈深向上取整到 32 字节粒度、直接当数组下标。同一个调度循环,十六份栈布局特化——这是「用宏给 C 手搓模板」的教科书案例,也是解释器侧为数不多的性能抠法:**栈初始化的成本随档位精确匹配,不多清一页也不少清一页**。

**形态二:JIT——加载时翻译,运行时原生执行。**`bpf_prog_select_runtime` 调 `bpf_int_jit_compile(fp)`(`core.c:2384`,v6.6.78)时,解析到的是架构后端的**强符号**——第 5 章讲过,`core.c:2884` 的 `__weak` 桩只是兜底,x86_64 上真身是 `arch/x86/net/bpf_jit_comp.c:2754` 的 `bpf_int_jit_compile`(3020 行文件,门面在第 5 章已见)。它把 BPF 指令逐条翻译成 x86 指令、放进一段可执行的内核内存里,再把 `fp->bpf_func` 指过去——此后每次触发,CPU 直接执行这段原生指令。后端内部如何逐条翻译,是二节六站的正题。

**形态三:offload——程序不在 CPU 上跑。**一句带过:`bpf_prog_is_offloaded` 为真时,`select_runtime` 跳过软件执行形态,程序经驱动侧(`net/core/offload.c` 一族)交给网卡固件编译执行;这条路线需要专用硬件与驱动支持,本书不展开,记住「第三种形态存在、判定优先级最高」即可。

### 1.2 为什么 JIT 重要:成本结构的三笔账

如果解释器也能跑对,JIT 图什么?把成本摊开是三笔账。

**第一笔:每条指令的分派开销。**解释器每执行一条指令,都要付「取指 + 查表 + 间接跳转」三步;JIT 把这三步在**加载时一次付清**——编译 N 条指令,此后 M 次执行全是原生指令直接跑。总量对比:解释器约 `O(N×M)` 次分派动作,JIT 是 `O(N)` 编译 + `O(M)` 原生执行。M 越大,这笔账越向 JIT 倾斜——网络数据面的程序每个包都执行一遍,M 是天文学数字。

**第二笔:分支预测与流水线。**跳转表的 `goto *jumptable[insn->code]` 是一次**间接跳转**,目标地址随操作码变化,CPU 的分支预测器只能按历史猜测;猜错一次,流水线清空一次。原生指令流里,跳转目标编译期就编码成相对偏移,预测命中率天差地别。数据面程序对时延敏感,流水线清空不是「理论开销」,是每个包都在付的钱。

**第三笔:缓存足迹。**BPF 指令定长 8 字节,其中大量位花在编码规整上;翻译成 x86 后常用指令 2–5 字节,同样的程序逻辑占的指令缓存(i-cache)更小——热路径里,更小的足迹意味着更多的热代码留在缓存里。顺带一提,1.1 的栈长分档在解释器侧已经把「栈初始化」这笔开销抠到最紧,但**分派本身的开销它无法消除**——这是形态差异,不是实现差异。

三笔账合成一句:**JIT 是把「每包执行」的开销换成「一次加载」的开销**——固定成本前移。对高频路径(XDP/TC 每包、tracepoint 每事件)是暴利;对一次性诊断类程序则几乎无感。这也解释了为什么现代发行版倾向把 JIT 做成默认开启(1.3 的 `CONFIG_BPF_JIT_DEFAULT_ON`):对主流负载,解释器是兜底而不是选项。

### 1.3 那排开关:`bpf_jit_*` 四兄弟

JIT 的行为受一排 sysctl 开关管,声明集中在 `kernel/bpf/core.c:560-566`(v6.6.78),注释原话 "All BPF JIT sysctl knobs here":

```c
/* kernel/bpf/core.c:560-566(v6.6.78) */
#ifdef CONFIG_BPF_JIT
/* All BPF JIT sysctl knobs here. */
int bpf_jit_enable   __read_mostly = IS_BUILTIN(CONFIG_BPF_JIT_DEFAULT_ON);
int bpf_jit_kallsyms __read_mostly = IS_BUILTIN(CONFIG_BPF_JIT_DEFAULT_ON);
int bpf_jit_harden   __read_mostly;
long bpf_jit_limit   __read_mostly;
long bpf_jit_limit_max __read_mostly;
```

四个开关各自的语义与用途:

| 开关 | 类型与默认 | 语义 | 本章出场位置 |
|---|---|---|---|
| `bpf_jit_enable` | int;`IS_BUILTIN(CONFIG_BPF_JIT_DEFAULT_ON)` | JIT 总开关:0=关(回退解释器)、1=开;>1 时各后端编译收尾把结果 dump 进内核日志(arm64 侧见 `arch/arm64/net/bpf_jit_comp.c:1637` 的 `bpf_jit_dump` 调用,v6.6.78) | 一节 1.4、四节⑤层 |
| `bpf_jit_kallsyms` | int;同上默认 | JIT 映像是否以 `bpf_prog_<tag>_<name>` 形式的符号暴露进 `/proc/kallsyms`;排查「程序 JIT 到哪段地址」时配合 root 使用 | 四节⑤层 |
| `bpf_jit_harden` | int;默认 0 | 常量致盲(constant blinding):0=关;1=仅对非特权程序生效;2=对所有程序生效。开启后 JIT 前先改写含敏感立即数的指令,防 JIT 喷射攻击 | 二节站六、排错节 |
| `bpf_jit_limit` | long;默认 0(运行时初始化) | 全局 JIT 内存配额(字节):JIT 映像分配受此限额约束,耗尽后新的 JIT 分配被拒 | 排错节 |

默认值那列值得停一下:`IS_BUILTIN(CONFIG_BPF_JIT_DEFAULT_ON)` 是编译期求值——**开关的默认值由内核配置决定**,而配置里又有三层:`CONFIG_BPF_JIT`(基础设施编不编进来)、`CONFIG_BPF_JIT_ALWAYS_ON`(无条件 JIT,1.4 展开)、`CONFIG_BPF_JIT_DEFAULT_ON`(开机默认开)。本机三者全 `y`(见 [`00-env/`](../examples/chapter-06/00-env/) ④ 段实测)。

还有一个教学点藏在**文件权限**里:四个开关的 sysctl 表项都登记在 `net/core/sysctl_net_core.c`,但 `bpf_jit_enable` 是 0644(人人可读),`bpf_jit_harden`/`kallsyms`/`limit` 是 0600(仅 root)——同一个目录、同一排开关,非特权用户 `cat` 后三个直接 `EACCES`。「读不到」不等于「不存在」,这是权限位在说话,不是开关没编译进去。1.5 的分级诊断把这排开关的可见性摸了一遍。

### 1.4 本机实况:`ALWAYS_ON=y` 意味着什么

**教师视角。**打个比方:解释器像会议现场的人工口译——讲一句翻一句,每句都付翻译成本;JIT 像会前把讲稿整体翻译成中文印发——成本一次付清,与会者直接读。`CONFIG_BPF_JIT_ALWAYS_ON=y` 的含义是:**口译员根本没被允许进会场**——`kernel/bpf/core.c:1669`(v6.6.78)起,整段解释器代码包在 `#ifndef CONFIG_BPF_JIT_ALWAYS_ON` 里,配置为 y 时它们连内核映像都进不去;与此同时,墙上那块写着「JIT 开关」的牌子还在(`bpf_jit_enable` 的 sysctl 表项仍在),但已经被焊死在「开」——表项锁成 min=max=1,写 0 直接失败。它从性能开关退化成了安全声明:禁用的是「退回慢路径」的可能,而不是调节快慢。常见误区是把 ALWAYS_ON 读成「一个更大的开关值」——它不是档位,是**把解释器从构建产物里物理移除**。

把实测对回源码,看两条证据。其一,`bpf_prog_select_func`(`core.c:2335`,v6.6.78)的两世界分叉:

```c
/* kernel/bpf/core.c:2335-2343(v6.6.78) */
static void bpf_prog_select_func(struct bpf_prog *fp)
{
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
	u32 stack_depth = max_t(u32, fp->aux->stack_depth, 1);

	fp->bpf_func = interpreters[(round_up(stack_depth, 32) / 32) - 1];
#else
	fp->bpf_func = __bpf_prog_ret0_warn;
#endif
}
```

ALWAYS_ON 下 1.1 的 interpreters[] 数组根本不存在,`bpf_func` 先指向一个占位函数 `__bpf_prog_ret0_warn`;真正接手的是紧接着的 `bpf_prog_select_runtime`(`core.c:2357`):`IS_ENABLED(CONFIG_BPF_JIT_ALWAYS_ON)` 直接把 `jit_needed` 置真(:2367-2369),随后 `fp = bpf_int_jit_compile(fp)`(:2384)——JIT 失败且必须 JIT 时返回 `-ENOTSUPP`(:2386-2389,第 5 章站 4 已贴)。占位函数永远不会真被执行:**程序要么 JIT 成,要么加载失败**,没有第三条路。

其二,是本机 CONFIG 与 sysctl 的自洽(均为 [`00-env/`](../examples/chapter-06/00-env/) 实测):`/boot/config-7.0.0-38-generic` 里 `CONFIG_BPF_JIT=y`、`CONFIG_BPF_JIT_ALWAYS_ON=y`、`CONFIG_BPF_JIT_DEFAULT_ON=y`;`cat /proc/sys/net/core/bpf_jit_enable` 读到 1,与 DEFAULT_ON 一致。

对本章学习路径,这带来三条具体含义:①**本机无法实测「JIT 关」**——四节的 JIT on/off 性能对比只能给方法论,在可关 ALWAYS_ON 的内核上复现;②**1.1 贴的解释器代码在运行内核里并不存在**——读的对象是走读树 6.6.78,不是运行内核 7.0.0-38,两个对象从现在起要分得很清;③边界照例声明:内核配置项与开关语义随版本演进,以所用内核源码与 CONFIG 为准。

### 1.5 学生动手:三条命令摸一摸那排开关

不必 root,三条 `cat` 就能感受 1.3 说的可见性分级(命令与预期输出,均本机实测):

```bash
cat /proc/sys/net/core/bpf_jit_enable
# 预期输出:1
cat /proc/sys/net/core/bpf_jit_harden
# 预期输出:cat: '/proc/sys/net/core/bpf_jit_harden': Permission denied(EACCES)
cat /proc/sys/kernel/perf_event_paranoid
# 预期输出:4
```

三条读数三档含义:第一条可读且为 1——JIT 开着,与 `CONFIG_BPF_JIT_DEFAULT_ON=y` 自洽;第二条 `EACCES`——文件权限 0600,root-only,非特权读不到属**预期行为**;第三条 4——perf 取证的最严加固档,四节性能对比只能走方法论的直接原因。`00-env/check-env.sh` 把这套诊断做成了分级表(② 段逐项打印权限与解读),并顺手把走读树、工具链、CONFIG、BTF 五段一次查完:

```bash
bash book/examples/chapter-06/00-env/check-env.sh
```

**检查点**(自测后看提示):

1. 同一个程序,走解释器和走 JIT,执行结果可能不同吗?——提示:语义上等价(它们是同一份指令的两种执行方式);但注意二节站六:开启致盲时,**字节码**在 JIT 前会被改写——改写前后语义不变,这是下一章伏笔。
2. `interpreters[]` 为什么要按栈长分 16 档,而不是一个通用解释器?——提示:栈数组尺寸要编译期定死;通用版只能在运行时按 `aux->stack_depth` 初始化一块「最大可能」的栈,16 档让初始化成本精确匹配。
3. 在 `CONFIG_BPF_JIT_ALWAYS_ON=y` 的机器上把 `bpf_jit_enable` 写成 0,会发生什么?——提示:sysctl 表项锁 min=max=1,写直接失败;且解释器不在内核映像里,「关了退回解释器」这条路物理上不存在。

**独立示例**:本节的开关分级与五段自检已工具化——[`00-env/check-env.sh`](../examples/chapter-06/00-env/),只读、无 root 可跑;反汇编锚点(亲手产出字节码)见 [`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/)。

<!-- ============================================================
     Task 7 占位:二、x86_64 JIT:从字节码到机器码的六站
     六站:①选通与入口 ②do_jit 与两趟编译 ③emit_prologue 与寄存器映射
     ④指令 lowering 逐例 ⑤helper 调用与 tail call ⑥常量致盲与映像分配
     消费:02-jit-x86/trace-jit-x86.sh 六站输出;锚点:01-bpf-asm 14 条指令
============================================================ -->

<!-- ============================================================
     Task 8 占位:三、arm64:另一套后端的同构与差异
     四站:①编译入口 ②build_body 分派 ③bpf2a64 映射 ④与 x86 差异
     消费:03-jit-arm64/trace-jit-arm64.sh 四站输出
============================================================ -->

<!-- ============================================================
     Task 9 占位:四、实战:亲手摸到字节码与机器码(五层)
     ①依赖 ②「编译」环境=机检+反汇编 ③部署=定位 ④运行=两切片器
     ⑤效果=bpftool prog dump xlated/jited(预期)+性能对比方法论
============================================================ -->

<!-- ============================================================
     Task 10 占位:可视化(解释器 vs JIT 执行路径对比 + x86 六站流水图)、
     常见问题与排错、课后扩展任务、总结、本章参考来源
============================================================ -->

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

## 二、x86_64 JIT:从字节码到机器码的六站

本节是本章主重,也是导读预告③「深挖留第 6 章」的兑现处。走读方法与第 5 章一致,但多了一个抓手:每站与切片器 [`02-jit-x86/trace-jit-x86.sh`](../examples/chapter-06/02-jit-x86/) 的六站一一对应——正文贴的每一段码,切片器都能在你自己的树上原样切出来。输入样本统一用 [`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/) 的 14 条指令:JIT 读到什么、翻译成什么,全程拿它对号。每站四拍:签名(标注 v6.6.78 行号)→职责→关键代码段→失败出口。

### 站 1:选通与入口

**签名**:`bpf_int_jit_compile(struct bpf_prog *prog)`,定义在 `arch/x86/net/bpf_jit_comp.c:2754`(v6.6.78)。**职责**:接住 `core.c:2384` 的调用(第 5 章二节站 4 在此止步——「深挖」承诺现在兑现),做两件预备工作后把程序交给逐条翻译的主循环。

```c
/* arch/x86/net/bpf_jit_comp.c:2754-2773(v6.6.78) */
struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_binary_header *rw_header = NULL;
	struct bpf_binary_header *header = NULL;
	struct bpf_prog *tmp, *orig_prog = prog;
	struct x64_jit_data *jit_data;
	int proglen, oldproglen = 0;
	struct jit_context ctx = {};
	bool tmp_blinded = false;
	bool extra_pass = false;
	bool padding = false;
	u8 *rw_image = NULL;
	u8 *image = NULL;
	int *addrs;
	int pass;
	int i;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	/*
	 * If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter.
	 */
	if (IS_ERR(tmp))
		return orig_prog;
```

三个入口事实,对应三个失败出口。其一,`jit_requested` 门:内核对程序按「程序类型 + sysctl + cgroup 设置」判定是否请求 JIT,为假时原样返回——程序保持字节码走解释器(一节 1.1 的回退路径;在本机 ALWAYS_ON 内核上这条路不通,回指 1.4)。其二,`bpf_jit_blind_constants(prog)` 先行:常量致盲发生在一切翻译之前(站六展开);它返回的是**改写后的程序克隆**,`tmp != prog` 时后续编译用克隆。其三,失败出口:`IS_ERR(tmp)`——致盲失败立即返回原程序回退解释器;「编译还没开始,输在预处理」是 JIT 的第一类失败。

### 站 2:do_jit 与两趟编译

**签名**:`do_jit(struct bpf_prog *bpf_prog, int *addrs, u8 *image, u8 *rw_image, int oldproglen, struct jit_context *ctx, bool jmp_padding)`,定义在 `arch/x86/net/bpf_jit_comp.c:1075`(v6.6.78)。**职责**:JIT 的心脏——遍历每条 BPF 指令,查 `addrs[i]` 记录每条指令翻译后的机器码地址,逐条 lower 成 x86 指令序列。

```c
/* arch/x86/net/bpf_jit_comp.c:1075-1098(v6.6.78) */
static int do_jit(struct bpf_prog *bpf_prog, int *addrs, u8 *image, u8 *rw_image,
		  int oldproglen, struct jit_context *ctx, bool jmp_padding)
{
	bool tail_call_reachable = bpf_prog->aux->tail_call_reachable;
	struct bpf_insn *insn = bpf_prog->insnsi;
	bool callee_regs_used[4] = {};
	int insn_cnt = bpf_prog->len;
	bool tail_call_seen = false;
	bool seen_exit = false;
	u8 temp[BPF_MAX_INSN_SIZE + BPF_INSN_SAFETY];
	int i, excnt = 0;
	int ilen, proglen = 0;
	u8 *prog = temp;
	int err;

	detect_reg_usage(insn, insn_cnt, callee_regs_used,
			 &tail_call_seen);

	/* tail call's presence in current prog implies it is reachable */
	tail_call_reachable |= tail_call_seen;

	emit_prologue(&prog, bpf_prog->aux->stack_depth,
		      bpf_prog_was_classic(bpf_prog), tail_call_reachable,
		      bpf_prog->aux->func_idx != 0);
	push_callee_regs(&prog, callee_regs_used);
```

开局三步:寄存器活跃性分析(`detect_reg_usage`——算出哪些 BPF 被调用者保存寄存器真的被用到,决定序言要压栈哪些,没用的不压,省下运行时开销);调 `emit_prologue` 搭序言(注意它读的正是验证器算出的 `aux->stack_depth`,站三展开);压被调用者保存寄存器。此后进入逐条 switch——那是站四的领地。

**「两趟编译」从哪来?**看入口函数的另一段(`arch/x86/net/bpf_jit_comp.c:2795-2806`,v6.6.78):第二次进入时,若上一趟的 `jit_data` 里还存着 `addrs`/`image`/`header`,就整组恢复、置 `extra_pass = true` 并 `goto skip_init_addrs` 跳过重新分配——切片器站 2 切的就是这 12 行。为什么要两趟?**第一趟时,每条指令的机器码长度未知,所有「跳到第 i 条指令」的目标地址都写不准**;第一趟跑完,`addrs[]` 把每条指令的机器码地址固定下来,第二趟重放时地址才填得对。编译主循环(`arch/x86/net/bpf_jit_comp.c:2830-2844`,v6.6.78)正是这个「反复 do_jit 直到长度收敛」的循环:

```c
/* arch/x86/net/bpf_jit_comp.c:2830-2844(v6.6.78) */
	for (pass = 0; pass < MAX_PASSES || image; pass++) {
		if (!padding && pass >= PADDING_PASSES)
			padding = true;
		proglen = do_jit(prog, addrs, image, rw_image, oldproglen, &ctx, padding);
		if (proglen <= 0) {
out_image:
			image = NULL;
			if (header) {
				bpf_arch_text_copy(&header->size, &rw_header->size,
						   sizeof(rw_header->size));
				bpf_jit_binary_pack_free(header, rw_header);
			}
			/* Fall back to interpreter mode */
			prog = orig_prog;
			if (extra_pass) {
				prog->bpf_func = NULL;
				prog->jited = 0;
```

**失败出口**:`proglen <= 0`——任何一趟翻译失败(含站四的 `-EFAULT`),已分配的映像立即释放,注释原话 "Fall back to interpreter mode",程序退回字节码形态。翻译失败在正常程序上不应发生(操作码早被验证器放行,第 3 章),这条出口是给内核自身 bug 留的保险丝。

### 站 3:emit_prologue 与寄存器映射

**签名**:`emit_prologue(u8 **pprog, u32 stack_depth, bool ebpf_from_cbpf, bool tail_call_reachable, bool is_subprog)`,定义在 `arch/x86/net/bpf_jit_comp.c:343`(v6.6.78)。**职责**:生成每段 JIT 代码开头的固定序言——搭栈帧、留补丁位。但先看它的地基:**BPF 寄存器到 x86 寄存器的映射表**(`arch/x86/net/bpf_jit_comp.c:171-186`,v6.6.78):

```c
/* arch/x86/net/bpf_jit_comp.c:171-186(v6.6.78) */
static const int reg2hex[] = {
	[BPF_REG_0] = 0,  /* RAX */
	[BPF_REG_1] = 7,  /* RDI */
	[BPF_REG_2] = 6,  /* RSI */
	[BPF_REG_3] = 2,  /* RDX */
	[BPF_REG_4] = 1,  /* RCX */
	[BPF_REG_5] = 0,  /* R8  */
	[BPF_REG_6] = 3,  /* RBX callee saved */
	[BPF_REG_7] = 5,  /* R13 callee saved */
	[BPF_REG_8] = 6,  /* R14 callee saved */
	[BPF_REG_9] = 7,  /* R15 callee saved */
	[BPF_REG_FP] = 5, /* RBP readonly */
	[BPF_REG_AX] = 2, /* R10 temp register */
	[AUX_REG] = 3,    /* R11 temp register */
	[X86_REG_R9] = 1, /* R9 register, 6th function argument */
};
```

表里的数字不是寄存器名,是 **x86 编码号**(0–7 + REX 前缀扩展出 8–15,`is_ereg()` 据此判断要不要多编一个前缀字节)。对照第 2 章 2.1 的虚拟机模型,规律一目了然:BPF 的调用约定直接对齐 x86-64 的 System V ABI——r0 返回值→RAX,r1–r5 参数→RDI/RSI/RDX/RCX/R8,「调 helper 就是一次原生 call」的物理基础在此(第 4 章辅助函数白名单的落地);r6–r9→被调用者保存寄存器,helper 不会碰坏;帧指针 FP→RBP 只读。三个内部寄存器各有专职:AX→R10 供致盲(站六)、AUX→R11 是 JIT 搬运工、R9 留给蹦床。同表的 `reg2pt_regs[]`(:188–199)是同一映射在 `struct pt_regs` 偏移上的复制品,服务经典 BPF 兼容与上下文读取。

序言本体分两段(`arch/x86/net/bpf_jit_comp.c:343-353` 与 `:354-372`,v6.6.78):

```c
/* arch/x86/net/bpf_jit_comp.c:343-353(v6.6.78) */
static void emit_prologue(u8 **pprog, u32 stack_depth, bool ebpf_from_cbpf,
			  bool tail_call_reachable, bool is_subprog)
{
	u8 *prog = *pprog;

	/* BPF trampoline can be made to work without these nops,
	 * but let's waste 5 bytes for now and optimize later
	 */
	EMIT_ENDBR();
	memcpy(prog, x86_nops[5], X86_PATCH_SIZE);
	prog += X86_PATCH_SIZE;
```

```c
/* arch/x86/net/bpf_jit_comp.c:354-372(v6.6.78) */
	if (!ebpf_from_cbpf) {
		if (tail_call_reachable && !is_subprog)
			EMIT2(0x31, 0xC0); /* xor eax, eax */
		else
			EMIT2(0x66, 0x90); /* nop2 */
	}
	EMIT1(0x55);             /* push rbp */
	EMIT3(0x48, 0x89, 0xE5); /* mov rbp, rsp */

	/* X86_TAIL_CALL_OFFSET is here */
	EMIT_ENDBR();

	/* sub rsp, rounded_stack_depth */
	if (stack_depth)
		EMIT3_off32(0x48, 0x81, 0xEC, round_up(stack_depth, 8));
	if (tail_call_reachable)
		EMIT1(0x50);         /* push rax */
	*pprog = prog;
}
```

序言四件事,件件有主:**5 字节 nop**(`X86_PATCH_SIZE`)是补丁位——蹦床(bpf trampoline)与站五的直接尾调用都会从这里原地改写;`push rbp; mov rbp, rsp` 搭帧;`sub rsp, round_up(stack_depth, 8)` 开出验证器算好的程序栈(8 字节对齐);`X86_TAIL_CALL_OFFSET` 处的第二个 ENDBR 是尾调用跳板落点——站五的 `poke->adj_off` 直接引用它。`push rax` 专为尾调用计数腾位(站五)。**失败出口**:序言只做定长编码,恒成功——失败集中在站四的翻译与站六的分配。

### 站 4:指令 lowering 逐例

**职责**:把 14 条锚点指令这样的 BPF 指令逐条翻成 x86——`do_jit` 大 switch 的领地。下面四例覆盖四类基本动作,每例先给 BPF 侧(锚点程序实产)、再给内核 lowering 代码;x86 机器码一列为源码级预期(实际输出需加载后 `bpftool prog dump jited`,四节⑤层)。

**例① 立即数装载(ALU MOV K / LD IMM64)。**锚点指令 0/10/12 的 `r0 = 0x0/0x1/0x2`(机器码 `b7`)都走这条:

```c
/* arch/x86/net/bpf_jit_comp.c:1206-1215(v6.6.78) */
		case BPF_ALU64 | BPF_MOV | BPF_K:
		case BPF_ALU | BPF_MOV | BPF_K:
			emit_mov_imm32(&prog, BPF_CLASS(insn->code) == BPF_ALU64,
				       dst_reg, imm32);
			break;

		case BPF_LD | BPF_IMM | BPF_DW:
			emit_mov_imm64(&prog, dst_reg, insn[1].imm, insn[0].imm);
			insn++;
			i++;
			break;
```

`emit_mov_imm32`(`arch/x86/net/bpf_jit_comp.c:674` 起,v6.6.78)里藏着两处编码优化:立即数为 0 时改发 `xor %eax,%eax`(省 3 字节)——锚点的 `r0 = 0x0` 因此变两条指令变一条;立即数为正且无需符号传播时用 `mov %eax, imm32`(零扩展,省 2 字节)。64 位常量走 `BPF_LD | BPF_IMM | BPF_DW` 双宽指令,由 `emit_mov_imm64` 拼装。

**例② 存内存(STX MEM)。**

```c
/* arch/x86/net/bpf_jit_comp.c:1483-1488(v6.6.78) */
		case BPF_STX | BPF_MEM | BPF_B:
		case BPF_STX | BPF_MEM | BPF_H:
		case BPF_STX | BPF_MEM | BPF_W:
		case BPF_STX | BPF_MEM | BPF_DW:
			emit_stx(&prog, BPF_SIZE(insn->code), dst_reg, src_reg, insn->off);
			break;
```

**例③ 取内存(LDX MEM)。**锚点指令 1/2 的 `r2 = *(u32 *)(r1 + 0x4)`(机器码 `61`)的娘家在此——B/W/H/DW 四宽度、MEM 与 PROBE_MEM 两形态,8 个 case 标签共用一个出口:

```c
/* arch/x86/net/bpf_jit_comp.c:1491-1498(v6.6.78) */
		case BPF_LDX | BPF_MEM | BPF_B:
		case BPF_LDX | BPF_PROBE_MEM | BPF_B:
		case BPF_LDX | BPF_MEM | BPF_H:
		case BPF_LDX | BPF_PROBE_MEM | BPF_H:
		case BPF_LDX | BPF_MEM | BPF_W:
		case BPF_LDX | BPF_PROBE_MEM | BPF_W:
		case BPF_LDX | BPF_MEM | BPF_DW:
		case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
```

`PROBE_MEM`(探针版)是给「可能失效的内核指针」准备的:JIT 为它生成额外的地址边界修正代码,指针失效时跳过访存而不是崩溃——追踪族程序读内核结构体时,这是验证器静态检查之外的运行时保险(与第 3 章的边界检查互补)。

**例④ 条件跳转(JMP 比较族)。**锚点指令 5 的 `if r3 > r2`(`2d`)与指令 11 的 `if r1 == 0xdd86`(`15`)都出自这里。比较寄存器版(JMP_X)十个 case 标签连排(`arch/x86/net/bpf_jit_comp.c:1703-1712`,v6.6.78):

```c
/* arch/x86/net/bpf_jit_comp.c:1703-1712(v6.6.78) */
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JNE | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JLT | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JLE | BPF_X:
		case BPF_JMP | BPF_JSGT | BPF_X:
		case BPF_JMP | BPF_JSLT | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_X:
		case BPF_JMP | BPF_JSLE | BPF_X:
```

出口是同一对指令(`arch/x86/net/bpf_jit_comp.c:1723-1727`,v6.6.78)——一条 `cmp` 加一次条件跳转:

```c
/* arch/x86/net/bpf_jit_comp.c:1723-1727(v6.6.78) */
			/* cmp dst_reg, src_reg */
			maybe_emit_mod(&prog, dst_reg, src_reg,
				       BPF_CLASS(insn->code) == BPF_JMP);
			EMIT2(0x39, add_2reg(0xC0, dst_reg, src_reg));
			goto emit_cond_jmp;
```

`goto emit_cond_jmp` 指向 switch 后面的公共出口(把比较结果转成跳转偏移的那段)——连同 JMP32 十个标签,20 种比较共享一条编码逻辑,「多样性压在 case 标签、共性压在出口」。锚点指令 11 是 `BPF_JEQ | BPF_K`(立即数比较)变体,lowering 同族(立即数版在 `:1732` 起)。

**失败出口**:switch 的 `default` 直接 `return -EFAULT`(`arch/x86/net/bpf_jit_comp.c:1823-1824`,v6.6.78)——但如站二所说,验证过的程序到不了这里。

### 站 5:helper 调用与 tail call

**签名**:`bpf_jit_get_func_addr(const struct bpf_prog *prog, const struct bpf_insn *insn, bool extra_pass, u64 *func_addr, bool *func_addr_fixed)`,定义在 `kernel/bpf/core.c:1210`(v6.6.78)——通用层,所有架构共享。**职责**:给每条 `call` 指令算目标地址。第 4 章讲过辅助函数白名单跟着程序类型走;到了 JIT 阶段,问题变成「目标地址是多少、哪一趟才填得对」:

```c
/* kernel/bpf/core.c:1210-1234(v6.6.78) */
int bpf_jit_get_func_addr(const struct bpf_prog *prog,
			  const struct bpf_insn *insn, bool extra_pass,
			  u64 *func_addr, bool *func_addr_fixed)
{
	s16 off = insn->off;
	s32 imm = insn->imm;
	u8 *addr;
	int err;

	*func_addr_fixed = insn->src_reg != BPF_PSEUDO_CALL;
	if (!*func_addr_fixed) {
		/* Place-holder address till the last pass has collected
		 * all addresses for JITed subprograms in which case we
		 * can pick them up from prog->aux.
		 */
		if (!extra_pass)
			addr = NULL;
		else if (prog->aux->func &&
			 off >= 0 && off < prog->aux->func_cnt)
			addr = (u8 *)prog->aux->func[off]->bpf_func;
		else
			return -EINVAL;
	} else if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL &&
		   bpf_jit_supports_far_kfunc_call()) {
```

三类 `call`,三种地址策略。**helper 调用**(普通 `call`):地址恒定——`__bpf_call_base + imm`(函数体接续在 `:1235-1244`,helper 与基址都在核心内核,第一趟就能写对)。**BPF 子程序调用**(`BPF_PSEUDO_CALL`,bpf2bpf):第一趟只能填占位 `NULL`——子程序自己的机器码地址要等 `addrs[]` 收敛;`extra_pass` 为真才从 `prog->aux->func[off]->bpf_func` 取真地址。站二的两趟编译,一半理由就在这几行。**kfunc 调用**(`BPF_PSEUDO_KFUNC_CALL`):按内核是否支持远距 kfunc 调用分流。**失败出口**:子程序索引越界 `return -EINVAL`。

**尾调用(tail call)是另一族「跳过去就不回来」的调用**,x86 侧有两种 lowering。间接版(`arch/x86/net/bpf_jit_comp.c:520-543`,v6.6.78)先做运行时边界检查:

```c
/* arch/x86/net/bpf_jit_comp.c:520-543(v6.6.78) */
static void emit_bpf_tail_call_indirect(u8 **pprog, bool *callee_regs_used,
					u32 stack_depth, u8 *ip,
					struct jit_context *ctx)
{
	int tcc_off = -4 - round_up(stack_depth, 8);
	u8 *prog = *pprog, *start = *pprog;
	int offset;

	/*
	 * rdi - pointer to ctx
	 * rsi - pointer to bpf_array
	 * rdx - index in bpf_array
	 */

	/*
	 * if (index >= array->map.max_entries)
	 *	goto out;
	 */
	EMIT2(0x89, 0xD2);                        /* mov edx, edx */
	EMIT3(0x39, 0x56,                         /* cmp dword ptr [rsi + 16], edx */
	      offsetof(struct bpf_array, map.max_entries));

	offset = ctx->tail_call_indirect_label - (prog + 2 - start);
	EMIT2(X86_JBE, offset);                   /* jbe out */
```

直接版(`arch/x86/net/bpf_jit_comp.c:594-606`,v6.6.78)多了计数检查——尾调用链上限 33 层(`MAX_TAIL_CALL_CNT`),防程序互调成环:

```c
/* arch/x86/net/bpf_jit_comp.c:594-606(v6.6.78) */
static void emit_bpf_tail_call_direct(struct bpf_jit_poke_descriptor *poke,
				      u8 **pprog, u8 *ip,
				      bool *callee_regs_used, u32 stack_depth,
				      struct jit_context *ctx)
{
	int tcc_off = -4 - round_up(stack_depth, 8);
	u8 *prog = *pprog, *start = *pprog;
	int offset;

	/*
	 * if (tail_call_cnt++ >= MAX_TAIL_CALL_CNT)
	 *	goto out;
	 */
	EMIT2_off32(0x8B, 0x85, tcc_off);             /* mov eax, dword ptr [rbp - tcc_off] */
	EMIT3(0x83, 0xF8, MAX_TAIL_CALL_CNT);         /* cmp eax, MAX_TAIL_CALL_CNT */
```

选哪条路,分派点一句话(`arch/x86/net/bpf_jit_comp.c:1687-1700`,v6.6.78):立即数非零走直接版(目标在 `poke_tab` 里静态已知),否则走间接版(运行时查 `bpf_array`):

```c
/* arch/x86/net/bpf_jit_comp.c:1687-1700(v6.6.78) */
		case BPF_JMP | BPF_TAIL_CALL:
			if (imm32)
				emit_bpf_tail_call_direct(&bpf_prog->aux->poke_tab[imm32 - 1],
							  &prog, image + addrs[i - 1],
							  callee_regs_used,
							  bpf_prog->aux->stack_depth,
							  ctx);
			else
				emit_bpf_tail_call_indirect(&prog,
							    callee_regs_used,
							    bpf_prog->aux->stack_depth,
							    image + addrs[i - 1],
							    ctx);
			break;
```

**失败出口**(运行时语义):越界或计数耗尽,两种 lowering 都用条件跳转把控制流送到 `out` 标签——尾调用**静默不发生**,主程序继续按原路径执行;这是设计好的降级,不是错误。

### 站 6:常量致盲与映像分配

**为什么需要致盲?**BPF 指令里的立即数是用户可控字节——JIT 后它们原样躺进可执行内存,构成 JIT 喷射类攻击的原料; Spectre 类瞬态执行还可能把立即数当跳板。防御是 `bpf_jit_harden`(一节 1.3):开启后,**JIT 前先把含敏感立即数的指令改写掉**——通用层实现在 `kernel/bpf/core.c` 的 `bpf_jit_blind_constants`(:1428,遍历入口)逐条调 `bpf_jit_blind_insn`(:1250,单条改写)。看单条改写的核心(`kernel/bpf/core.c:1279-1300`,v6.6.78):

```c
/* kernel/bpf/core.c:1279-1300(v6.6.78) */
	if (from->dst_reg == BPF_REG_AX || from->src_reg == BPF_REG_AX)
		goto out;

	if (from->imm == 0 &&
	    (from->code == (BPF_ALU   | BPF_MOV | BPF_K) ||
	     from->code == (BPF_ALU64 | BPF_MOV | BPF_K))) {
		*to++ = BPF_ALU64_REG(BPF_XOR, from->dst_reg, from->dst_reg);
		goto out;
	}

	switch (from->code) {
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU | BPF_OR  | BPF_K:
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
		*to++ = BPF_ALU32_IMM(BPF_MOV, BPF_REG_AX, imm_rnd ^ from->imm);
		*to++ = BPF_ALU32_IMM(BPF_XOR, BPF_REG_AX, imm_rnd);
		*to++ = BPF_ALU32_REG_OFF(from->code, from->dst_reg, BPF_REG_AX, from->off);
		break;
```

三拍读法:①AX(即站三映射表里 R10)是用户不可见的临时寄存器,源指令已占用 AX 的直接跳过;②立即数为 0 的 MOV 有特例——改写成 `xor dst,dst`,零值无需保密;③其余立即数指令被拆成**随机异或对**:先 `mov AX, imm_rnd ^ imm`,再 `xor AX, imm_rnd`,两条之后 AX 里恰好是原立即数,原指令改为操作 AX——明文立即数从此不在指令流里出现,每次加载随机数 `imm_rnd` 都不同。对照锚点程序:`r0 = 0x0` 会命中②(变 `xor`);`if r1 == 0xdd86` 的立即数会被拆对(JMP_K 族在 `:1318` 起,回跳还有偏移补偿,`:1329`)。**教学点:致盲后的字节码与源码对不上是预期行为**——语义不变,形态全非,排错节回收这一点。

遍历入口 `bpf_jit_blind_constants`(`kernel/bpf/core.c:1428-1443`,v6.6.78)只做三件事:过 `blinding_requested` 门(未请求或已致盲就原样返回)、`bpf_prog_clone_create` 克隆程序(原程序不动,克隆失败 `return ERR_PTR(-ENOMEM)`)、对克隆循环逐条调 `bpf_jit_blind_insn` 改写。**失败出口**:`ERR_PTR(-ENOMEM)`——正是站一入口里 `IS_ERR(tmp)` 接住的那个错误;致盲失败不硬闯,回退解释器。

最后一站是**映像分配**:翻译好的机器码要放进一段最终只读可执行的内存。散页分配(每程序一段)在 6.6 已让位给 `bpf_prog_pack`——多程序共享 2MB 大页、位图分块管理(`struct bpf_prog_pack`,`kernel/bpf/core.c:850-854`,v6.6.78)。分配入口 `bpf_prog_pack_alloc`(`kernel/bpf/core.c:904-918`,v6.6.78)走两条路:超过 `BPF_PROG_PACK_SIZE` 的大程序独占整页——先 `bpf_fill_ill_insns` 填满非法指令、`set_vm_flush_reset_perms` 挂上释放复位标记、`set_memory_rox` 转只读可执行;小程序则在共享大页里 `bitmap_find_next_zero_area` 找位图空位切块(切片器站 6 切的是前 15 行)。**失败出口**:分配失败沿调用链上报,程序回退解释器——与致盲失败同一个兜底。

### 二节收束:双视角与切片器

**教师视角:两趟编译与一次解码的经济学。**六站连起来,「前移」二字有了具象:两趟编译把「跳转目标写不准」在加载时解决,代价是加载多走一趟,换来此后每次执行免掉间接分派(一节 1.2 的三笔账)。类比排版:第一遍校对定下各章节页码(`addrs[]`),第二遍才能把目录页码填对——没人说「校对两遍是浪费」,因为错页码的目录每天被查千万次。两个常见误区:把两趟理解成「编译器笨」——它是固定点迭代,长度收敛即停;以为 helper 调用也要两趟——固定地址第一趟就准,只有子程序调用在等 `addrs[]`。

**学生动手**:三条命令把六站在自己的树上复现——

```bash
# ①六站全量切片(行号即本节引用事实)
bash book/examples/chapter-06/02-jit-x86/trace-jit-x86.sh
# ②只看某一站(例如站 3)
bash book/examples/chapter-06/02-jit-x86/trace-jit-x86.sh | sed -n '/^## 站 3/,/^## 站 4/p'
# ③换树自检(负路径:给一个不存在的树,应报错 exit 1)
WALK_TREE=/nonexistent bash book/examples/chapter-06/02-jit-x86/trace-jit-x86.sh
```

预期输出:①六站各带「定位/代码段/职责」结构,汇总行 `TREE_VERSION=v6.6.78 STATIONS=6`,exit 0;②输出站 3 的寄存器映射表与序言两段;③`走读树目录不存在:/nonexistent`,exit 1(均本机实测)。

**检查点**(自测后看提示):

1. 序言开头为什么要放 5 字节 nop,而不是直接 `push rbp`?——提示:站三的 `X86_PATCH_SIZE` 补丁位是给谁留的?回看站五 direct 版的 `poke->tailcall_bypass` 与蹦床:原地改写需要一段**等长**的占位。
2. 第一趟编译时,helper 调用与 bpf2bpf 子程序调用的目标地址各填了什么?——提示:回看站五 `get_func_addr`——`func_addr_fixed` 的分支条件;固定地址(helper)第一趟就准,占位(NULL)等第二趟。
3. 打开 `bpf_jit_harden=2` 后,同一程序 JIT 出的机器码每次加载都不同——对吗?对在哪、不对在哪?——提示:站六的 `imm_rnd` 每次加载随机,异或对形态不同;但语义等价,程序行为不变。

**独立示例**:本节六站的全部代码段与行号已工具化——[`02-jit-x86/`](../examples/chapter-06/02-jit-x86/),切片器只读源码树、不构建不加载;JIT 的输入样本(14 条字节码)见 [`01-bpf-asm/`](../examples/chapter-06/01-bpf-asm/)。


## 三、arm64:另一套后端的同构与差异

本节按「高层对照」读 arm64 后端(`arch/arm64/net/bpf_jit_comp.c`,2303 行,v6.6.78)——不逐指令深挖,目标是验证一个命题:**二节的六站结构是框架属性,不是 x86 私有**。切片器 [`03-jit-arm64/trace-jit-arm64.sh`](../examples/chapter-06/03-jit-arm64/) 的四站与本节对应;每站职责注都写明「与 x86 对照」。

### 3.1 入口同构:换个后端,骨架没换

arm64 的编译入口 `bpf_int_jit_compile`(`arch/arm64/net/bpf_jit_comp.c:1518`,v6.6.78)与 x86 站一的入口**签名逐字相同**,开头几拍也一致:先问 `prog->jit_requested`(`:1530`),再 `bpf_jit_blind_constants(prog)` 做致盲预处理,失败同样 `return orig_prog` 回退。这不是巧合:通用层(core.c)与架构层(arch/*/net/)的分界就是这一个强符号加少量架构回调——**「框架与后端分层」是内核对 JIT 的架构裁决**,谁来实现谁就继承这套骨架。指令遍历同样同构:`build_body`(`:1442`)循环调 `build_insn`(`:761`)逐条 lower,`extra_pass` 两趟语义原样保留(`build_body` 的第二个参数就是它)——对应 x86 的 do_jit 与站二。

### 3.2 bpf2a64:第二张寄存器映射表

寄存器映射是两套后端差异最直观的地方。arm64 的映射表 `bpf2a64`(`arch/arm64/net/bpf_jit_comp.c:44-66`,v6.6.78):

```c
/* arch/arm64/net/bpf_jit_comp.c:44-66(v6.6.78) */
/* Map BPF registers to A64 registers */
static const int bpf2a64[] = {
	/* return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = A64_R(7),
	/* arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = A64_R(0),
	[BPF_REG_2] = A64_R(1),
	[BPF_REG_3] = A64_R(2),
	[BPF_REG_4] = A64_R(3),
	[BPF_REG_5] = A64_R(4),
	/* callee saved registers that in-kernel function will preserve */
	[BPF_REG_6] = A64_R(19),
	[BPF_REG_7] = A64_R(20),
	[BPF_REG_8] = A64_R(21),
	[BPF_REG_9] = A64_R(22),
	/* read-only frame pointer to access stack */
	[BPF_REG_FP] = A64_R(25),
	/* temporary registers for BPF JIT */
	[TMP_REG_1] = A64_R(10),
	[TMP_REG_2] = A64_R(11),
	[TMP_REG_3] = A64_R(12),
	/* tail_call_cnt */
	[TCALL_CNT] = A64_R(26),
	/* temporary register for blinding constants */
	[BPF_REG_AX] = A64_R(9),
	[FP_BOTTOM] = A64_R(27),
};
```

与 x86 站三的 reg2hex 并排读,「思路同构、映射各异」落到实处:

| BPF 寄存器 | x86_64(reg2hex) | arm64(bpf2a64) | 角色 |
|---|---|---|---|
| r0 | RAX | x7(尾声才搬回 x0) | 返回值 |
| r1–r5 | RDI/RSI/RDX/RCX/R8 | x0–x4 | 函数参数 |
| r6–r9 | RBX/R13–R15 | x19–x22 | 被调用者保存 |
| FP | RBP(只读) | x25 | 帧指针 |
| 尾调用计数 | 栈上 `[rbp-tcc_off]` | x26(TCALL_CNT) | 33 层上限计数 |
| AX(致盲) | R10 | x9 | 致盲临时寄存器 |

两套 ABI 的影子都在:arm64 侧 r1–r5 直接对齐 AAPCS 的 x0–x4;但 **r0 映射到 x7 而不是 x0**——x0 已经被 r1 占了,arm64 的返回值寄存器只能「借用」x7,尾声 `emit(A64_MOV(1, A64_R(0), r0), ctx)`(`:682`,v6.6.78)再把 x7 搬回 x0 交差;反过来,helper 调用返回后 `:1123` 把 x0 搬回 x7。这是 arm64 十五个通用寄存器不够「参数、返回值、被保存者」各就各位的妥协,x86 侧不存在(寄存器够多)。尾调用计数两套后端实现也不同:x86 放栈上(站五的 `[rbp-tcc_off]`),arm64 专门占用 x26——同一语义,资源约束不同,落地不同。

### 3.3 差异三点:icache、立即数、原子指令

**差异一:icache 必须显式刷新。**arm64 的指令缓存与数据缓存不保证一致(CPU 改写的数据要指令缓存自己看到才算数),JIT 写完机器码必须执行一次 cache 维护:`bpf_flush_icache` 封装(`:1507-1510`)在编译收尾统一调用(`:1639`)。x86 是一致的缓存体系,写完即可执行,没有这一步——这是两套后端「最后一公里」的架构差异。

**差异二:64 位立即数要按 16 位分块。**x86 指令变长,一条 `movabs` 能装下 64 位立即数;arm64 指令**定长 32 位**,一条装不下 64 位常量,必须 MOVZ/MOVK 序列按 16 位一档拼装。`emit_a64_mov_i64`(`:129-152`,v6.6.78)就是这个拼装器:

```c
/* arch/arm64/net/bpf_jit_comp.c:129-152(v6.6.78) */
static inline void emit_a64_mov_i64(const int reg, const u64 val,
				    struct jit_ctx *ctx)
{
	u64 nrm_tmp = val, rev_tmp = ~val;
	bool inverse;
	int shift;

	if (!(nrm_tmp >> 32))
		return emit_a64_mov_i(0, reg, (u32)val, ctx);

	inverse = i64_i16_blocks(nrm_tmp, true) < i64_i16_blocks(nrm_tmp, false);
	shift = max(round_down((inverse ? (fls64(rev_tmp) - 1) :
					  (fls64(nrm_tmp) - 1)), 16), 0);
	if (inverse)
		emit(A64_MOVN(1, reg, (rev_tmp >> shift) & 0xffff, shift), ctx);
	else
		emit(A64_MOVZ(1, reg, (nrm_tmp >> shift) & 0xffff, shift), ctx);
	shift -= 16;
	while (shift >= 0) {
		if (((nrm_tmp >> shift) & 0xffff) != (inverse ? 0xffff : 0x0000))
			emit(A64_MOVK(1, reg, (nrm_tmp >> shift) & 0xffff, shift), ctx);
		shift -= 16;
	}
}
```

四层优化读法:高 32 位为零的常量直接走 32 位装载(第一条 return);否则比较「原值分块」与「按位取反分块」哪边的 16 位块更少——取反后 MOVN 打头可能更省;MOVZ/MOVN 打头一次,余下每 16 位一条 MOVK,而且**全零/全全一的块直接跳过**(循环里的条件)——x86 侧二节例①的「0 值 xor 优化」在这里有了 arm64 版对应物。锚点程序的 `r0 = 0x0` 在 arm64 上同样命中「零值短路」;而一个 64 位大常量最多四条、常常两三条。

**差异三:原子指令的可选路径(LSE)。**x86 的原子操作一律 `lock` 前缀,没有分叉;arm64 有两代原子机制——传统 LL/SC(excl 前缀的「加载-修改-条件存储」循环)与 ARMv8.1 的 LSE 原生原子指令。JIT 在 `#ifdef CONFIG_ARM64_LSE_ATOMICS`(`:445`)编译期分支内,再按 CPU 能力运行时检测 `cpus_have_cap(ARM64_HAS_LSE_ATOMICS)`(`:1353`)选路:支持 LSE 走 CASAL 一类单条原子指令(`:500` 一带),否则回退 LL/SC 序列。同一份 BPF 原子操作,在两台 arm64 机器上可能 lower 出不同形态——「架构相关」至此又多一层「微架构相关」。

### 3.4 收束:第三套后端怎么读

方法迁移:读任何一套 `arch/*/net/bpf_jit_comp.c`,套路固定四步——①找 `bpf_int_jit_compile` 入口,确认 jit_requested 门与致盲预处理在不在;②找 `build_body`/`do_jit` 等价物,看逐条 lower 的分派结构;③找寄存器映射表,对照本节两张表数「参数、返回值、被保存者」各归谁;④找尾声,看返回值怎么搬、映像怎么刷。第 6 章开头说「框架同构、后端各异」,读第三套时你用的就是这三张对照表加这四步。

**教师视角。**同构性不是 x86 与 arm64 「长得像」,而是**分层**的产物:通用层把「什么时候编、编之前做什么、失败怎么办」全部收走,架构层只剩「怎么翻译一条指令」——所以任何后端一进来就长一个样。学生最常见的误区是拿映射表逐行背诵:不必,表是查的,规律是记的——「参数对齐调用约定、被保存者让位、临时寄存器谁也不用」三条规律在手,x86、arm64、以及未来你遇到的任何后端都能自己推出个八九不离十。

**学生动手**:三条命令把四站切出来——

```bash
# ①四站全量
bash book/examples/chapter-06/03-jit-arm64/trace-jit-arm64.sh
# ②只看差异站(站 4:立即数装载与 icache)
bash book/examples/chapter-06/03-jit-arm64/trace-jit-arm64.sh | sed -n '/^## 站 4/,$p'
# ③负路径自检(应报错 exit 1)
WALK_TREE=/nonexistent bash book/examples/chapter-06/03-jit-arm64/trace-jit-arm64.sh
```

预期输出:①四站结构同切片器 README 摘录,汇总 `TREE_VERSION=v6.6.78 STATIONS=4`,exit 0;②站 4 三段(MOVZ/MOVK 拼装器、icache 封装、编译收尾);③`走读树目录不存在:/nonexistent`,exit 1(均本机实测)。

**检查点**(自测后看提示):

1. BPF r0 在 arm64 上映射到 x7,那 helper 返回后程序从哪里读返回值?——提示:`:1123` 与 `:682` 两次搬运方向相反——进 helper 前后各搬一次,搞清哪次是「取」哪次是「还」。
2. 同一个 `r0 = 0x0`,在 x86 与 arm64 上各 lower 成什么?——提示:二节例①的零值优化与 3.3 的零值短路;x86 是 `xor`,arm64 是单条 MOVZ/等价——两套后端都对零值做了省编码处理。
3. 为什么尾调用计数 x86 放栈上、arm64 占寄存器?——提示:数一数 arm64 表里还有几个空闲通用寄存器;x86 的 R12–R15 已各有所属。

**独立示例**:本节四站的代码段与行号工具化在 [`03-jit-arm64/`](../examples/chapter-06/03-jit-arm64/);与二节六站并排跑,两张输出对着读,「同构与差异」一目了然。


<!-- ============================================================
     Task 9 占位:四、实战:亲手摸到字节码与机器码(五层)
     ①依赖 ②「编译」环境=机检+反汇编 ③部署=定位 ④运行=两切片器
     ⑤效果=bpftool prog dump xlated/jited(预期)+性能对比方法论
============================================================ -->

<!-- ============================================================
     Task 10 占位:可视化(解释器 vs JIT 执行路径对比 + x86 六站流水图)、
     常见问题与排错、课后扩展任务、总结、本章参考来源
============================================================ -->

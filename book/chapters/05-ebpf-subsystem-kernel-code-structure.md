# 第 5 章 eBPF 子系统内核代码结构

> 本章独立示例已全部落地到 [`book/examples/chapter-05/`](../examples/chapter-05/)：[`00-env/`](../examples/chapter-05/00-env/) 走读环境自检、[`01-source-map/`](../examples/chapter-05/01-source-map/) 源码树地图（六分区表与引用锚点清单）、[`02-load-path/`](../examples/chapter-05/02-load-path/) 与 [`03-map-ops/`](../examples/chapter-05/03-map-ops/) 两条走读链切片器、`scripts/` 机检脚本。本章是源码走读教学章：全程只读内核源码树，不构建、不加载；行号引用一律以 v6.6.78 走读树实测为准，机检脚本的输出是本机可复现的事实。

## 学习目标

读完本章后，你应当能够：

- 对着 `kernel/bpf/` 目录说出六分区的名字与分工——入口分发、验证、执行与 JIT（just-in-time，即时编译）、映射实现、BTF（BPF Type Format，BPF 类型格式）与本地存储、其他——并给任意一个 .c 文件找到分区归属（一节 1.1）。
- 讲透「一行宏展开成源码地图」：`include/linux/bpf_types.h` 这份清单在 `kernel/bpf/syscall.c` 里三次定义宏、三次 include，展开成映射类型注册表、程序类型注册表与 link 类型名字符串表；并辨认三层 include 的所有权边界，把本机 `unprivileged_bpf_disabled=2` 对回源码拦截点（一节 1.2 与 1.3）。
- 沿 `BPF_PROG_LOAD` 从 `bpf(2)` 入口到程序就绪走通加载链，说出每一站落在哪个文件（二节）。
- 沿映射（map）从 `BPF_MAP_CREATE` 到 fd、从 fd 到一块内核内存走通生命周期，把第 2 章的八类映射对回实现文件（三节）。
- 用 `scripts/verify-citations.sh` 自检正文里的每一条「文件：行号」引用，并在自己换内核版本时定位行号漂移（四节与排错节）。

## 本章导读

前四章你一直在「用门」：第 1 章把程序从门里递进去，第 3 章看着验证器（verifier）在门口安检，第 4 章弄清了门牌决定工位。但门框长什么样、门是谁造的、为什么这套设施能一边被全系统依赖一边持续生长，还没有拆开看过。本章拆门框——读的对象从「程序怎么写」换成「子系统自己长什么样」。量级先摆出来：`kernel/bpf/` 下 45 个 .c 文件共 65955 行（v6.6.78 实测），单是网络钩子的家 `net/core/filter.c` 就另有 12026 行，加上三层 `include/` 头文件与各架构 JIT 后端，构成 eBPF 的内核侧全貌。本章不写 BPF 程序、不编译、不加载，全部动作是源码走读：贴源码、数行号、对符号。与前四章的分工：前四章讲行为与语义，本章讲结构与边界——此后各章引用内核源码，一律以本章的地图为坐标。

走读需要一个树。全书以 v6.6.78 为走读树（路径与约定见 [`01-source-map/`](../examples/chapter-05/01-source-map/)），正文行号一律标注「(v6.6.78)」。走读树只承担两类操作——`wc -l` 数行数、`grep` 对符号——构建、加载、运行都不发生在它身上；本章所有行号都应能用一条 grep 重现，这就是「静态事实」的含义。你的树若不是这个版本，行号会漂移；这不是麻烦，是本章要教的技能：先按符号检索定位，再读上下文，四节把它变成可复现的动作。

学习路线按「先地图、再两条链、最后上机」推进：一节给源码地图与所有权边界（六分区、双展开、三层 include 与权限开关）；二节追踪加载路径，`BPF_PROG_LOAD` 一站一停；三节追踪映射生命周期；四节把走读变成可复现的动作；可视化、常见问题与排错、课后扩展任务随后收尾。二节与三节各配一个切片器（`02-load-path/`、`03-map-ops/`），四节教你实跑。

两个阅读约定沿用全书。其一，双视角行文——加粗的「**教师视角**」段给类比与误区，「**学生动手**」段给可操作命令，一节各小节的检查点以编号自测问题收拢。其二，验证状态约定——本章是源码走读章，验证口径换档：源码摘录与行号是**静态事实**（贴码与走读树逐字一致，行号全部实测）；机检脚本输出是**本机实测**；凡涉及加载与运行的描述一律「预期输出示例 / 运行级待验证」。若某条行号对不上，先怀疑版本漂移，排错路径见排错节。

最后把前四章的预告逐条回收，共四点，各自标注兑现位置：

- 预告①（第 2 章 1.8）：「JIT 编译为本机指令，架构相关（第 6 章，源码地图见第 5 章）」→ 二节沿 `BPF_PROG_LOAD` 一站一停，走到 JIT 那一站时给出源码位置；各架构后端内部留给第 6 章。
- 预告②（第 2 章三节引言）：「映射在内核里的实现结构是第 5 章的内容」→ 三节《映射生命周期：从一个 fd 到一块内存》兑现：从 `BPF_MAP_CREATE` 到 fd、从 fd 到内核内存，把八类映射对回实现文件。
- 预告③（第 2 章 4.1）：「子系统源码地图（`syscall.c`/`verifier.c`/各映射实现文件的关系）是第 5 章的正题」→ 一节正面兑现：1.1 的分区表、1.3 的边界。
- 预告④（第 4 章总结的桥）：「第 5 章把 2.1 的一行宏展开成源码地图」→ 一节 1.2 的双展开现场兑现：同一个头文件、两处不同宏定义、两张注册表。

## 一、源码地图：布局与所有权边界

### 1.1 六分区：一条 bpf 命令路径上的六个区

背景问题：`kernel/bpf/` 下 45 个 .c 文件一字排开（v6.6.78 实测共 65955 行），凭什么说它「读得动」？答案是分区：全部文件按「一条 bpf 命令从进门到出门」的路径划成六区。完整分区表（每文件一行、行数与职责）在 [`01-source-map/`](../examples/chapter-05/01-source-map/) README 第一节，这里摘 3 行——`syscall.c`（5823 行，入口分发）：bpf(2) 唯一入口，管命令分发、fd 生命周期与权限检查；`verifier.c`（20459 行，验证区）：验证器主体，静态模拟每条指令后放行加载；`hashtab.c`（2630 行，映射实现区）：最常用的映射实现。

六区记忆法沿用该 README：入口分发是「门」，验证是「安检」，执行与 JIT 是「发动机」，映射实现是「集装箱」，BTF 与本地存储是「说明书与贴纸」，其余是「各个码头」。下面逐区走，每区四步：背景问题、文件职责、关键行、失败点与边界。

**区 1：入口分发——`syscall.c`。**背景问题：一个 `bpf(2)` 服务几十个命令字，对号入座在哪？`__sys_bpf`（`kernel/bpf/syscall.c:5426`，v6.6.78）三道前置工序后交 `switch (cmd)`（:5445 起）分发。摘录：

```c
/* kernel/bpf/syscall.c:5445-5448（v6.6.78） */
	switch (cmd) {
	case BPF_MAP_CREATE:
		err = map_create(&attr);
		break;
```

三道前置：`bpf_check_uarg_tail_zero`（:5431）把用户态多传的属性位清零；`copy_from_bpfptr`（:5438）拷贝属性；`security_bpf`（:5441）给 LSM 留闸。失败点：未知命令落 `default: err = -EINVAL`。边界：命令实体在 `map_create`（:1127）、`bpf_prog_load`（:2586）等函数，往下调用其余五区。

**区 2：验证——`verifier.c`、`tnum.c`、`log.c`、`disasm.c`。**背景问题：凭什么放进来的代码不把内核搞崩？`verifier.c`（20459 行，全子系统最大单文件）是第 3 章的家；`tnum.c`（214 行）做三值数字算术，`log.c`（327 行）是日志出口，`disasm.c`（350 行）把指令反汇编进日志。摘录：

```c
/* kernel/bpf/verifier.c:20222-20224（v6.6.78） */
int bpf_check(struct bpf_prog **prog, union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)
{
	u64 start_time = ktime_get_ns();
```

`bpf_check` 是验证器大门，第 3 章的全部规则从这里向下展开。失败点：拒绝日志都出自 `log.c`。边界：验证只在加载期跑一次，运行期不重来。

**区 3：执行与 JIT——`core.c`、`dispatcher.c`、`trampoline.c`、`mprog.c`、`offload.c`。**背景问题：验证通过的指令以什么形态执行？`core.c`（2975 行）同时装着解释器与通用 JIT 骨架，关键在 `__weak`。摘录：

```c
/* kernel/bpf/core.c:2884-2887（v6.6.78） */
struct bpf_prog * __weak bpf_int_jit_compile(struct bpf_prog *prog)
{
	return prog;
}
```

`__weak` 是「架构可覆盖」的链接语义：架构提供同名强符号时，链接期换掉兜底；兜底是原样返回——程序保持指令形态走解释器。`trampoline.c` 为 fentry/fexit 生成蹦床（第 4 章 2.4），`offload.c` 把程序交给网卡，`dispatcher.c`/`mprog.c` 做调用聚合与多程序挂载。边界：JIT 内部是第 6 章正题。

**区 4：映射实现——`arraymap.c`、`hashtab.c` 等 14 个文件。**背景问题：映射类型是个族，靠什么统一？靠一张 ops 回调表。摘录：

```c
/* kernel/bpf/arraymap.c:750-753（v6.6.78） */
const struct bpf_map_ops array_map_ops = {
	.map_meta_equal = array_map_meta_equal,
	.map_alloc_check = array_map_alloc_check,
	.map_alloc = array_map_alloc,
```

每个实现文件都产出这样一个 ops 对象：`hashtab.c` 产 `htab_map_ops`（`kernel/bpf/hashtab.c:2280`，v6.6.78）等变体，`ringbuf.c` 产 `ringbuf_map_ops`（:355）——正是 1.2 注册表里存的地址。失败点：ops 没挂的回调由内核判空。边界：fd 与引用计数归 `syscall.c`，实现文件只管「这类映射怎么存取」（三节展开）。

**区 5：BTF 与本地存储——`btf.c` 等 7 个文件。**背景问题：CO-RE（Compile Once - Run Everywhere，一次编译，到处运行）与 fentry/fexit 依赖的类型信息从哪来？`btf.c`（8601 行）做 BTF 的解析、校验与查找；`sysfs_btf.c` 全文 45 行，把内核 BTF 暴露成第 4 章用过的 `/sys/kernel/btf/vmlinux`。摘录：

```c
/* kernel/bpf/sysfs_btf.c:31-38（v6.6.78） */
static int __init btf_vmlinux_init(void)
{
	bin_attr_btf_vmlinux.size = __stop_BTF - __start_BTF;

	if (!__start_BTF || bin_attr_btf_vmlinux.size == 0)
		return 0;

	btf_kobj = kobject_create_and_add("btf", kernel_kobj);
```

链接器把 vmlinux BTF 放进 `__start_BTF`/`__stop_BTF` 区间，:42 的 `sysfs_create_bin_file` 把它挂成 sysfs 文件（`subsys_initcall` 在 :45）。失败点：内核没编 BTF 时文件不存在，fentry/fexit 与 CO-RE 全线不可用——第 4 章环境前提的源码出处。本地存储侧：task/inode/cgroup 三族映射共享 `bpf_local_storage.c`。

**区 6：其他——`cgroup.c`、`bpf_iter.c` 族、`bpf_lsm.c`、`helpers.c`、`inode.c` 等 14 个文件。**背景问题：挂载点与辅助函数（helper）为什么也住在 `kernel/bpf/`？它们是所有程序类型共享的底座。摘录：

```c
/* kernel/bpf/helpers.c:1756-1759（v6.6.78） */
const struct bpf_func_proto *
bpf_base_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
```

这是第 3 章「辅助函数白名单」的底座：全类型共享的映射操作与基础函数从这里发牌（:1760 起 `bpf_map_lookup_elem_proto` 等），专属面由各自 `func_proto` 叠加。`cgroup.c` 是 cgroup 挂载点本体，`bpf_iter.c` 族是迭代器框架与目标，`inode.c` 是 bpffs——pin（钉住）背后的最小文件系统。失败点：pin 失败先查 bpffs 挂载与对象类型支持。

**学生动手**：三条命令，在内核源码树根目录生成同款清单（本机对 v6.6.78 实测）：

```bash
wc -l kernel/bpf/*.c | sort -n | tail -6   # 行数榜
ls kernel/bpf/*.c | wc -l                  # 文件数
grep -n "BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER\|BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY," include/linux/bpf_types.h
```

预期输出示例（v6.6.78 实测）：

```text
   2630 kernel/bpf/hashtab.c
   2975 kernel/bpf/core.c
   5823 kernel/bpf/syscall.c
   8601 kernel/bpf/btf.c
  20459 kernel/bpf/verifier.c
  65955 总计
45
5:BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER, sk_filter,
87:BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops)
```

第三条命中的两行正是 1.2 的实例——`SOCKET_FILTER` 宏调用占两行，数组映射一行。

### 1.2 双展开现场：同一份清单，两张注册表

背景问题：第 4 章总结留的债「第 5 章把 2.1 的一行宏展开成源码地图」现在兑现。主角是 `include/linux/bpf_types.h`：全文 153 行，文件头 :2 自注 `/* internal file - do not include directly */`；全文件只有一列宏调用——`BPF_PROG_TYPE` 32 行、`BPF_MAP_TYPE` 32 行、`BPF_LINK_TYPE` 12 行（v6.6.78 实测）。这就是 X-macro（同一份清单多次展开的宏惯用法）：清单只写一遍，含义由包含者当场定义。

第一次展开在 `kernel/bpf/syscall.c:63-72`（v6.6.78）：

```c
static const struct bpf_map_ops * const bpf_map_types[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type)
#define BPF_MAP_TYPE(_id, _ops) \
	[_id] = &_ops,
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE
};
```

读法：清单 include 之前，当次把 `BPF_MAP_TYPE(_id, _ops)` 定义成「下标 `_id` 处放 `&_ops`」，另两个宏定义为空——预处理器按当次定义展开，得到「映射类型 → ops 表」。第二次展开在 `syscall.c:2039-2048`（v6.6.78）：

```c
static const struct bpf_prog_ops * const bpf_prog_types[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	[_id] = & _name ## _prog_ops,
#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE
};
```

同一个头文件、两处不同宏定义、两张注册表：这次 `BPF_PROG_TYPE` 展开成「`_id` 处放 `&_name##_prog_ops`」，另两个宏为空。清单里的实例行（`include/linux/bpf_types.h:5-6`，v6.6.78）：

```c
BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER, sk_filter,
	      struct __sk_buff, struct sk_buff)
```

它在第一张表里展开成**空**，在第二张表里展开成 `[BPF_PROG_TYPE_SOCKET_FILTER] = &sk_filter_prog_ops`；:87 的 `BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops)` 正相反——进第一张表成 `[BPF_MAP_TYPE_ARRAY] = &array_map_ops`，进第二张表成空。而 `sk_filter_prog_ops` 定义在 `net/core/filter.c:10953`（v6.6.78），`array_map_ops` 在 `kernel/bpf/arraymap.c:750`——注册表与实现对象分居，链接期对上两半。第三处 include 在 `syscall.c:2962-2969`（v6.6.78，`CONFIG_PROC_FS` 之下）：第三次定义宏并 include，把 `BPF_LINK_TYPE` 展开成名字字符串：

```c
#ifdef CONFIG_PROC_FS
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type)
#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name) [_id] = #_name,
static const char *bpf_link_type_strs[] = {
	[BPF_LINK_TYPE_UNSPEC] = "<invalid>",
#include <linux/bpf_types.h>
};
```

`bpf_link_type_strs[]` 供 /proc 的 fdinfo 打印 link 类型名——`#_name` 把记号变成字符串。三处展开、三种结果：ops 表、prog_ops 表、字符串表。效果：新增一种程序类型，只需清单一行宏调用、实现文件一个 `*_prog_ops` 对象、UAPI 枚举一个成员——该长的那张注册表自动长出一格，`__sys_bpf` 与 `find_prog_type`（`syscall.c:2050`，:2056 有 `array_index_nospec` 挡越界猜测）一行都不用改。「一行宏展开成源码地图」，说的就是这个。

**教师视角**：这个子系统以十万行计、只增不减，凭什么不散架？扩展点被压到极致：**一个分发器**（`__sys_bpf` 的 `switch`）加**一张清单**（`bpf_types.h`）。新能力进来只有三个动作——UAPI 加枚举、清单加一行、实现文件放一个 ops 对象——其余几万行各回各的文件，只靠注册表与 ops 回调相连。正确姿势不是从 main 顺着读，而是从注册表入手——`bpf_map_types[]` 每个非空下标都指向一个实现文件，表即目录。

### 1.3 所有权边界：三层 include 与一枚权限开关

背景问题之一：谁能包含谁？用户态工具为什么敢直接用 `<linux/bpf.h>`？边界有三层。第一层 `include/uapi/linux/bpf.h`（7350 行，v6.6.78）是 UAPI 契约：命令字枚举、`union bpf_attr`、类型枚举与辅助函数编号——内核对用户态唯一承诺稳定的部分，两边都可包含。第二层 `include/linux/bpf.h`（3232 行）是内核对象层：`struct bpf_map`、`struct bpf_map_ops`、`struct bpf_prog` 与验证器导出原型，仅内核态可包含；它自己在 :7 `#include <uapi/linux/bpf.h>`（v6.6.78）——「契约在内层、实现在外层」的单向依赖就此可见；`syscall.c` 的 :4 包含的正是第二层。第三层 `bpf_types.h` 游离在依赖链之外——不是「被包含的头」，是「被多次展开的清单」，1.2 的三处现场即全部用法。失败点：内核私有头在用户态编不过——工具链用自带 uapi 副本（第 2 章三节引言数过它的枚举成员）。

背景问题之二：前四章标注的「本机非 root、`unprivileged_bpf_disabled=2`」在源码哪里？声明在 `syscall.c:60-61`（v6.6.78）：`int sysctl_unprivileged_bpf_disabled __read_mostly = IS_BUILTIN(CONFIG_BPF_UNPRIV_DEFAULT_OFF) ? 2 : 0;`——发行版打开该配置即开机为 2，本机值正是 2（`/proc/sys/kernel/unprivileged_bpf_disabled`，本机实测）。拦截点两处、同一句式：`map_create` 的 :1186 与 `bpf_prog_load` 的 :2618，都是 `if (sysctl_unprivileged_bpf_disabled && !bpf_capable()) return -EPERM;`——非 root 加载拿到 `Operation not permitted` 的源码出处，前四章「加载级待验证」的答案落在这里。开关自身：`bpf_unpriv_handler`（:5774）要求写入者持 `CAP_SYS_ADMIN`（:5781），值为 1 时锁死（:5787）；表项（:5800 起）以 `SYSCTL_TWO`（:5806）钳住值域 0–2。边界：权限不止一层——值哪怕归 0，非特权也只拿到受限面：映射侧 :1190 起 switch 只放行 ARRAY、PERCPU_ARRAY 等，程序侧 :2624-2627 只放行 SOCKET_FILTER 与 CGROUP_SKB。

### 1.4 检查点

先自答，再看提示：

1. 内核要新增一种映射类型，按本章地图至少要动哪几处？哪些地方**不用**动？（提示：UAPI 枚举加成员、`bpf_types.h` 加一行、实现文件给 ops 对象，三处；注册表与分发不用动——双展开替你长表，见 1.2。）
2. `bpf_prog_types[]` 里 `BPF_PROG_TYPE_SOCKET_FILTER` 一格指向 `&sk_filter_prog_ops`，这个对象定义在哪个文件？为什么不在 `syscall.c`？（提示：`net/core/filter.c:10953`（v6.6.78）；ops 跟着钩子实现走，注册表只管入口与生命周期（1.2）。）
3. 非 root 终端执行 `bpftool prog load` 报 `Operation not permitted`，本章能把报错对到源码哪一行？再想一层：若 sysctl 归 0，同一个程序就能加载吗？（提示：`syscall.c:2618`（PROG_LOAD）与 :1186（MAP_CREATE）的 `sysctl_unprivileged_bpf_disabled && !bpf_capable()`；归 0 后还要过 :2624 的类型白名单——非特权仅 SOCKET_FILTER 与 CGROUP_SKB（1.3）。）

**独立示例**：[`01-source-map/`](../examples/chapter-05/01-source-map/)——45 个 .c 文件六分区全量表、三层 include 与双展开的文字版、`citations.txt` 引用锚点清单（本章每条「文件：行号」都能对上号），机检脚本逐条核对（用法见四节）。

## 二、加载路径追踪：BPF_PROG_LOAD 的一站一停

背景问题：第 1 章你按下回车，`bpftool prog load` 把一段 ELF（Executable and Linkable Format，可执行与链接格式）递给内核，几毫秒后 fd 回到你手里——这中间走了哪几站、失败落在哪一行？本节沿 `BPF_PROG_LOAD` 设六个站，一站一停，每站四要素：函数签名（标注 v6.6.78 行号）→ 职责 → 关键代码段（≤25 行，与走读树逐字一致）→ 失败出口；六站与 [`02-load-path/`](../examples/chapter-05/02-load-path/) 切片器的输出一一对应，行号即切片器实测行号，新引入的引用同样全部在树上 grep 实测。六站一行链：入口分发（`syscall.c:5464`）→ 业务入口 `bpf_prog_load`（:2586）→ 验证 `bpf_check`（:2743 → `verifier.c:20222`）→ 选执行引擎 `bpf_prog_select_runtime`（:2747 → `core.c:2357`）→ 编号与发 fd（:2751/:2773）→ 「加载 ≠ 挂载」与释放链（:2181 起）。贴码头注的「文件：起-止行（v6.6.78）」都能用一条 `sed -n 'A,Bp'` 重现；失败出口落到具体 errno（错误码）与行号，2.6 末尾收成总表。

### 2.1 站 1：入口分发——`bpf(2)` 如何选中 `BPF_PROG_LOAD`

背景问题：一个 `bpf(2)` 服务几十个命令字，加载命令凭什么被挑中？真入口在 `syscall.c:5567`（v6.6.78）：

```c
/* kernel/bpf/syscall.c:5567-5570（v6.6.78） */
SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
{
	return __sys_bpf(cmd, USER_BPFPTR(uattr), size);
}
```

`SYSCALL_DEFINE3` 展开成名为 `bpf` 的系统调用，只做一件事：把用户态指针包成 `USER_BPFPTR` 转交 `__sys_bpf`（:5426）。后者过三道前置工序（一节区 1 已点名：:5431/:5438/:5441），随后 `switch (cmd)`（:5445）分发，加载分支在 :5464：

```c
/* kernel/bpf/syscall.c:5464-5466（v6.6.78） */
	case BPF_PROG_LOAD:
		err = bpf_prog_load(&attr, uattr, size);
		break;
```

失败出口：属性拷贝失败 `-EFAULT`（:5438-5439）；未知命令落 `default: err = -EINVAL`（:5559-5560）。容易误读的细节：`case BPF_PROG_LOAD:` 全文件出现两次（:5464 与 :5592）——第二处在 `bpf_sys_bpf`（:5584，BPF 程序运行期再调 bpf(2) 的命令白名单）的 switch 里，不是加载路径本体；切片器把两处都列进定位行，读时认第一处。

### 2.2 站 2：`bpf_prog_load`——业务入口与属性处理

背景问题：分发只是传球，业务函数接球第一件事不是验证，是逐项审参数。`bpf_prog_load` 定义在 `syscall.c:2586`（v6.6.78），切片器站 2 切头 24 行（:2586-2609），正文摘签名段：

```c
/* kernel/bpf/syscall.c:2586-2592（v6.6.78） */
static int bpf_prog_load(union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)
{
	enum bpf_prog_type type = attr->prog_type;
	struct bpf_prog *prog, *dst_prog = NULL;
	struct btf *attach_btf = NULL;
	int err;
	char license[128];
```

逐段读：签名后的第一道闸是 `CHECK_ATTR(BPF_PROG_LOAD)`（:2594-2595），要求属性块中本命令之后的所有填充位为零——UAPI 向后兼容的地基：未来内核新增字段，老 loader 置零即可不报错；`prog_flags` 掩码（:2597-2604）与对齐类检查（:2606-2609）把标志位审死，越位同样 `-EINVAL`/`-EPERM`。紧接三道权限闸，全在 :2743 的验证调用**之前**：:2618-2619 非特权开关（一节 1.3 已拆）；:2621-2623 指令条数为 0 或超限即 `-E2BIG`，上限分档——特权 `BPF_COMPLEXITY_LIMIT_INSNS`（100 万条，`include/linux/bpf.h:1769`，v6.6.78）、非特权 `BPF_MAXINSNS`（4096 条，`include/uapi/linux/bpf_common.h:54`）；:2624-2627 类型白名单一句回指——非特权仅 SOCKET_FILTER 与 CGROUP_SKB（一节 1.3 贴过码）；:2629-2632 分档查 `CAP_NET_ADMIN`/`CAP_PERFMON`。位置关系要记牢：权限闸在验证之前——第 3 章排错节第三组「先答一个问题：进没进验证」的源码出处就是 :2618-2632 与 :2743 的先后。

### 2.3 站 3：验证调用点——`bpf_check` 的唯一入口

背景问题：第 3 章整章的验证器，在加载路径里占几行？切片器站 3 切出 `syscall.c:2736-2749`——验证只占两行：

```c
/* kernel/bpf/syscall.c:2736-2749（v6.6.78） */
	prog->aux->load_time = ktime_get_boottime_ns();
	err = bpf_obj_name_cpy(prog->aux->name, attr->prog_name,
			       sizeof(attr->prog_name));
	if (err < 0)
		goto free_prog_sec;

	/* run eBPF verifier */
	err = bpf_check(&prog, attr, uattr, uattr_size);
	if (err < 0)
		goto free_used_maps;

	prog = bpf_prog_select_runtime(prog, &err);
	if (err < 0)
		goto free_used_maps;
```

这 14 行是后三拍的总谱：:2743 验证、:2747 选引擎、下一拍 :2751 领编号，首尾相接。`bpf_check` 定义在 `verifier.c:20222`（v6.6.78，一节区 2 贴过签名）。本站只讲边界——输入：已填好指令与类型的 `struct bpf_prog *` 加用户属性；返回：0 放行，负值拒绝；内部机制是第 3 章整章的内容，不重复。失败出口：拒绝以 `return -EACCES` 落地（`verifier.c` 全文件 126 处，v6.6.78 实测），另有白名单落空（`unknown func`）、回边一类 `-EINVAL` 形态——两类都带完整验证日志，信号对根因的速查表见第 3 章排错节第一组。失败后 `goto free_used_maps`（:2745），沿站 6 释放链回收——「每拍失败都干净退出」第一拍。

### 2.4 站 4：JIT 选择——`bpf_prog_select_runtime`

背景问题：验证通过的是 eBPF 指令，执行时跑的可能是本机机器码——形态在哪一站定下来？就在 :2747，验证的下一行。定义在 `core.c:2357`（v6.6.78），文档注释（:2347 起）一句定职责：「Try to JIT eBPF program, if JIT is not available, use interpreter」。切片器站 4 从文档注释切起（正文摘签名段）：

```c
/* kernel/bpf/core.c:2357-2369（v6.6.78） */
struct bpf_prog *bpf_prog_select_runtime(struct bpf_prog *fp, int *err)
{
	/* In case of BPF to BPF calls, verifier did all the prep
	 * work with regards to JITing, etc.
	 */
	bool jit_needed = false;

	if (fp->bpf_func)
		goto finalize;

	if (IS_ENABLED(CONFIG_BPF_JIT_ALWAYS_ON) ||
	    bpf_prog_has_kfunc_call(fp))
		jit_needed = true;
```

函数体 :2384 调架构 JIT：

```c
/* kernel/bpf/core.c:2384-2389（v6.6.78） */
		fp = bpf_int_jit_compile(fp);
		bpf_prog_jit_attempt_done(fp);
		if (!fp->jited && jit_needed) {
			*err = -ENOTSUPP;
			return fp;
		}
```

失败出口两种。其一，回退不是失败：`bpf_int_jit_compile` 是 `__weak` 兜底桩（:2884，一节区 3 贴过），架构没提供强符号时原样返回，程序保持指令形态走解释器——设计好的回退，`*err` 为 0。其二，禁止回退时才失败：`jit_needed` 为真（`CONFIG_BPF_JIT_ALWAYS_ON`，或程序含 kfunc 调用——BPF 直接调用内核函数的机制，:2367-2369；头注释 :2359-2361 说明原因：此类程序验证器已按 JIT 前提做完准备）而 `fp->jited` 仍为假，即 `-ENOTSUPP`（:2386-2388）。finalize 段（:2396-2406）两件收尾：`bpf_prog_lock_ro`（:2397）把程序内存锁定只读——验证期可改写、运行期只读的边界画在这一行；`bpf_check_tail_call`（:2404）做尾调用兼容检查。

第 2 章 1.8 的预告「JIT 编译为本机指令，架构相关（第 6 章，源码地图见第 5 章）」至此兑现：通用骨架在本函数，`__weak` 兜底在 :2884，架构强符号在各架构目录（x86-64 为 `arch/x86/net/bpf_jit_comp.c`）；eBPF 指令如何逐条翻成本机码，深挖留第 6 章。

### 2.5 站 5：编号与发 fd——`prog_idr` 与 `bpf_prog_new_fd`

背景问题：程序已验证、已选好引擎，但用户态还看不见它——「看得见」从哪一行开始？两步：先领全局编号，再领 fd。切片器站 5 的定位行给出两个函数与两个调用点：`bpf_prog_alloc_id`（`syscall.c:2099`，调用点 :2751）、`bpf_prog_new_fd`（`syscall.c:2293`，调用点 :2773）。编号的家是文件头部 `static DEFINE_IDR(prog_idr)`（:53，配套自旋锁 :54）——IDR（ID-to-pointer 编号表）。

```c
/* kernel/bpf/syscall.c:2099-2109（v6.6.78） */
static int bpf_prog_alloc_id(struct bpf_prog *prog)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&prog_idr_lock);
	id = idr_alloc_cyclic(&prog_idr, prog, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		prog->aux->id = id;
	spin_unlock_bh(&prog_idr_lock);
	idr_preload_end();
```

职责：`idr_alloc_cyclic` 从 1 起循环发号写入 `prog->aux->id`（:2105-2107），全程持自旋锁（:2104/:2108）——`bpftool prog show` 里那个 ID 的源码出处就在这里；编号耗尽兜底 `WARN_ON_ONCE` 加 `-ENOSPC`（:2112-2113）。编号在场不等于可见：发 fd 才算「对用户态存在」。

```c
/* kernel/bpf/syscall.c:2293-2303（v6.6.78） */
int bpf_prog_new_fd(struct bpf_prog *prog)
{
	int ret;

	ret = security_bpf_prog(prog);
	if (ret < 0)
		return ret;

	return anon_inode_getfd("bpf-prog", &bpf_prog_fops, prog,
				O_RDWR | O_CLOEXEC);
}
```

`security_bpf_prog`（:2297）是 LSM 闸；`anon_inode_getfd`（:2301-2302）发放匿名 inode 的 fd（名字 `bpf-prog`，`O_CLOEXEC` 防跨 exec 泄漏）。站 5 同时是释放链的起点：加载体内 `atomic64_set(&prog->aux->refcnt, 1)`（:2716）把引用计数置 1，fd 是第一位持有者。失败出口：编号路径 `WARN_ON_ONCE(!id)` → `-ENOSPC`（:2112-2113）；fd 分配失败（fd 表满一类标准错误随返回值传出）→ :2774-2775 `bpf_prog_put(prog)`——编号已发、通告已登记（kallsyms/perf/audit 三件套，:2769-2771），善后必须走释放链。

### 2.6 站 6：「加载 ≠ 挂载」与释放链

背景问题：fd 到手，程序开始干活了吗？没有。

「加载 ≠ 挂载」的证据数得出来：`bpf_prog_load` 从 :2586 到函数尾 :2793 没有一次执行挂载——它只处理 attach 元数据（`attach_btf_id` 目标核对 :2637-2659、`expected_attach_type` 补全与校验 :2662-2671），装进 `prog->aux` 就收工。真正的挂载是同一个 switch 里两条平级的独立命令：`case BPF_PROG_ATTACH:`（:5473-5475，实体 `bpf_prog_attach` :3911）与 `case BPF_LINK_CREATE:`（:5534-5536，实体 `link_create` :5010）。第 4 章 2.3 的 SEC 挂载路线表按类型给出各自走哪条命令，第 4 章 2.4 讲 `bpf_link`（BPF 链接对象——挂载关系的 fd 形态）机制；本站的补充：三条命令共享一个入口 switch、互不调用——fd 返回时程序未挂任何钩子。加载时登记的引用总要有出口，切片器站 6 切出释放链骨架：

```c
/* kernel/bpf/syscall.c:2181-2193（v6.6.78） */
static void __bpf_prog_put(struct bpf_prog *prog)
{
	struct bpf_prog_aux *aux = prog->aux;

	if (atomic64_dec_and_test(&aux->refcnt)) {
		if (in_irq() || irqs_disabled()) {
			INIT_WORK(&aux->work, bpf_prog_put_deferred);
			schedule_work(&aux->work);
		} else {
			bpf_prog_put_deferred(&aux->work);
		}
	}
}
```

职责：引用计数归零（:2185）才启动分级释放——入口 `bpf_prog_put`（:2195-2199，导出符号）→ `__bpf_prog_put`（:2181，中断上下文转工作队列 :2186-2188）→ `bpf_prog_put_deferred`（:2168）卸 :2769-2771 登记的三件套、把编号还给 IDR（:2175-2177）→ `__bpf_prog_put_noref`（:2147）注销 BTF/模块引用后按 sleepable 与否走两种 RCU 回收（:2158-2165）。常态入口是 fd 关闭：`bpf_prog_release`（:2201）在 `->release` 回调里调 `bpf_prog_put`（:2205）——第 2 章 1.8 的「Ctrl-C 退出即卸载」源码出处就在这一行。map 侧的完整释放故事在三节；prog 侧到此一段带过。

六站失败出口收成总表（行号 v6.6.78 实测；`-EACCES`/`-EPERM` 日志排错见第 3 章排错节第一、三组）：

| errno | 拦截点 | 触发条件 |
|---|---|---|
| `-EFAULT` | `syscall.c:5438-5439`、:2698-2702 | 属性块或指令流拷贝失败 |
| `-EINVAL` | :5559-5560；:2594-2595；:2597-2604；:2670；验证期 | 未知命令；属性填充位/标志越界；attach 校验失败；验证期 `unknown func`、回边等 |
| `-E2BIG` | :2621-2623 | 指令条数为 0 或超档（特权 100 万/非特权 4096） |
| `-EPERM` | :2618-2619、:2624-2627、:2629-2632、:2606-2609 | 非特权开关、类型白名单、NET_ADMIN/PERFMON 档位等——均未进验证即拦 |
| `-EACCES` | `verifier.c` 全文件 126 处 `return -EACCES` | 验证器拒绝，带完整日志 |
| `-ENOMEM` | :2674-2681 | `bpf_prog_alloc` 失败 |
| `-ENOTSUPP` | `core.c:2386-2388` | 必须 JIT（JIT_ALWAYS_ON 或含 kfunc 调用）而架构未提供 |

判别口径沿用第 3 章排错节第三组：`-EPERM` 没进验证（查权限档位），`-EACCES`/`-EINVAL` 已进（读验证日志）——现在这句话能对回 :2618-2632 与 :2743 的先后顺序。

**教师视角**：为什么拆成「验证 → 选执行 → 编号 → 发 fd」四拍，而不是一口气做完？看四拍的失败代价。验证失败（:2744）与选引擎失败（:2748）：程序还没领编号、没对用户态可见，`goto free_used_maps` 清完即走，不留「半个程序」；领编号失败（:2752）同样不可见；发 fd 失败（:2774-2775）是唯一需要善后的——编号已发、通告已登记，必须 `bpf_prog_put` 走释放链。四拍次序保证每个失败出口干净：不可见的对象用 `goto` 清理，可见的对象用引用计数清理。类比：安检 → 挑发动机 → 发工牌 → 发门禁卡——门禁卡是外人唯一认得的东西，发卡前的失败悄悄退场，发卡后的失败才要刷卡注销。四拍不是人为划分，是 :2743/:2747/:2751/:2773 四个调用点的天然分界。

**学生动手**：用切片器逐站对照。三条命令（走读树路径约定见 [`01-source-map/`](../examples/chapter-05/01-source-map/)）：

```bash
bash book/examples/chapter-05/02-load-path/trace-load-path.sh    # 全量六站
bash book/examples/chapter-05/02-load-path/trace-load-path.sh | grep "^## 站"   # 站目录
WALK_TREE=/path/to/your/tree bash book/examples/chapter-05/02-load-path/trace-load-path.sh
```

预期输出示例（第一、二条为 v6.6.78 树本机实跑，机检级）：第一条共 307 行、六站齐全，末行 `TREE_VERSION=v6.6.78 STATIONS=6`；第二条命中 6 行站标题；第三条换树时 stderr 出版本 WARN（其 README 失败模式 3），行号仅供参考。对照方法：每段贴码的行号区间都取自切片器同站的 `sed -n 'A,Bp'` 命令（如站 3 的 2736-2749）——对不上的那处就是行号漂移，复核见四节的 `verify-citations.sh`。

### 2.7 检查点

先自答，再看提示：

1. `bpftool prog load` 返回 `-E2BIG`，程序进验证了吗？对回源码行号。（提示：没进——:2621-2623 在 :2743 之前；上限分档见 2.2 与总表。）
2. 站 4 说「JIT 失败回退解释器不是错误」，什么条件下回退会变成加载失败？（提示：`jit_needed` 为真——`CONFIG_BPF_JIT_ALWAYS_ON` 或含 kfunc 调用——而 `fp->jited` 仍为假，即 `-ENOTSUPP`（`core.c:2386-2388`）。）
3. fd 拿到后立刻 `close`，内核里发生什么？连带回答「此刻程序挂在钩子上吗」。（提示：`close` 触发 `bpf_prog_release`（:2201）→ `bpf_prog_put`，引用计数归零才分级释放（2.6）；fd 返回时程序未挂任何钩子——挂载是 `BPF_LINK_CREATE`/`BPF_PROG_ATTACH` 的独立命令。）

**独立示例**：[`02-load-path/`](../examples/chapter-05/02-load-path/)——本节六站的切片器（`trace-load-path.sh`），每站「定位 → 代码段 → 职责注」三层输出，正文行号与其一一对应；README 记录调用方法、实跑摘录与失败模式。

<!-- 三节（T7）：## 三、映射生命周期：从一个 fd 到一块内存 -->
<!-- 四节（T8）：## 四、实战：把走读变成可复现的动作 -->
<!-- 可视化（T8）：## 可视化：源码树全景与加载调用流 -->
<!-- 排错（T9）：## 常见问题与排错 -->
<!-- 课后（T9）：## 课后扩展任务 -->
<!-- 总结（T9）：## 总结 -->
<!-- 参考来源（T9）：## 本章参考来源 -->

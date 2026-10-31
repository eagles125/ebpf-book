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

## 三、映射生命周期：从一个 fd 到一块内存

背景问题：第 2 章三节引言预告「映射在内核里的实现结构是第 5 章的内容」，本节正面兑现：沿七个动作走通映射（map）生命周期——create、lookup、update、delete、freeze（冻结）、pin（钉住）、release（释放）。公共头是 `struct bpf_map`（`include/linux/bpf.h:247`，v6.6.78），本节用的字段：`refcnt`（:276）与 `usercnt`（:277）两层引用计数、`writecnt`（:284）写计数、`frozen`（:298）；具体类型把公共头嵌进私有结构（`struct bpf_array` 首字段即它，`include/linux/bpf.h:1757`）。八类映射各对回一份 ops 对象（一节区 4 已点名）；本节以数组为对照例。

七个动作落在同一条三层链上，先立模型再逐站走：

```text
命令层（fd/权限/拷贝）  通用层（按类型分流）  ops 回调（具体算法，arraymap 对照）
BPF_MAP_CREATE      :5446 → map_create      :1127 ────→ map_alloc_check/map_alloc（:52/:83）
BPF_MAP_LOOKUP_ELEM :5449 → map_lookup_elem :1434 → bpf_map_copy_value  :210 → array_map_lookup_elem :166
BPF_MAP_UPDATE_ELEM :5452 → map_update_elem :1509 → bpf_map_update_value :153 → array_map_update_elem :313
BPF_MAP_DELETE_ELEM :5455 → map_delete_elem :1565 → rcu 内直呼 ops ：1605 → array_map_delete_elem :392
BPF_MAP_FREEZE      :5461 → map_freeze      :1998 ──（自包含，无 ops 回调，只置 frozen）──
BPF_OBJ_PIN         :5467 → bpf_obj_pin     :2797 ──（与 ops 无关：绑 bpffs 路径）──
close(fd)（⑦）            → bpf_map_release  :773 → bpf_map_put :750 → RCU 宽限期 → 工作队列 → ops->map_free :416
```

模型图把 1.2 注册表（`bpf_map_types[]`，`syscall.c:63`）落到调用关系上，也是 [`03-map-ops/`](../examples/chapter-05/03-map-ops/) README 第四节三层表的正文版。数组 ops 的存取三格（表头 ：750 见 1.1 区 4）：

```c
/* kernel/bpf/arraymap.c:757-759（v6.6.78） */
	.map_lookup_elem = array_map_lookup_elem,
	.map_update_elem = array_map_update_elem,
	.map_delete_elem = array_map_delete_elem,
```

`map_free`/`map_release_uref`（:754/:756）留给动作⑦（3.5）。各站四要素：入口命令 → 通用层 → ops 回调（arraymap 对照）→ 失败出口。

### 3.1 动作① create：从类型编号到一块内存

入口 ：5446 → `map_create`（`syscall.c:1127`，切片器站 2）。属性审查后是查表（`CHECK_ATTR`，:1136-1138）：`map_type` 越界即 `-EINVAL`（:1163-1164）；`array_index_nospec`（:1165）是防推测围栏，与程序侧 `find_prog_type` 的 :2056 同句式；`ops = bpf_map_types[map_type]`（:1166）落空再 `-EINVAL`（:1167-1168）——「枚举在场、实现缺席」（1.2 的按配置分段）。随后：`ops->map_alloc_check`（:1170-1173，数组 `arraymap.c:52`）审私有参数，非特权闸 ：1186-1187（1.3 已拆），特权类型分档 ：1190-1233（`-EPERM` 三档 ：1221/:1229/:1233）。然后 `ops->map_alloc`（:1236-1238，数组为 `arraymap.c:83`）——**「一块内存」在此诞生**：数组按 `max_entries` 预分配定长一整块。收尾：两层计数置 1（:1247-1248）、`freeze_mutex` 初始化（:1249）、`bpf_map_new_fd` 发 fd（:1297）。失败出口：alloc 失败传 `PTR_ERR`（:1237-1238）；**fd 失败是唯一要善后的出口**——对象已建、计数已置，:1305 `bpf_map_put_with_uref` 走释放链（2.6「发卡后失败」的映射侧倒影）。

### 3.2 动作②③④ lookup/update/delete：三层各司其职

**lookup。**入口 ：5449 → `map_lookup_elem`（:1434，切片器站 4）：`__bpf_map_get`（:1452）把 fd 换回 `struct bpf_map *`（空 `-EBADF` :1325；非映射对象 `-EINVAL` :1328）；`FMODE_CAN_READ` 闸（:1455）；拷 key、分配值缓冲（:1466、:1474-1476）；进通用层 `bpf_map_copy_value`（:1487）。通用层定义在 ：210，数组这类常规类型落最后的 else：

```c
/* kernel/bpf/syscall.c:246-251（v6.6.78） */
		else
			ptr = map->ops->map_lookup_elem(map, key);
		if (IS_ERR(ptr)) {
			err = PTR_ERR(ptr);
		} else if (!ptr) {
			err = -ENOENT;
```

ops 回调返回**指针**而非 errno，三态由通用层翻译——`IS_ERR` 原样传（:248-249）、`NULL` 翻成 `-ENOENT`（:250-251）、成功则 `copy_map_value` 拷值（:254-258），末尾 `copy_to_user`（:1492）；读取全程在 `rcu_read_lock()`（:243）内。数组侧 `array_map_lookup_elem`（`arraymap.c:166`）印证指针返回：越界返 `NULL`（:171-172，经 ：251 变 `-ENOENT`），否则一句指针算术 `array->value + elem_size * index`（:174）。

**update。**入口 ：5452 → `map_update_elem`（:1509）：`__bpf_map_get`（:1524）后先递增写计数（:1527，动作⑤用）再查 `FMODE_CAN_WRITE`（:1528-1530），拷值（:1546），进通用层 `bpf_map_update_value`（:1552），出口递减（:1558）。通用层定义在 ：153，头部分流：

```c
/* kernel/bpf/syscall.c:159-163（v6.6.78） */
	if (bpf_map_is_offloaded(map)) {
		return bpf_map_offload_update_elem(map, key, value, flags);
	} else if (map->map_type == BPF_MAP_TYPE_CPUMAP ||
		   map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		return map->ops->map_update_elem(map, key, value, flags);
```

offload 交驱动、CPUMAP/STRUCT_OPS 直呼 ops；往下 SOCKMAP 走专用通道（:164-166）、prog array 更新 fd（:167-170）、:172 起禁插桩、percpu 族（:173-180）、QUEUE/STACK 翻译成 push（:195-198）；数组落最后的 else：rcu_read_lock 内 `ops->map_update_elem`（:199-202）。数组侧 `array_map_update_elem`（`arraymap.c:313`）失败出口最有教学价值：越界 `-E2BIG`（:324-326，元素预分配、不能追加）；`BPF_NOEXIST` 撞 `-EEXIST`（:328-330）——数组没有「空槽」。「同一命令、不同实现、不同 errno」——回调表的存在意义。

**delete。**入口 ：5455 → `map_delete_elem`（:1565）：权限闸同 update（:1582-1584）；prog array/struct_ops 要求可睡眠上下文，先分支直呼 ops（:1596-1601）；常规类型在 `rcu_read_lock` 内直呼 ops（:1605）——无独立通用层包装，只剩上下文纪律。数组侧对照三行：`array_map_delete_elem`（`arraymap.c:392-394`）直接 `return -EINVAL;`——定长预分配内存没有「删掉一格」的语义，想归还只有整块释放（动作⑦）；哈希表同名回调返 `-ENOENT`——返回什么由实现定义。

### 3.3 动作⑤ freeze：一张「空白行」的对照面

入口 ：5461 → `map_freeze`（:1998，切片器站 5）。函数体里**没有一次 ops 调用**——三层表的空白行。冻结 = 把映射从「可写」改「只读」：通用属性、不触及存储算法，通用层自己收口：

```c
/* kernel/bpf/syscall.c:2022-2032（v6.6.78） */
	mutex_lock(&map->freeze_mutex);
	if (bpf_map_write_active(map)) {
		err = -EBUSY;
		goto err_put;
	}
	if (READ_ONCE(map->frozen)) {
		err = -EBUSY;
		goto err_put;
	}

	WRITE_ONCE(map->frozen, true);
```

生效点不在本函数，而在**之后每一次写命令的权限检查**：`map_get_sys_perms`（:784）发现 `map->frozen` 就剥掉 fd 的 `FMODE_CAN_WRITE`（:791-792）——此后 update/delete/freeze 在 ：1528/:1582/:2017 撞 `-EPERM`，读（:1455 的 `FMODE_CAN_READ`）不受影响。并发两道：`bpf_map_write_active`（:124-127）不撕裂进行中的写——update/delete 入口的递增递减（:1527/:1558、:1581/:1612）是另一半；有写在进行时冻结拿 `-EBUSY`（:2024），已冻结再冻同样 `-EBUSY`（:2028）。

### 3.4 动作⑥ pin：fd 与路径的往返

入口 `BPF_OBJ_PIN` :5467 / `BPF_OBJ_GET` :5470 → `bpf_obj_pin`（:2797）/`bpf_obj_get`（:2813）。fd 进程私有、退出即失；pin 把对象在 bpffs（`kernel/bpf/inode.c`，一节区 6）里记到路径名下：`bpf_obj_pin` 审参（:2801-2806）后交 `bpf_obj_pin_user`（:2808-2810，实体 `kernel/bpf/inode.c:478`）——取对象、认类型（只认映射与程序）、建 inode，`bpf_any_get` 对映射走 `bpf_map_inc_with_uref`（`inode.c:39`）：**bpffs 路径从此持有一份 usercnt**。反向 `bpf_obj_get` → `bpf_obj_get_user`（`kernel/bpf/inode.c:527`）：路径换回新 fd。生命周期含义：pin 后持有者从「进程 fd 表」扩到「文件系统」，close 原 fd 不再让对象消失——第 1 章 `bpftool map pin`/`--pinned` 的出处。失败出口：路径非法、类型不支持、路径已占用 `-EEXIST` 等随 `bpf_obj_pin_user` 传出。

### 3.5 动作⑦ release：两层计数、两级归零、两段延迟

二节 2.6 留的释放线索在此展开，映射侧是程序侧释放链（`__bpf_prog_put`，:2181）的姊妹篇。入口 `close(fd)`：`bpf_map_fops` 的 `.release`（:944）挂 `bpf_map_release`（:773）——给实现一次 `ops->map_release` 机会（:777-778），再 `bpf_map_put_with_uref`（:780）。两层计数分工：`usercnt` 计用户态可见持有者（fd 与 pin），`refcnt` 计全部持有者——程序引用映射走 `bpf_map_inc`（`verifier.c:17748`，登记进 used_maps）**只加 refcnt、不加 usercnt**：关光 fd、删光 pin，只要程序还在用 refcnt 就不为零。归零流程：

```c
/* kernel/bpf/syscall.c:750-764（v6.6.78） */
void bpf_map_put(struct bpf_map *map)
{
	if (atomic64_dec_and_test(&map->refcnt)) {
		/* bpf_map_free_id() must be called first */
		bpf_map_free_id(map);

		WARN_ON_ONCE(atomic64_read(&map->sleepable_refcnt));
		if (READ_ONCE(map->free_after_mult_rcu_gp))
			call_rcu_tasks_trace(&map->rcu, bpf_map_free_mult_rcu_gp);
		else if (READ_ONCE(map->free_after_rcu_gp))
			call_rcu(&map->rcu, bpf_map_free_rcu_gp);
		else
			bpf_map_free_in_work(map);
	}
}
```

头注释（:747-749）先回答「为什么不能在 syscall 上下文直接 free」：`ops->map_free` 可能睡眠；更根本的是**其他 CPU 可能还在读**——lookup 在 `rcu_read_lock` 下裸拿 ops 返回的指针（:243-247），须等所有在读 CPU 离开读侧临界区。所以 `refcnt` 归零（:752）后先还编号给 `map_idr`（:754；对称 2.5 `prog_idr`），再走 RCU 宽限期（:758-761）或直接转工作队列（:762）。usercnt 先行：`bpf_map_put_uref`（:717）在 `usercnt` 归零时回调 `ops->map_release_uref`（:720-721，数组挂 `array_map_free_timers`，`arraymap.c:402`）。宽限期走完，`bpf_map_free_rcu_gp`（:734）递进工作队列，`bpf_map_free_deferred`（:691）执行 `ops->map_free`（:700），再收元数据（:710/:714）。数组侧 `array_map_free`（`arraymap.c:416`）逐元素清 BTF 字段（:428/:434）、释放每 CPU 内存（:438-439），最后 `bpf_map_area_free`（:444）归还那「一块内存」——与 3.1 对上。

**教师视角**：内核处理「多实现」的惯用法在映射侧凑齐了——命令包装管 fd、权限、拷贝与 errno 翻译；通用层管公共分支与上下文纪律；ops 表管算法。第 4 章 2.1 的 prog ops 表是它的镜像：程序侧 `bpf_prog_load` → `find_prog_type`（:2050）查 `bpf_prog_types[]` → `bpf_verifier_ops`/`bpf_prog_ops`；映射侧 `map_create` → `bpf_map_types[]`（:1166）→ `bpf_map_ops`。两张表出自同一份清单的三次展开（1.2），越界围栏同句式（:1165 与 :2056），连「fd 失败走释放链」的善后都互为倒影（2.6/:1305）——学会一侧，另一侧免费。freeze/pin 的空白行补一刀：回调表只装**随实现变化的语义**；冻结、命名这类共享属性，用标志位与引用计数在通用层解决。

**学生动手**：切片器逐站对照（站 1→节首模型图，2→3.1，3/4→3.2，5→3.3/3.4，6→3.5）。三条命令（走读树路径约定见 [`01-source-map/`](../examples/chapter-05/01-source-map/)）：

```bash
bash book/examples/chapter-05/03-map-ops/trace-map-ops.sh    # 全量六站
bash book/examples/chapter-05/03-map-ops/trace-map-ops.sh | grep "^## 站"   # 站目录
WALK_TREE=/path/to/your/tree bash book/examples/chapter-05/03-map-ops/trace-map-ops.sh
```

预期输出示例（第一、二条为 v6.6.78 树本机实跑，机检级）：第一条共 427 行、25 段代码、末行 `TREE_VERSION=v6.6.78 STATIONS=6`；第二条命中 6 行站标题；第三条换树时各站打 `NOT FOUND`（其 README 失败模式 2）。本节行号与该 README 锚点表一致，均可用一条 `grep -n` 复现；贴码区间与切片器切段不同处，以同站「定位」行为锚，漂移复核见四节 `verify-citations.sh`。

### 3.6 检查点

先自答，再看提示：

1. freeze 为什么不需要 ops 回调？pin 呢？（提示：只置 `map->frozen`（:2032），`map_get_sys_perms`（:791-792）剥 `FMODE_CAN_WRITE`；pin 只在 bpffs 建名——「对象私有语义走 ops，通用属性走通用层」（3.3/3.4）。）
2. `bpftool map update` 一个数组映射报 `-E2BIG`，是超了什么？（提示：元素创建时已预分配，「索引越界即满」——`arraymap.c:324-326`；对照 `BPF_NOEXIST` 撞 `-EEXIST`（:330），数组没有空槽（3.2）。）
3. 被程序引用的映射，close 光所有 fd、删光 pin，为何还不释放？（提示：程序登记 `used_maps` 走 `bpf_map_inc`（`verifier.c:17748`）只加 refcnt；归零才 `bpf_map_put`（:750），经 RCU 宽限期进工作队列（:758-762），`ops->map_free` 在 `bpf_map_free_deferred`（:691）执行——直接 free 会踩到还在读的 CPU（3.5）。）

**独立示例**：[`03-map-ops/`](../examples/chapter-05/03-map-ops/)——六站切片器（`trace-map-ops.sh`），每站「定位→代码段→职责注」三层输出；README 第四节三层表是节首模型图的底稿，第五、六节为实跑摘录与失败模式。
## 四、实战：把走读变成可复现的动作

**教师视角**：源码走读的生命线是「可复现」——正文每条「文件：行号」都应当能用一条命令重现。三个月后内核演进几十个版本，行号几乎必漂；行号一旦成了无法核实的孤证，本章就从教材退化成小说。一至三节已把走读做成三类可复跑资产：环境自检（[`00-env/`](../examples/chapter-05/00-env/)）、机检脚本（[`scripts/`](../examples/chapter-05/scripts/)）、两个切片器（`02-load-path/`、`03-map-ops/`），本节把它们装回五层环境阐述——走读章的口径与实验章不同，逐层换档：**依赖环境**是走读树本身；「编译环境」是机检（本章不编译 BPF 程序，机检即编译级等价物）；「部署环境」是定位手法；「运行环境」是切片器实跑；「运行效果」是汇总行与 introspection（内省查询）读数。读者容易卡住的地方：以为「照着书抄行号」就是走读——错，行号是快照，符号才是锚点，本节教你把每张快照重新对回锚点。

### 4.1 核心命令原理：机检两步法与切片器两步法

全节的命令只有两组惯用法，先讲透原理，五层里反复出现时不再解释。

**机检两步法**：`verify-citations.sh` 以 `01-source-map/citations.txt`（每行 `文件|符号|说明`）为唯一数据源，对每条引用只做两步只读检查——第一步测文件在树里存在，第二步用 `grep -F -q` 定长匹配符号仍在该文件中。**为什么按符号不按行号**：行号随每条 commit 漂移，是版本快照；符号改名、删除才是引用真正的破坏，而符号是语义单元，跨版本存活期长得多。机检能抓住：文件消失（树版本不符、`WALK_TREE` 指错、清单拼写错）、符号改名或删除、清单行本身写错；抓不住：符号还活着但行号漂了（机检不看行号）、符号还在但语义变了（要靠读代码，机器替不了）——所以正文每条行号都同时标注版本「（v6.6.78）」：机检守符号底线，行号靠第三层手法随时复核。

**切片器两步法**：`grep -n` 先定位（输出「行号：内容」），`sed -n 'A,Bp'` 再切区间（逐字贴码），`wc -l` 看规模。切片器把这两步固化成六站输出，正文二、三节每段贴码的行号区间都取自同站的 `sed` 命令——「引用皆可一条命令重现」说的就是这两步。

机检出现 FAIL 时按三类对号处置（完整排错路径见排错节）：

| FAIL 原因 | 含义 | 处置 |
|---|---|---|
| 文件不在树 | 树版本不符、`WALK_TREE` 指错目录或清单路径拼写错 | 先核对 `WALK_TREE` 与树版本，再查清单 |
| 符号未命中 | 内核改名、内联或宏展开，或清单写的形态与源码不一致 | 回修 `citations.txt`，不放水改脚本 |
| 格式错 | 清单行不是三段（分隔符不是恰好 2 个，或文件/符号字段为空） | 修清单后重跑 |

### 4.2 学生动手：按五层把走读跑一遍

按层序完整跑一遍：装树 → 过机检 → 练定位 → 跑切片 → 读数。全部动作只读源码树，不构建、不加载；机检级输出为本机 v6.6.78 树实测，预期输出逐一标注。

#### 第一层：依赖环境——走读树从哪来

走读树获取两法任选其一（与 `00-env/check-env.sh` 缺树提示中的指引一致）：

```bash
# 法一:tar 包(稳定版源码,一次到位)
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.78.tar.xz
tar xf linux-6.6.78.tar.xz

# 法二:git 浅克隆指定 tag(省磁盘与时间,可换成任意 v6.6.x)
git clone --branch v6.6.78 --depth 1 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-6.6.78
```

拿到树先核对版本：`grep ^SUBLEVEL Makefile` 在本机走读树实测输出 `SUBLEVEL = 78`（连同 `VERSION = 6`、`PATCHLEVEL = 6` 即 v6.6.78）。你的树不必与本机同版——行号引用随内核版本演进，以所用内核源码为准。工具清单与各层用途的映射（grep/sed/wc 属系统包，`bpftool` 在 Debian/Ubuntu 装在 `/usr/sbin`）：

| 工具 | 服务的层 | 缺失时的降级（check-env.sh 逐项声明） |
|---|---|---|
| grep | 机检与切片器的硬依赖（第二、四层） | 全部脚本拒绝运行，不产出假 PASS |
| sed | 切片器区间提取（第四层） | 切片降级，机检不受影响 |
| wc | 行数规模（第一、三层） | 行数统计降级 |
| bpftool | 仅第五层 introspection | 机检与切片完全不受影响 |

自检一条命令：`bash book/examples/chapter-05/00-env/check-env.sh`。输出四个检查区加三档结论，摘录第一区与结论段（本机实测，机检级）：

```text
== ① 走读树(本章机检与切片的唯一数据源) ==
OK    WALK_TREE=/work/linux/linux-6.6.78(可用 WALK_TREE=<路径> 换树自检)
OK    树版本:v6.6.78(VERSION=6 PATCHLEVEL=6 SUBLEVEL=78)
OK    kernel/bpf/syscall.c 在场(5823 行)
OK    kernel/bpf/verifier.c 在场(20459 行)
……
== 结论(三档口径,正文「四、实战」逐档引用) ==
[1] 编译级             PASS  走读树 v6.6.78 齐备 + grep/sed/wc 在场(本章无 BPF 程序,树+工具即编译级前提)
[2] 机检级(脚本与机检) PASS  check-env.sh 实跑通过;verify-citations.sh 与两个切片器对同一棵树实跑即机检级
[3] 加载级与特权查询    需要特权,输出为预期示例(运行级待验证):bpftool prog show / feature probe 等需 root
```

其余检查区（工具链、内核 BTF、权限档位）的输出见 [`00-env/`](../examples/chapter-05/00-env/)；换树自检加 `WALK_TREE=<你的树路径>` 前缀。

#### 第二层：「编译」环境=机检

```bash
bash book/examples/chapter-05/scripts/verify-citations.sh
```

每条锚点一行 PASS/FAIL，末尾汇总行（本机实测，机检级；节选）：

```text
PASS kernel/bpf/verifier.c|int bpf_check(
PASS include/linux/bpf_types.h|BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY
TOTAL=43 PASS=43 FAIL=0 WALK_TREE=/work/linux/linux-6.6.78 TREE_VERSION=v6.6.78
```

读法：43 条即正文全部「文件：行号」引用背后的符号锚点（清单见 [`01-source-map/`](../examples/chapter-05/01-source-map/)）；`TREE_VERSION=v6.6.78` 是书稿锁定常量而非自动探测，若你的 `WALK_TREE` 不是该版本，机检结果不可信——先换树，再谈对照。退出码可作门禁：FAIL=0 返回 0，否则返回 1。FAIL 时按 4.1 三类表处置：先查树与 `WALK_TREE`，再回修清单——机检的纪律是「修清单，不改脚本放水」。

#### 第三层：部署环境=定位

「部署」在走读章换档为：把自己部署到正确的目录、正确的符号前。命令目录表（前三行的输出为 v6.6.78 实测）：

| 目的 | 命令 | 输出读法 |
|---|---|---|
| 找符号在哪一行 | `grep -n 'SYSCALL_DEFINE3(bpf' kernel/bpf/syscall.c` | `5567:SYSCALL_DEFINE3(bpf, ...`——行号起读 |
| 切出一段原文 | `sed -n '5567,5570p' kernel/bpf/syscall.c` | 逐字四行，即 2.1 站 1 的贴码 |
| 估一个文件的阅读量 | `wc -l kernel/bpf/syscall.c` | `5823`——一次该读多少心里有数 |
| 可选：ctags 建索引 | 装 `universal-ctags` 后 `ctags -R kernel/bpf include/linux` | 编辑器对符号名一键跳转，适合反复走读 |
| 可选：cscope 查调用者 | 装 `cscope` 后 `cscope -Rbq` 建索引、`cscope -dL4 'bpf_check'` | 查询 4 列出谁调用该函数，适合逆向追链 |

两个提醒。其一，grep 检索的是源码文本，不存在「被编译器内联了搜不到」的问题；真正的坑在**源码里就找不到**：符号在 `#ifdef` 分支里（如 1.2 第三处展开的 `CONFIG_PROC_FS`），或藏在宏展开背后（`bpf_map_types[]` 的下标要等预处理才出现）——换关键词、回注册表是第一反应；`grep -n` 落空且机检同时报「符号未命中」时，完整排错路径见排错节。其二，定位到手别急着抄行号——先 `sed -n` 读上下文，确认符号语义与正文一致，再更新行号与版本标注。

#### 第四层：运行环境=两个切片器实跑与总装

切片器命令与输出全貌已在二、三节末的学生动手给过（v6.6.78 树本机实跑：加载链 307 行、映射链 427 行，末行同为 `TREE_VERSION=v6.6.78 STATIONS=6`），这里补**输出阅读法**——每站固定三层：`### 定位`（`grep -n` 实测的行号与函数）→ 代码段（`sed -n` 切出的原文）→ 职责注（该站职责与失败出口，回连正文小节）。读法一句话：先借定位行拿坐标，再对代码段核原文，最后沿职责注跳回正文对应小节。

总装演示：一条管道把两条链的站目录接起来，得到全章 12 站的走读地图目录：

```bash
{ bash book/examples/chapter-05/02-load-path/trace-load-path.sh | grep '^## 站' | sed 's/^## 站/加载链·站/';
  bash book/examples/chapter-05/03-map-ops/trace-map-ops.sh | grep '^## 站' | sed 's/^## 站/映射链·站/'; }
```

预期输出示例（v6.6.78 树本机实跑，机检级，12 行）：

```text
加载链·站 1:系统调用入口与分发
加载链·站 2:bpf_prog_load 签名与属性处理
加载链·站 3:验证调用点 bpf_check
加载链·站 4:JIT 选择 bpf_prog_select_runtime
加载链·站 5:fd 分配 bpf_prog_new_fd 与 prog_idr
加载链·站 6:释放链 __bpf_prog_put / bpf_prog_put
映射链·站 1:入口分发与 bpf_map_types[] 注册表
映射链·站 2:map_create 与类型查找(array_index_nospec)
映射链·站 3:通用层 bpf_map_update_value / bpf_map_copy_value 与 array_map_ops 对照
映射链·站 4:命令包装 map_lookup_elem / map_update_elem / map_delete_elem
映射链·站 5:freeze 与 pin(map_freeze / bpf_obj_pin)
映射链·站 6:释放链 bpf_map_put / bpf_map_free_deferred / bpf_map_free_rcu_gp
```

12 行即两次走读的全部停靠点：前 6 行是「加载 ≠ 挂载」的 fd 之旅，后 6 行是「fd ↔ 内存」的映射之旅；两条链在第 1 站共用同一个入口 switch，在第 6 站各走一条释放链——1.2「两张注册表」在站目录上肉眼可见。

#### 第五层：运行效果=走读地图与特权查询

前四层跑完，三行汇总并排一放，就是本章的「走读地图」——章内全部行号引用都挂在这三行事实之下（三行均为本机实测，机检级）：

```text
机检:  TOTAL=43 PASS=43 FAIL=0 WALK_TREE=/work/linux/linux-6.6.78 TREE_VERSION=v6.6.78
加载链: TREE_VERSION=v6.6.78 STATIONS=6
映射链: TREE_VERSION=v6.6.78 STATIONS=6
```

introspection 读数按权限分档。`bpftool version` 无特权可跑（本机实测；末行 `features:` 为版本输出自带的特性清单行，本机构建其后无条目）：

```text
bpftool v7.7.0
using libbpf v1.7
features:
```

`bpftool feature probe kernel`（完整探测）需要特权（报错点名 `CAP_SYS_ADMIN`/`CAP_BPF`/`CAP_NET_ADMIN`/`CAP_PERFMON`），本机非 root 且 `unprivileged_bpf_disabled=2`（一节 1.3 的开关），实测被拒、退出码 255：

```text
Error: missing CAP_SYS_ADMIN, CAP_BPF, CAP_NET_ADMIN, CAP_PERFMON, required for full feature probing; run as root or use 'unprivileged'
```

非特权档按报错提示改用 `bpftool feature probe kernel unprivileged`（本机实测，186 行，节选）——CONFIG_* 段与系统调用可用性段可信：

```text
Scanning system configuration...
JIT compiler is enabled
CONFIG_BPF is set to y
CONFIG_BPF_SYSCALL is set to y
CONFIG_BPF_JIT_ALWAYS_ON is set to y
CONFIG_DEBUG_INFO_BTF is set to y
Scanning system call availability...
bpf() syscall is available
```

但「Scanning eBPF program types...」整节全部打印 `is NOT available`——这**不是**「内核没有」：探测程序/映射类型要真实调用 `bpf(2)` 加载试探，属特权动作，非特权档一律如此报告。因此非特权输出只有 CONFIG_* 与系统调用段可信；root（或持对应能力）下完整探测，同位置应翻成 `is available` 一类（预期输出示例 / 运行级待验证）：

```text
Scanning eBPF program types...
eBPF program_type socket_filter is available
eBPF program_type kprobe is available
eBPF program_type tracing is available
……
```

一句话分档：能本机看——`bpftool version`、非特权探测的 CONFIG_*/系统调用段、被拒报错本身；需特权才能看——完整探测与程序/映射类型的真实可用性（运行级待验证）。这也把 1.3 的权限开关从源码带回了命令行：同一个 `bpf_capable()` 世界。

### 4.3 检查点

先自答，再看提示：

1. 三个月后换了台机器、树也换成更新的版本，机检全 PASS，但正文某条行号对不上——机检为什么没拦住？下一步做什么？（提示：机检按符号不按行号——符号还在、行号漂了它抓不住（4.1）；用第三层命令 `grep -n` 按符号重新定位、`sed -n` 核对上下文，再更新行号与版本标注。）
2. 机检报 `FAIL 符号未命中:kernel/bpf/syscall.c|bpf_prog_load`，属于哪一类、处置第一步是什么？（提示：三类中的「符号未命中」；第一步不是改脚本，是核对 `WALK_TREE` 的树版本与 `TREE_VERSION` 是否一致——树对不上，FAIL 未必成立于本树；再判断改名/内联/宏展开与清单形态写错，回修 `citations.txt`（4.1，完整路径见排错节）。）
3. 非特权探测输出里 `eBPF program_type socket_filter is NOT available`，能读成「内核没编 socket_filter」吗？（提示：不能——探测类型要真实加载试探，非特权档一律 NOT available（第五层）；该输出只有 CONFIG_*/系统调用段可信，真值要 root 完整探测（预期输出示例/运行级待验证）。）

**独立示例**：[`scripts/`](../examples/chapter-05/scripts/)——机检脚本 `verify-citations.sh`（头注释记录数据源、汇总行格式与三类失败模式）；[`00-env/`](../examples/chapter-05/00-env/)——环境自检 `check-env.sh`（四个检查区、三档结论、缺树时的 kernel.org 获取指引）。

## 可视化：源码树全景与加载调用流

一至三节的文字坐标收成两张图：图 1 是空间——代码「住」在哪些文件、谁允许包含谁；图 2 是时间——加载命令经过哪些站、从哪里失败。两图数据取自 [`01-source-map/`](../examples/chapter-05/01-source-map/) README 与二节实测行号，行号口径 v6.6.78。

### 图 1：源码树全景

```text
kernel/bpf/ 六区（45 个 .c 文件，65955 行，v6.6.78）＝一条 bpf 命令从进门到出门

区1 入口分发·门（1 文件）
  syscall.c            5823 bpf(2) 唯一入口：分发、fd 与权限
区2 验证·安检（4 文件）
  verifier.c          20459 验证器主体：逐条静态模拟后放行
  tnum.c                214 三值数字算术：跟踪寄存器未知位
  log.c                 327 验证日志输出通道
  disasm.c              350 内建反汇编器，指令写进日志
区3 执行与 JIT·发动机（5 文件）
  core.c               2975 解释器与通用 JIT 骨架（__weak 兜底）
  dispatcher.c          173 同类程序聚合的间接跳转表
  trampoline.c         1051 为 fentry/fexit 生成蹦床
  mprog.c               452 多程序挂载通用层
  offload.c             865 网卡硬件卸载基础设施
区4 映射实现·集装箱（14 文件）
  arraymap.c           1383 数组映射族，per-CPU 数组底座
  hashtab.c            2630 哈希映射及 LRU 变体，最常用
  ringbuf.c             809 环形缓冲区，无锁记录式输出
  lpm_trie.c            766 最长前缀匹配（路由表）
  bloom_filter.c        219 布隆过滤器映射
  queue_stack_maps.c    299 队列（FIFO）与栈（LIFO）
  stackmap.c            691 栈轨迹映射（回溯调用栈）
  map_in_map.c          150 映射嵌套映射的查找层
  cpumap.c              779 报文引流到指定 CPU
  devmap.c             1164 网卡重定向的虚拟出口表
  reuseport_array.c     353 套接字组内分流旁路表
  percpu_freelist.c     200 per-CPU 空闲链表（节点回收）
  bpf_lru_list.c        700 LRU 逐出链表
  memalloc.c            932 BPF 专用内存分配器
区5 BTF 与本地存储·说明书与贴纸（7 文件）
  btf.c                8601 BTF 解析、校验与查找
  sysfs_btf.c            45 暴露 /sys/kernel/btf/vmlinux
  bpf_local_storage.c   921 本地存储共享核心（RCU 生命周期）
  bpf_task_storage.c    374 task 本地存储映射
  bpf_inode_storage.c   237 inode 本地存储映射
  bpf_cgrp_storage.c    240 cgroup 本地存储映射
  local_storage.c       614 cgroup 本地存储旧式形态
区6 其他·各个码头（14 文件）
  cgroup.c             2602 cgroup 挂载点本体
  bpf_iter.c            848 迭代器框架与目标注册
  cgroup_iter.c         296 遍历 cgroup 层级
  task_iter.c           864 遍历进程与线程
  prog_iter.c           107 遍历已加载的程序
  link_iter.c           107 遍历已创建的 link
  map_iter.c            231 遍历系统中的映射
  bpf_lsm.c             376 程序类型暴露为 LSM 钩子
  bpf_struct_ops.c      910 BPF 实现内核 ops 表（如拥塞控制）
  tcx.c                 346 TCX ingress/egress 多程序链
  net_namespace.c       567 网络命名空间 link 清理
  cpumask.c             466 cpumask 的 kfunc 集（非映射）
  helpers.c            2622 共享辅助函数实现与原型
  inode.c               817 bpffs：pin 的最小文件系统

include/ 三层——谁能包含谁（一节 1.3）
  syscall.c 等使用方 ──include──▶ linux/bpf.h（3232 行，内核对象层，仅内核态）
                              │ :7 单向 include
                              ▼
                 uapi/linux/bpf.h（7350 行，UAPI 契约，两边可含）
  linux/bpf_types.h（153 行）——游离于包含链外：三次定义宏、三次展开（1.2）
```

解读：竖着读，六区从上到下就是一条 bpf 命令的行进顺序——进「门」分发、过「安检」放行、「发动机」给出可执行形态、「集装箱」装数据、「说明书与贴纸」管类型信息，「码头」是各挂载点共享的底座。行数一列也是阅读量预估——走读按站切、不按文件吞。横着读，每行「名字＋规模＋职责」，先认区再查名，与 [`01-source-map/`](../examples/chapter-05/01-source-map/) 全量表互为详略；图末三行是 1.3 的所有权边界。

### 图 2：BPF_PROG_LOAD 调用流

```text
用户态 loader（bpftool prog load / libbpf）
  │ bpf(BPF_PROG_LOAD, &attr, size)
  ▼
SYSCALL_DEFINE3(bpf)        syscall.c:5567
  ▼
__sys_bpf                   :5426 ──✗→ -EFAULT（:5438 拷贝失败）/-EINVAL（:5559 未知命令）
  │ 三道前置（:5431/:5438/:5441）后 switch(cmd)
  ▼
case BPF_PROG_LOAD          :5464
  ▼
bpf_prog_load               :2586 ──✗→ -EPERM（:2618/:2624/:2629 三道权限闸）
  │ 审参（CHECK_ATTR :2594 起）──✗→ -EINVAL/-EPERM
  │ 指令数 0 或超限 ──✗→ -E2BIG（:2621-2623，特权 100 万/非特权 4096）
  │ 对象分配失败 ──✗→ -ENOMEM（:2674-2681）
  ▼
bpf_check                   :2743（verifier.c:20222）──✗→ -EACCES 等（126 处，带日志）
  ▼
bpf_prog_select_runtime     :2747（core.c:2357）
  │ __weak 兜底原样返回 = 回退解释器，不是失败（*err 为 0）
  │ 必须 JIT 而 fp->jited 为假 ──✗→ -ENOTSUPP（core.c:2386-2388）
  ▼
bpf_prog_alloc_id（:2099，调用点 :2751）──✗→ -ENOSPC（:2112-2113）
  ▼
bpf_prog_new_fd             :2293（调用点 :2773）
  │ security_bpf_prog 闸；anon_inode_getfd("bpf-prog")
  │ 发 fd 失败 ──✗→ bpf_prog_put 走释放链（:2774-2775，唯一善后出口）
  ▼
返回 fd（此刻程序未挂任何钩子——加载 ≠ 挂载）
  ── 平级独立命令（共用入口 switch，互不调用）──
     case BPF_LINK_CREATE  :5534 → link_create（:5010）
     case BPF_PROG_ATTACH  :5473 → bpf_prog_attach（:3911）
```

解读：图 2 是二节六站的时间轴：竖线是成功路径，✗ 是失败出口，每个出口标了 errno 与拦截行号，与 2.6 的 errno 总表对应。竖读可见一条分界线——`-EPERM`/`-E2BIG`/`-ENOMEM` 全部拦在 `bpf_check` 之前，`-EACCES` 是唯一「进了验证才可能拿到」的拒绝：第 3 章排错节第三组「先答进没进验证」，在图上就是这条线的位置。图末两条平级命令重复 2.6 的提醒：fd 返回不等于程序开始工作。换树时行号漂移而拓扑不变——按锚符号重定位（排错②）即可整图搬进新树。

## 常见问题与排错

本章的排错对象不是「程序跑不起来」，而是「走读走不下去」：符号找不到、行号对不上、机检报警、切片器罢工、查询命令被拒。与第 3 章排错节的分工先讲清：那边查「验证器为什么拒绝程序」——失败画廊把七类拒绝收成日志信号速查表（画廊 1–7，对应 3.1–3.7）；这边查「走读者为什么在源码树里迷路」。凡涉及加载与运行的报错，口径仍是预期输出示例 / 运行级待验证（本机非 root，见 4.2 第五层）。

**排错①：符号 grep 不到。**三种情形。其一，改名或搬文件——内核提交会重命名函数、移动文件：先 grep 短名（去掉类型与括号，查 `bpf_prog_load` 而非 `static int bpf_prog_load(`），落空再放开到全树 `grep -rn`，再查 `include/` 下的声明（`grep -rn 符号 include/linux`）；反复走读就装 `universal-ctags` 建索引，编辑器对符号一键跳转（4.1 第三层命令表）。其二，内联与宏展开——`static inline` 函数在调用点展开为机器码，源码定义仍在、grep 仍能命中；真正 grep 不到的是**展开才存在**的记号：1.2 讲过的注册表下标与 `##` 拼名，本树实测一例——`grep -rn sk_filter_prog_ops` 全树只命中定义处一处（`net/core/filter.c:10953`），注册表里的引用是预处理拼接出来的，要回 1.2 的三处展开现场去读。其三，`#ifdef` 分支里的符号——本树没开对应配置（如第三处展开所在的 `CONFIG_PROC_FS`）就 grep 不到；换关键词、回注册表是第一反应（4.1 第三层的两个提醒同口径）。

**排错②：行号对不上（版本漂移定位法）。**行号是快照，符号是锚点——对不上先核对正文标注的「（v6.6.78）」与你的树是否同版，再怀疑事实。重定位两步：第一步**锚符号重定位**，用贴码头注里的函数定义行做锚（如 `SYSCALL_DEFINE3(bpf`），`grep -n` 在你的树里拿到新行号；第二步**±偏移**，函数内部的引用行 ≈ 新锚行 +（旧引用行 − 旧锚行）——如二节站 3 的 `bpf_check` 调用点 ：2743 在锚 `bpf_prog_load` :2586 之下 157 行，换树后在新的锚行 +157 附近核对。切片器两步法就是这套动作的固化：`grep -n` 定位、`sed -n 'A,Bp'` 核对上下文（4.1）；校准后把新行号与版本标注一起更新，机检不受影响——它按符号不按行号（4.1）。

**排错③：机检 FAIL 三类原因与修复。**4.1 的三类表是速查，这里给每类的完整处置链。**文件不在树**：先 `grep ^SUBLEVEL Makefile` 验树版本，再核 `WALK_TREE` 指向，最后查 `citations.txt` 路径拼写——三步做完仍在 FAIL 才谈其他。**符号未命中**：第一步核对汇总行 `TREE_VERSION` 与你的树是否一致——树对不上，FAIL 未必成立于本树（4.3 检查点 2 同口径）；再按排错①的检索策略判改名/搬文件/宏展开，找到新形态后**回修 `citations.txt`** 对应行（保持「文件|符号|说明」三段），不放水改脚本——机检守的是「清单与树的一致性」，FAIL 修清单，不修现实。**格式错**：清单行分隔符不是恰好 2 个 `|` 或字段为空，修行重跑。修完以汇总行 `FAIL=0` 收口（退出码 0，可作门禁）。

**排错④：切片器 NOT FOUND。**两个切片器的预期行为分两档（各 README 失败模式表，均已实测）。树不在：第一站之前就报「走读树目录不存在」并退出，一站都不产（失败模式 1）。树在但锚点漂移：对应站打 `STATION n: NOT FOUND` 后**继续其余站**，退出码 1（失败模式 2）——降级粒度由锚点存活情况决定：实测同一加载链切片器指向 4.19 树时站 1/6 NOT FOUND、站 2–5 照常切出，映射链则六站全灭（通用层符号 5.x 才引入）。两档的分界就是判别法：`NOT FOUND` ≠ 树没挂上——后者连站目录都出不来。处置：回你的树 `grep -n` 新位置，改该站锚点模式与偏移，同步更新校准注释；stderr 的版本 WARN（失败模式 3）同时提醒——非 v6.6.78 树上行号仅供参考，换树遇到 NOT FOUND 属预期。

**排错⑤：bpftool 特权报错。**本机口径（4.2 第五层实测）：非 root 且 `unprivileged_bpf_disabled=2`（一节 1.3 的开关）时，`bpftool feature probe kernel` 报缺 `CAP_SYS_ADMIN`/`CAP_BPF`/`CAP_NET_ADMIN`/`CAP_PERFMON`、退出码 255——**这是预期行为，不是故障**，源码出处就是 ：2618 一族的权限闸。处置两档：无特权改走 `bpftool feature probe kernel unprivileged`（输出只信 CONFIG_*/系统调用段——程序/映射类型段一律 NOT available，探测要真实加载，4.2 已析）或无特权可跑的 `bpftool version`；有特权环境以 root 重跑完整探测（预期输出示例 / 运行级待验证）。与第 3 章排错节第三组的分工：那边判「加载被 `-EPERM` 拒，进没进验证」；这边是内省查询本身被拒——连属性块都没递进门。都叫「权限失败」，别混着查。

## 课后扩展任务

一至三节带你走完两条现成的链，两项扩展任务把 4.1 的两步法变成你自己的手艺——一项补链，一项扩链（参考实现 README 按「任务在前、思路在后」防剧透结构组织）：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 走读链补全 | 追踪 `BPF_MAP_FREEZE` 完整调用链，自制「freeze 走读卡」 | [`exercises/01-trace-freeze/`](../examples/chapter-05/exercises/01-trace-freeze/) |
| 2 | 切片器扩展 | 给 `trace-map-ops.sh` 新增 `BPF_BTF_LOAD` 站 | [`exercises/02-extend-tracer/`](../examples/chapter-05/exercises/02-extend-tracer/) |

**任务 1：追踪 BPF_MAP_FREEZE 完整调用链。**3.3 只展开了 freeze 的通用层收口，完整链要从入口分发走到底。要求：沿切片器两步法（4.1）走通 `case BPF_MAP_FREEZE` → `map_freeze` 全程，产出带行号标注的「freeze 走读卡」——至少覆盖入口分发行、`map_freeze` 全函数、三道并发闸的顺序、写命令撞 `-EPERM` 的拦截点；按 2.6 errno 总表的格式整理失败出口；说明 freeze 与 3.1 动作①相比少了什么、为什么（回调表与善后链两个角度）。交付物仿 [`citations.txt`](../examples/chapter-05/01-source-map/) 的「文件|符号|说明」格式列锚点清单，机检 `FAIL=0` 收口。提示：3.3/3.5 的模型图是底稿；「冻结为什么拦得住写」的另一半在写计数（3.2）。

**任务 2：给 trace-map-ops.sh 增加 BPF_BTF_LOAD 站。**切片器是可扩展的——每个站只是「锚点模式 + sed 区间 + 职责注」三件套。要求：在映射链切片器里新增第七站，切出 `case BPF_BTF_LOAD`（与 `BPF_MAP_CREATE` 同一条 switch 的平级命令）及其命令实体的头若干行，标注它进入 `kernel/bpf/btf.c`（1.1 区 5）的边界；输出三层（定位→代码段→职责注）与汇总行和既有六站一致；在 README 失败模式表补一条换树实测行为。提示：站 2 的锚点写法是现成模板；命令实体函数名自己 grep——这正是排错①练过的检索。

**防剧透约定**：两项练习的 README 都把任务描述与提示放在前部、参考实现思路独立成节放在后部——先只读前部自行走读，卡住再回对应小节补课（任务 1 回 3.2/3.3，任务 2 回 4.1 与排错①②），自检后才翻分界线之后的「参考实现思路」。验证状态：两项均为机检级（不涉及加载与运行），以各目录 README 为准。

## 总结

本章把前四章一直在「用」的门框拆开看了一遍，收成五点内化，逐条对回学习目标：

1. **源码地图**：`kernel/bpf/` 45 个 .c 文件 65955 行按「一条 bpf 命令从进门到出门」分六区——门（入口分发）、安检（验证）、发动机（执行与 JIT）、集装箱（映射实现）、说明书与贴纸（BTF 与本地存储）、码头（其他）；三层 include 立起所有权边界——uapi 层两边可含、内核对象层仅内核态、`bpf_types.h` 只被展开；地图的灵魂是 1.2 的双展开：一份 153 行的清单三次展开出两张注册表与一张名字表，新增类型只动三处、注册表自动生长（1.1–1.3，图 1）。
2. **加载四拍**：验证（:2743）→ 选执行（:2747）→ 编号（:2751）→ 发 fd（:2773），次序由失败代价决定——不可见的失败 `goto` 清理、可见的失败引用计数善后；fd 返回时程序未挂任何钩子，挂载是平级的独立命令（二节，图 2）。
3. **映射三层**：命令包装管 fd、权限、拷贝与 errno 翻译，通用层管公共分支与上下文纪律，ops 回调管算法；冻结与 pin 这类共享属性不走回调表——回调表只装随实现变化的语义（三节）。
4. **机检纪律**：正文每条「文件：行号」都应能用一条命令重现；`verify-citations.sh` 按符号守底线，三类 FAIL 对号处置，修清单不放水（四节、排错③）。
5. **版本漂移心法**：行号是快照、符号是锚点；换树先「锚符号重定位 ± 偏移」，再用切片器两步法核对上下文——行号漂移不是麻烦，是走读者必须掌握的技能（排错①②④）。

通往第 6 章的桥：二节站 4 把 JIT 留在门口——通用骨架（`core.c:2357`）与 `__weak` 兜底（:2884）已点名；各架构后端如何把 eBPF 指令逐条翻成本机机器码是第 6 章的正题——带着图 1 去读「发动机」区里你还没进过的那个房间。

## 本章参考来源

- Linux 内核源码树（v6.6.78，本章走读树；获取指引见 4.2 第一层）：`kernel/bpf/` 全部 45 个 .c 文件（全量表见 [`01-source-map/`](../examples/chapter-05/01-source-map/)）、`net/core/filter.c`（程序类型 prog_ops 之家）与三份头文件——`include/uapi/linux/bpf.h`（7350 行，UAPI 契约：命令字枚举、`union bpf_attr`、辅助函数编号）、`include/linux/bpf.h`（3232 行，内核对象层）、`include/linux/bpf_types.h`（153 行，注册表清单）。
- Linux kernel documentation：`Documentation/bpf/index.rst`（BPF 子系统文档总目录）、`Documentation/bpf/btf.rst`（BTF 格式与工具链）、`Documentation/bpf/classic_vs_extended.rst`（cBPF 与 eBPF 指令集对照）——随内核版本演进，以所用树为准。
- `include/uapi/linux/bpf.h`：命令字与辅助函数原型的权威注释（第 3 章 2.7 白名单口径的出处）。
- 前章回指：第 2 章（八类映射与生命周期六步）、第 3 章（验证器机制与失败画廊 1–7）、第 4 章（程序类型与挂载点）——本章为三者提供源码坐标，此后各章沿用本章地图。
- ebpf.top：BPF 入门系列相关文章（内核源码结构的中文讲解）。

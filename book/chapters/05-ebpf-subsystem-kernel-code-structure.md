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

<!-- 二节（T7）：## 二、加载路径追踪：BPF_PROG_LOAD 的一站一停 -->
<!-- 三节（T7）：## 三、映射生命周期：从一个 fd 到一块内存 -->
<!-- 四节（T8）：## 四、实战：把走读变成可复现的动作 -->
<!-- 可视化（T8）：## 可视化：源码树全景与加载调用流 -->
<!-- 排错（T9）：## 常见问题与排错 -->
<!-- 课后（T9）：## 课后扩展任务 -->
<!-- 总结（T9）：## 总结 -->
<!-- 参考来源（T9）：## 本章参考来源 -->

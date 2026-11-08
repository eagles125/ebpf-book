/* compat.bpf.c —— 第 8 章练习② 跨版本兼容分支(BPF 侧,参考实现)
 *
 * 【文件用途】
 *   仿 ../03-core-read/ 写的跨版本兼容分支参考实现:用
 *   bpf_core_field_exists(tools/lib/bpf/bpf_core_read.h:132,v6.6.78)
 *   与 bpf_core_type_exists(同文件 :185)各构造一个兼容分支,挂
 *   tracepoint(跟踪点)sched/sched_process_exec,经 ring buffer 把
 *   判定结果发给用户态(加载运行为预期档,需 root)。
 *
 *   探测对象刻意选「本机 BTF 在场、v6.6.78 走读树不在场」的一对:
 *     - 字段级:task_struct 的 scx 成员(struct sched_ext_entity,本机
 *       实况:成员序 23、位偏移 5952=字节 744,切片器站 5 实测;在 v6.6.78 源码树 grep `scx` 与
 *       `sched_ext_entity` 均无命中——字段随版本漂移,正是 CO-RE 用武之地);
 *     - 类型级:struct sched_ext_entity(本机 BTF [149960] size=208 vlen=23,
 *       本机实况;同树 grep 无命中)。
 *   同一份 .bpf.o:目标内核有 scx → 判 1、读真值;没有 → 判 0、走哨兵分支
 *   (分支走向由加载期 CO-RE 引擎按目标 BTF 常量化,运行行为预期档)。
 *
 * 【验证状态】编译级已验证(本机 clang 18.1.3 实测产 compat.bpf.o,
 *   make 零告警);加载运行为预期输出示例/运行级待验证(需 root,
 *   本机 unprivileged_bpf_disabled=2)。
 *
 * 【安全声明】只读当前任务的 pid 与一个存在性探测字段,不写任何内核
 *   状态;事件仅含这几个字段,不含敏感内存。
 */

/* 本机内核(7.0)BTF 生成的 vmlinux.h 里有几处「无声明符声明」(BTF 里悬在
 * struct/union 内部的 FWD 构造,bpftool 原样发射),clang 18 会告警
 * -Wmissing-declarations;属生成头文件的固有噪音,与本示例代码无关,故在
 * include 处局部静音,保持 make 全链零告警(与 ../03-core-read/ 同款处理)。 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* 事件结构。skeleton(骨架)不搬运 BPF 侧类型,真接用户态加载器时另一份
 * 同布局定义(bpftool gen skeleton 模式的常规,见 03);本练习止步编译级,
 * 不配加载器。 */
struct compat_event {
	__u32 pid;		/* BPF_CORE_READ(task, pid) */
	__u8 scx_exists;	/* bpf_core_field_exists(task->scx):1 有/0 无 */
	__u8 entity_exists;	/* bpf_core_type_exists(struct sched_ext_entity):1/0 */
	__u32 entity_size;	/* 存在时 bpf_core_type_size 的目标类型字节数,否则 0 */
	__u32 scx_weight;	/* 存在则读 task->scx.weight 真值,否则哨兵 0 */
};

/* ring buffer map:4 页(须为页大小的 2 的幂);仅承载事件,不上 Loader */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 12);
} events SEC(".maps");

SEC("tracepoint/sched/sched_process_exec")
int compat_probe(void *ctx)
{
	struct task_struct *task;
	struct compat_event *e;

	/* 本例不碰 tracepoint 上下文(ctx):要的数据都在 task_struct 里 */
	task = bpf_get_current_task_btf();

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	e->pid = BPF_CORE_READ(task, pid);

	/* ---- 字段级兼容分支:bpf_core_field_exists(task->scx) ----
	 * 编成 BPF_CORE_FIELD_EXISTS(kind 2)重定位:加载期 libbpf 按目标
	 * 内核 BTF 把 1/0 写进寄存器;目标缺字段时引用它的取数指令被投毒
	 * (poison,见正文 4.2),验证器淘汰死分支——只走目标内核有啥读啥
	 * 的那条路。 */
	if (bpf_core_field_exists(task->scx)) {
		e->scx_exists = 1;
		e->scx_weight = BPF_CORE_READ(task, scx.weight);
	} else {
		e->scx_exists = 0;
		e->scx_weight = 0;	/* 哨兵值:目标内核 task_struct 无此成员 */
	}

	/* ---- 类型级兼容分支:bpf_core_type_exists(struct sched_ext_entity) ----
	 * 守卫式用法:类型在目标内核存在,才敢向它要大小/挂它的指针——
	 * 存在性判定是后续访问的闸门,不是装饰。 */
	if (bpf_core_type_exists(struct sched_ext_entity)) {
		e->entity_exists = 1;
		e->entity_size = bpf_core_type_size(struct sched_ext_entity);
	} else {
		e->entity_exists = 0;
		e->entity_size = 0;	/* 哨兵值:目标内核无此类型 */
	}

	/* 教案点(主审口径):字段存在≠语义一致——CO-RE 保证的是「布局修正」
	 * (字段在目标内核的偏移/大小按目标 BTF 重算),不保证「语义不变」:
	 * 同名成员跨版本可能改单位、改极性、改取值集合;存在性分支只挡
	 * 「没有」,挡不住「有但变了」,后者要靠版本判据或字段命名隔离。
	 * (bpf_core_field_exists 支持两种形式:task->scx 与
	 *  struct task_struct, scx——本例用前者,后者见宏头注。) */

	bpf_ringbuf_submit(e, 0);
	return 0;
}

/* core.bpf.c —— 第 8 章 03-core-read:第一个 CO-RE 程序(BPF 侧)
 *
 * 【文件用途】
 *   挂在 tracepoint(跟踪点)sched/sched_process_exec——任意进程成功 exec
 *   (载入新程序镜像)即触发——用 helper(辅助函数)bpf_get_current_task_btf()
 *   取当前任务的 struct task_struct 指针,再用 BPF_CORE_READ 宏族读它的
 *   pid/comm 字段,连同 bpf_core_field_exists 兼容分支的判定结果,经
 *   map(映射,类型为 ring buffer/环形缓冲区)发给用户态 loader.c 打印。
 *
 *   这是章正文「三、CO-RE 编译期」的实证面:include vmlinux.h 之后(文件头
 *   preserve_access_index 的机制导读见 ../02-vmlinux-h/),BPF_CORE_READ 的
 *   每个字段访问都被 clang 编成「按本机 BTF 偏移读 + .BTF.ext 重定位记录」,
 *   加载到其他内核时由 CO-RE 引擎按目标内核 BTF 修正偏移(逐条切片指认见
 *   ../04-relo-slicer/)。
 *
 * 【验证状态】编译级已验证(本机 clang 18.1.3 实测产 core.bpf.o);
 *   加载运行为预期档,需 root(loader.c 负责加载,README 有预期输出示例)。
 *
 * 【安全声明】只读当前任务的三个字段,不写任何内核状态;事件仅含这几个
 *   字段与一个存在性标志,不含敏感内存。
 */

/* 本机内核(7.0)BTF 生成的 vmlinux.h 里有 5 处「无声明符声明」(如第 42426
 * 行 struct aes_enckey;——BTF 里悬在 struct/union 内部的 FWD 构造,bpftool
 * 7.7.0 原样发射),clang 18 会告警 -Wmissing-declarations;属生成头文件的
 * 固有噪音,与本示例代码无关,故在 include 处局部静音,保持 make 全链零告警
 * (vmlinux.h 本身不动:它必须与 bpftool btf dump 的产物逐字节一致)。 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

/* bpf_get_current_task_btf 是 GPL-only 辅助函数(6.6.78 走读树 kernel/trace/
 * bpf_trace.c :776,.gpl_only = true;机检以符号 bpf_get_current_task_btf_proto
 * 为准);license 字符串含 "GPL" 即通过内核的 GPL 兼容校验,"Dual BSD/GPL"
 * 与 ch09 各示例同款。 */
char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* 事件结构。skeleton(骨架)不搬运 BPF 侧类型,loader.c 另有一份同布局
 * 定义——这是 bpftool gen skeleton 模式的常规(与 ch07 03-event-output 同款)。
 * 字节偏移为本机实况(bpftool btf dump 的 bits_offset/8,其他内核不同,
 * 正是要被 CO-RE 修正的东西);kstack_offset 及其存在性标志是给兼容分支
 * 留的展示位,见下方 exec 函数。 */
struct exec_event {
	__u32 pid;		/* BPF_CORE_READ(task, pid);本机实况:字节偏移 2800 */
	char comm[16];		/* BPF_CORE_READ_STR_INTO;本机实况:字节偏移 3344 */
	__u32 kstack_offset;	/* 兼容分支演示字段;本机实况:字节偏移 9544 */
	__u8 kstack_offset_exists; /* bpf_core_field_exists 判定:1 有/0 无 */
};

/* ring buffer map:1MB(须为页大小的 2 的幂);loader 用 ring_buffer__poll 收 */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 20);
} events SEC(".maps");

SEC("tracepoint/sched/sched_process_exec")
int core_read_exec(void *ctx)
{
	struct task_struct *task;
	struct exec_event *e;

	/* 本例不碰 tracepoint 上下文(ctx):要的数据都在 task_struct 里,
	 * 用 BTF 指针直取——vmlinux.h 把类型搬进编译单元的价值正在于此。 */

	/* 直接拿到当前任务的 task_struct 可信指针(6.6.78 走读树 bpf_trace.c
	 * :777 返回类型 RET_PTR_TO_BTF_ID_TRUSTED:非空、可信,免判空即可
	 * 字段访问);这个返回类型能写出来,靠的正是 vmlinux.h 里的
	 * task_struct 定义。 */
	task = bpf_get_current_task_btf();

	/* ringbuf 惯用法:先 reserve 预留、填好再 submit 提交;缓冲满静默丢 */
	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	/* BPF_CORE_READ(task, pid):生成「读本机偏移 + .BTF.ext 重定位记录」,
	 * 加载时由 CO-RE 引擎按目标内核 BTF 把偏移修正后再读 */
	e->pid = BPF_CORE_READ(task, pid);
	/* 字符串字段用 STR 变体:走 bpf_core_read_str,按 NUL 截断拷贝 */
	BPF_CORE_READ_STR_INTO(&e->comm, task, comm);

	/* ---- 兼容分支演示:bpf_core_field_exists(task->kstack_offset) ----
	 * 字段漂移的本地实证:同一个名字,两个位置——
	 *   本机内核(7.0)BTF:task_struct 的成员,u32(字节偏移 9544,
	 *   成员序 258;编译进指令立即数的 0x2548 见 README 反汇编摘录);
	 *   6.6.78 走读树:不是 task_struct 成员,而是 per-CPU 变量
	 *     (include/linux/randomize_kstack.h :12 的 DECLARE_PER_CPU)。
	 * task_struct 字段随版本漂移,正是 CO-RE 的用武之地:
	 *   编译期:字段必须在本机 vmlinux.h(即本机 BTF)里存在才编得过
	 *     ——这正是本章要教的工作流(先看本机 BTF,再写代码);
	 *   加载期:bpf_core_field_exists 编成 BPF_CORE_FIELD_EXISTS 重定位,
	 *     libbpf 按目标内核 BTF 把判定值(1/0)写进寄存器;若某分支引用了
	 *     目标内核缺失的字段,libbpf 会把那条指令投毒(poison,替换成
	 *     invalid func unknown#195896080,relo_core.c bpf_core_poison_insn),
	 *     而 verifier 只校验可达路径——常量化后的条件让死分支被淘汰,
	 *     程序照样加载,永远只走「目标内核有啥读啥」的那条路。 */
	if (bpf_core_field_exists(task->kstack_offset)) {
		e->kstack_offset_exists = 1;
		e->kstack_offset = BPF_CORE_READ(task, kstack_offset);
	} else {
		e->kstack_offset_exists = 0;
		e->kstack_offset = 0;	/* 哨兵值:目标内核 task_struct 无此成员 */
	}

	bpf_ringbuf_submit(e, 0);
	return 0;
}

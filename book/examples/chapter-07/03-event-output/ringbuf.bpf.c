/* ringbuf.bpf.c —— 第 7 章 03-event-output 的 ringbuf 变体(BPF 侧)
 *
 * 文件用途:演示 ring buffer 的事件输出——tracepoint 触发时用
 * bpf_ringbuf_reserve 预留、填好后 bpf_ringbuf_submit 提交(reserve/commit
 * 两段式,可中途 discard 丢弃);缓冲满时 reserve 返回 NULL,静默丢弃。
 *
 * 教学要点:reserve/commit 把「填事件」与「发布事件」分开——提交前读者
 * 看不见半成品,这是与 bpf_perf_event_output 一次性拷贝的关键语义差
 * (正文四节)。
 *
 * 安全声明:本程序只编译不加载;加载由 ringbuf.c 在 root 档完成(运行级待验证)。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct exec_event {
	__u32 pid;
	char comm[16];
};

/* 大小按字节给:必须是页大小的 2 的幂;此处 1MB */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 20);
} events SEC(".maps");

SEC("tp/syscalls/sys_enter_execve")
int ringbuf_exec(struct trace_event_raw_sys_enter *ctx)
{
	struct exec_event *e;

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;	/* 缓冲满:静默丢弃(丢失语义可观测,正文四节) */

	e->pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(e->comm, sizeof(e->comm));
	bpf_ringbuf_submit(e, 0);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";

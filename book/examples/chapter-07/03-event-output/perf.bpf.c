/* perf.bpf.c —— 第 7 章 03-event-output 的 perf buffer 变体(BPF 侧)
 *
 * 文件用途:同一事件源(execve tracepoint)走 perf buffer——
 * bpf_perf_event_output 一次性把事件拷进当前 CPU 的 perf 环;
 * BPF_MAP_TYPE_PERF_EVENT_ARRAY 按 CPU 号索引,由 libbpf 替每个 CPU 挂上事件。
 *
 * 教学要点:与 ringbuf 的 reserve/commit 两段式相对,perf 是「一把拷贝」:
 * 单值语义、无半成品问题,但跨 CPU 顺序与丢失行为不同(正文四节五点对比)。
 *
 * 安全声明:本程序只编译不加载;加载由 perf.c 在 root 档完成(运行级待验证)。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct exec_event {
	__u32 pid;
	char comm[16];
};

/* perf 事件数组:key=CPU 号(由 BPF_F_CURRENT_CPU 填),value=perf 事件 fd */
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(max_entries, 128);
	__type(key, __u32);
	__type(value, __u32);
} events SEC(".maps");

SEC("tp/syscalls/sys_enter_execve")
int perf_exec(struct trace_event_raw_sys_enter *ctx)
{
	struct exec_event e = {};

	e.pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(e.comm, sizeof(e.comm));
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));

	return 0;
}

char LICENSE[] SEC("license") = "GPL";

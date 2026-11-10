// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 03-ringbuf-to-perf
 * （04-ring-buffer 的输出机制迁移练习：ring buffer -> perf buffer）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_execve（与 04 相同）
 * 输出：BPF_MAP_TYPE_PERF_EVENT_ARRAY + bpf_perf_event_output
 *
 * 事件结构（perf_exec_common.h）、挂载点、输出列（PID/CGROUP_ID/COMM）
 * 与 04 完全一致，唯一的差别是事件通道：ring buffer 的
 * reserve/submit 两段式换成 bpf_perf_event_output(BPF_F_CURRENT_CPU)
 * 一次调用。与 04（`../../04-ring-buffer/`）逐行 diff，内核态的全部
 * 差别就集中在 events map 定义与程序体最后的输出调用两处。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "perf_exec_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} events SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int perf_exec(void *ctx)
{
	struct event e = {};

	e.pid = bpf_get_current_pid_tgid() >> 32;
	e.cgroup_id = bpf_get_current_cgroup_id();
	bpf_get_current_comm(&e.comm, sizeof(e.comm));

	/* 写入“当前 CPU”对应的 perf buffer；BPF_F_CURRENT_CPU 由内核
	 * 解析为当前 CPU id，作为 events map 的 key。
	 * 与 ring buffer 的 reserve/submit 两段式不同：没有“预留失败”
	 * 背压路径——per-CPU 缓冲满时事件在内核侧被直接丢弃并计数，
	 * 丢失情况由用户态的丢失回调上报。 */
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));
	return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 04-ring-buffer
 * （对应 ebpf.top BPF 入门系列实践教程四：在 eBPF 中使用 ring buffer）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_execve
 * 输出：BPF_MAP_TYPE_RINGBUF + bpf_ringbuf_reserve/submit
 *
 * 与 01-03 的 bpf_printk 相比，本示例开始输出“结构化事件”：
 * 用户态按 struct event 解析，不再依赖文本格式。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "ringbuf_exec_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);	/* 256 KB */
} events SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int ringbuf_exec(void *ctx)
{
	struct event *e;

	/* 从 ring buffer 预留事件空间；满时返回 NULL（背压路径） */
	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	/* reserve 成功后填充字段 */
	e->pid = bpf_get_current_pid_tgid() >> 32;
	e->cgroup_id = bpf_get_current_cgroup_id();
	bpf_get_current_comm(&e->comm, sizeof(e->comm));

	/* 提交后用户态才可见；若决定不输出，应改调 bpf_ringbuf_discard */
	bpf_ringbuf_submit(e, 0);
	return 0;
}

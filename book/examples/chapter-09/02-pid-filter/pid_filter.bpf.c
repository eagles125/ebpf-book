// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 02-pid-filter（对应 ebpf.top BPF 入门系列实践教程二：通过 PID 过滤）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_execve
 * 输出：bpf_printk -> trace_pipe
 *
 * 在 01-hello-world 基础上引入第一张 map：用户态把目标 PID 写入
 * target_pid（ARRAY map，key 0），内核态每次触发时读取并过滤。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* key 0 保存目标 PID；值为 0 表示“不过滤，输出所有进程”。 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} target_pid SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int pid_filter(void *ctx)
{
	char comm[16];
	__u32 key = 0;
	__u32 current = bpf_get_current_pid_tgid() >> 32;
	__u32 *target = bpf_map_lookup_elem(&target_pid, &key);

	/* map 查找可能返回 NULL，必须先判断再解引用 */
	if (target && *target != 0 && *target != current)
		return 0;

	bpf_get_current_comm(comm, sizeof(comm));
	bpf_printk("execve: pid=%u comm=%s\n", current, comm);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 08-open-file-tracepoint
 * （对应 ebpf.top BPF 入门系列实践教程八：捕获进程打开文件信息 tracepoint 篇）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_openat + sys_exit_openat
 * 输出：ring buffer
 *
 * 与 07（kprobe/kretprobe 版）功能相同，但使用内核显式提供的稳定事件点：
 * 字段格式来自 tracefs 的 format 文件，不依赖内核内部函数名和参数布局。
 *
 * 运行前建议先核对字段（字段布局以当前内核为准）：
 *   cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/format
 *   cat /sys/kernel/tracing/events/syscalls/sys_exit_openat/format
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "opensnoop_tp_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct inflight_value {
	char comm[16];
	char filename[FILENAME_LEN];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/* key: pid_tgid（64 位，理由同 07-open-file-return） */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct inflight_value);
} inflight SEC(".maps");

/*
 * trace_event_raw_sys_enter 对应 format 文件中的字段布局：
 *   name: sys_enter_openat
 *   field:unsigned short common_type; ...
 *   field:int __syscall_nr;  field:int dfd;
 *   field:const char __user * filename;  field:int flags;  field:umode_t mode;
 * syscall 参数统一放在 ctx->args[]：args[0]=dfd, args[1]=filename, ...
 */
SEC("tracepoint/syscalls/sys_enter_openat")
int tp_openat_enter(struct trace_event_raw_sys_enter *ctx)
{
	struct inflight_value v = {};
	__u64 pid_tgid = bpf_get_current_pid_tgid();

	bpf_get_current_comm(&v.comm, sizeof(v.comm));
	bpf_probe_read_user_str(&v.filename, sizeof(v.filename),
				(const void *)ctx->args[1]);

	bpf_map_update_elem(&inflight, &pid_tgid, &v, BPF_ANY);
	return 0;
}

/*
 * sys_exit_openat 的 format：
 *   field:int __syscall_nr;  field:long ret;
 * 返回值在 ctx->ret，不需要 kretprobe。
 */
SEC("tracepoint/syscalls/sys_exit_openat")
int tp_openat_exit(struct trace_event_raw_sys_exit *ctx)
{
	struct inflight_value *v;
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct event *e;

	v = bpf_map_lookup_elem(&inflight, &pid_tgid);
	if (!v)
		return 0;

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (e) {
		e->pid = pid_tgid >> 32;
		e->ret = ctx->ret;
		__builtin_memcpy(&e->comm, v->comm, sizeof(e->comm));
		__builtin_memcpy(&e->filename, v->filename, sizeof(e->filename));
		bpf_ringbuf_submit(e, 0);
	}

	bpf_map_delete_elem(&inflight, &pid_tgid);
	return 0;
}

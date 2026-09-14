// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 07-open-file-return
 * （对应 ebpf.top BPF 入门系列实践教程七：捕获进程打开文件信息返回值）。
 *
 * 挂载点：kprobe/do_sys_openat2 + kretprobe/do_sys_openat2
 * 输出：ring buffer
 *
 * enter 阶段能拿到 filename 但不知道结果；exit 阶段能拿到返回值但
 * filename 参数已不便读取。解法：enter 时把 {comm, filename} 暂存到
 * inflight HASH map（key = pid_tgid 64 位，线程安全），exit 时查回、
 * 补上返回值、输出完整事件，最后删除临时项。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "openret_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct inflight_value {
	char comm[16];
	char filename[FILENAME_LEN];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/* key: pid_tgid（64 位 = tgid<<32 | tid，避免多线程同进程冲突） */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct inflight_value);
} inflight SEC(".maps");

SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(openret_enter, int dfd, const char *filename,
	       struct open_how *how)
{
	struct inflight_value v = {};
	__u64 pid_tgid = bpf_get_current_pid_tgid();

	bpf_get_current_comm(&v.comm, sizeof(v.comm));
	bpf_probe_read_user_str(&v.filename, sizeof(v.filename),
				(const void *)filename);

	bpf_map_update_elem(&inflight, &pid_tgid, &v, BPF_ANY);
	return 0;
}

/* BPF_KRETPROBE 的第一个参数即返回值，等价于 PT_REGS_RC(ctx) */
SEC("kretprobe/do_sys_openat2")
int BPF_KRETPROBE(openret_exit, long ret)
{
	struct inflight_value *v;
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct event *e;

	v = bpf_map_lookup_elem(&inflight, &pid_tgid);
	if (!v)
		return 0;	/* enter 阶段没记录到（如缓冲已满），跳过 */

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (e) {
		e->pid = pid_tgid >> 32;
		e->ret = ret;
		__builtin_memcpy(&e->comm, v->comm, sizeof(e->comm));
		__builtin_memcpy(&e->filename, v->filename, sizeof(e->filename));
		bpf_ringbuf_submit(e, 0);
	}

	/* 临时状态必须清理，否则 map 持续增长、旧 filename 会串到后续事件 */
	bpf_map_delete_elem(&inflight, &pid_tgid);
	return 0;
}

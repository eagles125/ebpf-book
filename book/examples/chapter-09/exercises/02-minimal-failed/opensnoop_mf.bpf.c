// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 02-minimal-failed
 * （05-open-file-kprobe 的新增字段练习：两级事件 opensnoop）。
 *
 * 挂载点：kprobe/do_sys_openat2 + kretprobe/do_sys_openat2
 * 输出：ring buffer
 *
 * 在 05 的 opensnoop 事件上做「两级事件」改造：
 *   成功路径（fd >= 0）：事件保持 minimal 信息（pid/comm/filename），
 *                        用户态按 05 同形的三列打印；
 *   失败路径（ret < 0）：事件带 ret = -errno 与 failed=1，用户态把
 *                        -errno 映射成 errno 名（如 -2 -> ENOENT），
 *                        在 minimal 三列的行尾追加诊断信息。
 *
 * 与 07-open-file-return 的口径区分：07 教的是 enter/exit 关联机制本身，
 * 每个 open 输出一条带 RET 列的完整事件（fd=N / ERR=-2(ENOENT)）；
 * 本练习的重点在事件结构演化与输出语义——排障视角下失败才是信号，
 * 成功行保持最小信息量、失败行才带完整诊断。enter 暂存 + exit 查回的
 * 关联机制与 07 同源（key = 64 位 pid_tgid），不再另造轮子。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "opensnoop_mf_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct inflight_value {
	char comm[16];
	char filename[FILENAME_LEN];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/* key: pid_tgid（64 位 = tgid<<32 | tid，避免多线程同进程冲突）——与 07 同一约定 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct inflight_value);
} inflight SEC(".maps");

SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(opensnoop_mf_enter, int dfd, const char *filename,
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

SEC("kretprobe/do_sys_openat2")
int BPF_KRETPROBE(opensnoop_mf_exit, long ret)
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
		e->failed = ret < 0;	/* 0: 成功 minimal；1: 失败带 -errno */
		__builtin_memcpy(&e->comm, v->comm, sizeof(e->comm));
		__builtin_memcpy(&e->filename, v->filename, sizeof(e->filename));
		bpf_ringbuf_submit(e, 0);
	}

	/* 临时状态必须清理（旧 filename 串位 / map 泄漏）；
	 * 即使 ringbuf 满、事件没发出去，也要先删再返回。 */
	bpf_map_delete_elem(&inflight, &pid_tgid);
	return 0;
}

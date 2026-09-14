// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 06-open-file-perf-event
 * （对应 ebpf.top BPF 入门系列实践教程六：捕获进程打开文件信息 perf_event 篇）。
 *
 * 挂载点：kprobe/do_sys_openat2（与 05 相同）
 * 输出：BPF_MAP_TYPE_PERF_EVENT_ARRAY + bpf_perf_event_output
 *
 * 本示例与 05-open-file-kprobe 唯一的区别是输出机制：
 * ring buffer 换成 perf buffer。事件结构、挂载点、过滤逻辑全部一致，
 * 方便读者逐行 diff 两个目录，直观理解两种事件通道的差异。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "opensnoop_perf_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} events SEC(".maps");

SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(opensnoop_entry, int dfd, const char *filename,
	       struct open_how *how)
{
	struct event e = {};

	e.pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(&e.comm, sizeof(e.comm));
	bpf_probe_read_user_str(&e.filename, sizeof(e.filename),
				(const void *)filename);

	/* 写入“当前 CPU”对应的 perf buffer；BPF_F_CURRENT_CPU 由内核
	 * 解析为当前 CPU id，作为 events map 的 key。 */
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
			      &e, sizeof(e));
	return 0;
}

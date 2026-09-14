// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 05-open-file-kprobe
 * （对应 ebpf.top BPF 入门系列实践教程五：捕获进程打开文件信息）。
 *
 * 挂载点：kprobe/do_sys_openat2
 * 输出：ring buffer
 *
 * 这是第一个 opensnoop 类工具：用 kprobe 动态追踪内核函数，
 * 从函数参数中拿到用户态 filename 指针并复制字符串。
 *
 * do_sys_openat2 的原型（fs/open.c，5.6+）：
 *   long do_sys_openat2(int dfd, const char __user *filename,
 *                       struct open_how *how);
 * 第二个参数是用户态地址，必须用 bpf_probe_read_user_str() 读取。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "opensnoop_kprobe_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/*
 * BPF_KPROBE 宏按位置把原函数参数绑定到同名形参：
 *   BPF_KPROBE(opensnoop_entry, int dfd, const char *filename, struct open_how *how)
 * 等价于（不使用宏时）：
 *   int opensnoop_entry(struct pt_regs *ctx)
 *   {
 *       const char *filename = (void *)PT_REGS_PARM2(ctx);
 *       ...
 *   }
 */
SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(opensnoop_entry, int dfd, const char *filename,
	       struct open_how *how)
{
	struct event *e;

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	e->pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(&e->comm, sizeof(e->comm));

	/* filename 是用户态地址：读取用户内存，可能失败/截断 */
	bpf_probe_read_user_str(&e->filename, sizeof(e->filename),
				(const void *)filename);

	bpf_ringbuf_submit(e, 0);
	return 0;
}

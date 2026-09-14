// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 01-hello-world（对应 ebpf.top BPF 入门系列实践教程一：Hello World）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_execve
 * 输出：bpf_printk -> /sys/kernel/tracing/trace_pipe
 *
 * 与第 1 章的 skeleton 示例相比，本例刻意保持最小：
 * 只包含 license 声明和一个 tracepoint 程序，用于验证工具链闭环。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

SEC("tracepoint/syscalls/sys_enter_execve")
int hello(void *ctx)
{
	bpf_printk("Hello, eBPF world!\n");
	return 0;
}

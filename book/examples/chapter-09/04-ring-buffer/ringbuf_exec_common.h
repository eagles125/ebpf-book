/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 第 9 章示例 04-ring-buffer 的共享事件定义。
 *
 * 同一份结构体同时被内核态（.bpf.c）和用户态（.c）引用，
 * 保证两侧的事件布局一致——这是所有 ring buffer / perf buffer
 * 示例的基本纪律：两侧各自定义一份略有差异的结构，是最常见的
 * “输出乱码/串位”根因。
 *
 * 注意：__u32/__u64 等类型由包含方提供——
 * BPF 侧来自 "vmlinux.h"，用户态侧来自 <linux/types.h>。
 */

#pragma once

struct event {
	__u32 pid;
	__u64 cgroup_id;
	char comm[16];
};

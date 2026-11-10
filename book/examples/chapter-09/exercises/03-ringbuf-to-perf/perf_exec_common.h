/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 第 9 章练习 03-ringbuf-to-perf 的共享事件定义。
 *
 * 与 04-ring-buffer 的 ringbuf_exec_common.h 逐字段一致：
 * 迁移练习的纪律就是「只换通道、不换事件」——两侧 diff 里事件结构
 * 不允许出现差异。
 *
 * 类型由包含方提供：BPF 侧来自 "vmlinux.h"，用户态侧来自 <linux/types.h>。
 */

#pragma once

struct event {
	__u32 pid;
	__u64 cgroup_id;
	char comm[16];
};

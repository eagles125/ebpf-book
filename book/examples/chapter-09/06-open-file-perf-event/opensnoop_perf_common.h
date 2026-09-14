/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 第 9 章示例 06-open-file-perf-event 的共享事件定义。
 *
 * 与 05-open-file-kprobe 的事件保持一致，便于对照两个输出机制。
 * 类型由包含方提供：BPF 侧来自 "vmlinux.h"，用户态侧来自 <linux/types.h>。
 */

#pragma once

#define FILENAME_LEN 128

struct event {
	__u32 pid;
	char comm[16];
	char filename[FILENAME_LEN];
};

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 *
 * 第 9 章示例 08-open-file-tracepoint 的共享事件定义。
 *
 * 与 07-open-file-return 的事件保持一致，便于对照
 * “kprobe/kretprobe 关联” 与 “tracepoint enter/exit 关联” 两种实现。
 * 类型由包含方提供：BPF 侧来自 "vmlinux.h"，用户态侧来自 <linux/types.h>。
 */

#pragma once

#define FILENAME_LEN 128

struct event {
	__u32 pid;
	char comm[16];
	char filename[FILENAME_LEN];
	long ret;	/* >= 0: fd；< 0: -errno */
};

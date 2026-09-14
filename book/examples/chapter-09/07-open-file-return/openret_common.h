/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 第 9 章示例 07-open-file-return 的共享定义。
 *
 * 事件结构：在 05 的基础上增加 ret 字段（open 的返回值）。
 * 临时状态 inflight_value 只在内核态使用，不放在共享头里。
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

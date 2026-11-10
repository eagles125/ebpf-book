/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 第 9 章练习 02-minimal-failed 的共享事件定义。
 *
 * 在 05-open-file-kprobe 的事件结构上增加两个字段，构成「两级事件」：
 *   failed —— 0：成功路径，事件保持 minimal 信息（pid/comm/filename）；
 *             1：失败路径，ret 为 -errno。
 *   ret    —— open 的返回值：>= 0 是 fd（minimal 路径不打印它）；
 *             < 0 是 -errno（失败路径由用户态映射成 errno 名）。
 *
 * 布局：__u32×2 + char[16] + char[128] + long = 160 字节，天然 8 字节对齐，
 * 满足 ring buffer 记录的对齐硬性要求。
 * 类型由包含方提供：BPF 侧来自 "vmlinux.h"，用户态侧来自 <linux/types.h>。
 */

#pragma once

#define FILENAME_LEN 128

struct event {
	__u32 pid;
	__u32 failed;		/* 0/1：成功路径 minimal / 失败路径带 -errno */
	char comm[16];
	char filename[FILENAME_LEN];
	long ret;		/* >= 0: fd；< 0: -errno（failed=1 时才有意义） */
};

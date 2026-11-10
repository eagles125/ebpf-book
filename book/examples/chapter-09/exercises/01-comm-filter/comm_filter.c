// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 01-comm-filter 的用户态 loader。
 *
 * 用法：sudo ./comm_filter -c <comm>
 *   -c <comm>  只输出该进程名的事件；不带 -c 时输出所有进程（map 写空串）。
 *
 * 与 02-pid-filter 的 loader 同构：用户态只写 map，不参与事件处理；
 * 内核态每次触发都重新读取 map，因此运行中更新过滤条件立即生效。
 * 与基线的差别只有一处——写进 map 的值从 4 字节 PID 换成 16 字节进程名。
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "comm_filter.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

int main(int argc, char **argv)
{
	struct comm_filter_bpf *skel;
	char target[16] = {};	/* 全零 = 空串 = 不过滤 */
	__u32 key = 0;
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
		case 'c':
			/* strncpy 只在源串较短时补零——手动补齐 NUL，
			 * 保证 16 字节尾部永远是零（与内核侧约定成对） */
			strncpy(target, optarg, sizeof(target) - 1);
			target[sizeof(target) - 1] = '\0';
			break;
		default:
			fprintf(stderr, "用法: %s [-c <comm>]\n", argv[0]);
			return 1;
		}
	}

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = comm_filter_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (comm_filter_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		comm_filter_bpf__destroy(skel);
		return 1;
	}

	/* 把目标进程名写入 ARRAY map 的 key 0；空串表示不过滤 */
	if (bpf_map_update_elem(bpf_map__fd(skel->maps.target_comm),
				 &key, target, BPF_ANY)) {
		fprintf(stderr, "更新 target_comm map 失败\n");
		comm_filter_bpf__destroy(skel);
		return 1;
	}

	printf("comm 过滤已生效：%s，按 Ctrl-C 退出。\n",
	       target[0] ? "仅输出目标进程名的事件"
			 : "输出所有进程的事件（未指定 -c）");
	printf("查看输出：sudo cat /sys/kernel/tracing/trace_pipe\n");

	while (!exiting)
		sleep(1);

	comm_filter_bpf__destroy(skel);
	return 0;
}

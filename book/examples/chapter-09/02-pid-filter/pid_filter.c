// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 02-pid-filter 的用户态 loader。
 *
 * 用法：sudo ./pid_filter -p <pid>
 *   -p <pid>  只输出该 PID 的事件；不带 -p 时输出所有进程（值写 0）。
 *
 * 演示“控制面/数据面”雏形：用户态只写 map，不参与事件处理；
 * 内核态每次触发都重新读取 map，因此运行中更新 map 立即生效。
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "pid_filter.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

int main(int argc, char **argv)
{
	struct pid_filter_bpf *skel;
	__u32 pid = 0, key = 0;
	int opt;

	while ((opt = getopt(argc, argv, "p:h")) != -1) {
		switch (opt) {
		case 'p':
			pid = (__u32)atoi(optarg);
			break;
		default:
			fprintf(stderr, "用法: %s [-p <pid>]\n", argv[0]);
			return 1;
		}
	}

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = pid_filter_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (pid_filter_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		pid_filter_bpf__destroy(skel);
		return 1;
	}

	/* 把目标 PID 写入 ARRAY map 的 key 0；0 表示不过滤 */
	if (bpf_map_update_elem(bpf_map__fd(skel->maps.target_pid),
				 &key, &pid, BPF_ANY)) {
		fprintf(stderr, "更新 target_pid map 失败\n");
		pid_filter_bpf__destroy(skel);
		return 1;
	}

	printf("PID 过滤已生效：%s，按 Ctrl-C 退出。\n",
	       pid ? "仅输出目标 PID 的事件" : "输出所有进程的事件（未指定 -p）");
	printf("查看输出：sudo cat /sys/kernel/tracing/trace_pipe\n");

	while (!exiting)
		sleep(1);

	pid_filter_bpf__destroy(skel);
	return 0;
}

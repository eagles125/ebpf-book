// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 07-open-file-return 的用户态 loader。
 *
 * 用法：sudo ./openret
 *
 * 打印 openat 的返回值：>= 0 是文件描述符 fd；
 * < 0 是 -errno（如 -2 = ENOENT，文件不存在）。
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include "openret_common.h"
#include "openret.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;

	if (e->ret >= 0)
		printf("%-8u %-16s fd=%-4ld %s\n",
		       e->pid, e->comm, e->ret, e->filename);
	else
		printf("%-8u %-16s ERR=%-4ld(%s) %s\n",
		       e->pid, e->comm, e->ret,
		       e->ret == -2 ? "ENOENT" : "errno", e->filename);
	return 0;
}

int main(void)
{
	struct openret_bpf *skel;
	struct ring_buffer *ring;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = openret_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (openret_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		openret_bpf__destroy(skel);
		return 1;
	}

	ring = ring_buffer__new(bpf_map__fd(skel->maps.events),
				handle_event, NULL, NULL);
	if (!ring) {
		fprintf(stderr, "创建 ring buffer 失败\n");
		openret_bpf__destroy(skel);
		return 1;
	}

	printf("%-8s %-16s %-10s %s\n", "PID", "COMM", "RET", "FILE");
	printf("open 返回值跟踪已就绪，按 Ctrl-C 退出。\n");

	while (!exiting) {
		if (ring_buffer__poll(ring, 100) == -EINTR) {
			exiting = 1;
			break;
		}
	}

	ring_buffer__free(ring);
	openret_bpf__destroy(skel);
	return 0;
}

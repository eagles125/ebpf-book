// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 04-ring-buffer 的用户态 loader。
 *
 * 用法：sudo ./ringbuf_exec
 *
 * 演示 ring buffer 的用户态消费方式：
 *   ring_buffer__new() 绑定回调 -> ring_buffer__poll() 循环消费。
 * poll 超时不是错误，只表示这段时间没有事件。
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include "ringbuf_exec_common.h"
#include "ringbuf_exec.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

/* 事件回调：data 指向内核态写入的 struct event */
static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;

	printf("%-8u %-20llu %s\n", e->pid, e->cgroup_id, e->comm);
	return 0;
}

int main(void)
{
	struct ringbuf_exec_bpf *skel;
	struct ring_buffer *ring;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = ringbuf_exec_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (ringbuf_exec_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		ringbuf_exec_bpf__destroy(skel);
		return 1;
	}

	/* 绑定 ring buffer 与事件回调 */
	ring = ring_buffer__new(bpf_map__fd(skel->maps.events),
				handle_event, NULL, NULL);
	if (!ring) {
		fprintf(stderr, "创建 ring buffer 失败\n");
		ringbuf_exec_bpf__destroy(skel);
		return 1;
	}

	printf("%-8s %-20s %s\n", "PID", "CGROUP_ID", "COMM");
	printf("ring buffer 已就绪，按 Ctrl-C 退出。\n");

	while (!exiting) {
		/* 100ms 超时：无事件时返回 0，负值才是错误 */
		if (ring_buffer__poll(ring, 100) == -EINTR) {
			exiting = 1;
			break;
		}
	}

	ring_buffer__free(ring);
	ringbuf_exec_bpf__destroy(skel);
	return 0;
}

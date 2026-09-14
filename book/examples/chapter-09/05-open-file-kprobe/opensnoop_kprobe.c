// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 05-open-file-kprobe 的用户态 loader。
 *
 * 用法：sudo ./opensnoop_kprobe
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include "opensnoop_kprobe_common.h"
#include "opensnoop_kprobe.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;

	printf("%-8u %-16s %s\n", e->pid, e->comm, e->filename);
	return 0;
}

int main(void)
{
	struct opensnoop_kprobe_bpf *skel;
	struct ring_buffer *ring;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = opensnoop_kprobe_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (opensnoop_kprobe_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败（do_sys_openat2 是否存在于当前内核？）\n");
		opensnoop_kprobe_bpf__destroy(skel);
		return 1;
	}

	ring = ring_buffer__new(bpf_map__fd(skel->maps.events),
				handle_event, NULL, NULL);
	if (!ring) {
		fprintf(stderr, "创建 ring buffer 失败\n");
		opensnoop_kprobe_bpf__destroy(skel);
		return 1;
	}

	printf("%-8s %-16s %s\n", "PID", "COMM", "FILE");
	printf("kprobe opensnoop 已就绪，按 Ctrl-C 退出。\n");

	while (!exiting) {
		if (ring_buffer__poll(ring, 100) == -EINTR) {
			exiting = 1;
			break;
		}
	}

	ring_buffer__free(ring);
	opensnoop_kprobe_bpf__destroy(skel);
	return 0;
}

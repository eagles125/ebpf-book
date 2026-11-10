// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 03-ringbuf-to-perf 的用户态 loader。
 *
 * 用法：sudo ./perf_exec
 *
 * 与 04-ring-buffer 的 loader 逐行对照：ring_buffer__new/ring_buffer__poll
 * 换成 perf_buffer__new/perf_buffer__poll，并新增丢失回调——
 * perf buffer 按 CPU 建立通道，回调可能并发触发，且必须显式处理
 * lost samples（丢事件回调）。事件行的打印格式与 04 完全一致。
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include "perf_exec_common.h"
#include "perf_exec.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

/* 事件回调：多 CPU 环境下可能被不同 CPU 的 poll 并发调用 */
static void handle_event(void *ctx, int cpu, void *data, __u32 data_sz)
{
	const struct event *e = data;

	printf("%-8u %-20llu %s\n", e->pid, e->cgroup_id, e->comm);
}

/* 丢失回调：该 CPU 的 perf buffer 满了，发生了丢样 */
static void handle_lost_events(void *ctx, int cpu, __u64 lost_cnt)
{
	fprintf(stderr, "警告：CPU %d 丢失 %llu 个事件（perf buffer 满）\n",
		cpu, lost_cnt);
}

int main(void)
{
	struct perf_exec_bpf *skel;
	struct perf_buffer *pb;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = perf_exec_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (perf_exec_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		perf_exec_bpf__destroy(skel);
		return 1;
	}

	/* 8 页/每 CPU 的缓冲；事件回调 + 丢失回调 */
	pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 8,
			      handle_event, handle_lost_events, NULL, NULL);
	if (!pb) {
		fprintf(stderr, "创建 perf buffer 失败\n");
		perf_exec_bpf__destroy(skel);
		return 1;
	}

	printf("%-8s %-20s %s\n", "PID", "CGROUP_ID", "COMM");
	printf("perf buffer execve 事件流已就绪，按 Ctrl-C 退出。\n");

	while (!exiting) {
		if (perf_buffer__poll(pb, 100) == -EINTR) {
			exiting = 1;
			break;
		}
	}

	perf_buffer__free(pb);
	perf_exec_bpf__destroy(skel);
	return 0;
}

/* perf.c —— 03-event-output 的 perf buffer 变体(用户态)
 *
 * 职责:open→load→attach→perf_buffer__new(按 CPU 数开环)→
 * perf_buffer__poll 循环收事件;丢失事件走 on_lost 回调(可观测)。
 * 验证状态:编译级已验证;运行需 root(预期输出示例 / 运行级待验证)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "perf_bpf.skel.h"

static volatile sig_atomic_t exiting;

static void on_sigint(int sig)
{
	(void)sig;
	exiting = 1;
}

struct exec_event {
	__u32 pid;
	char comm[16];
};

static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	const struct exec_event *e = data;

	(void)ctx;	/* 本例不需要上下文 */
	(void)size;	/* 恒为 sizeof(struct exec_event) */
	printf("[perf]    pid=%-6d comm=%s (cpu%d)\n", e->pid, e->comm, cpu);
}

static void on_lost(void *ctx, int cpu, __u64 cnt)
{
	(void)ctx;	/* 本例不需要上下文 */
	fprintf(stderr, "[perf]    cpu%d 丢失 %llu 个事件(环满)\n",
		cpu, (unsigned long long)cnt);
}

int main(void)
{
	struct perf_bpf *skel;
	struct perf_buffer *pb = NULL;
	struct bpf_link *link = NULL;
	int err, map_fd;

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	skel = perf_bpf__open();
	if (!skel) {
		fprintf(stderr, "FAIL skeleton open\n");
		return 1;
	}
	err = perf_bpf__load(skel);
	if (err) {
		fprintf(stderr, "FAIL skeleton load(err=%d):需 root(运行级待验证)\n", err);
		perf_bpf__destroy(skel);
		return 1;
	}
	err = perf_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "FAIL attach(err=%d)\n", err);
		goto cleanup;
	}

	/* 每 CPU 开 8 页环形缓冲;lost 回调使丢失可观测 */
	map_fd = bpf_map__fd(skel->maps.events);
	pb = perf_buffer__new(map_fd, 8, on_sample, on_lost, NULL, NULL);
	if (!pb) {
		fprintf(stderr, "FAIL perf_buffer__new(需 root:perf_event_open 档位)\n");
		goto cleanup;
	}

	signal(SIGINT, on_sigint);
	printf("[perf]    监听 execve,等待事件(Ctrl-C 退出)…\n");
	while (!exiting) {
		err = perf_buffer__poll(pb, 100);
		if (err == -EINTR)
			break;
		if (err < 0) {
			fprintf(stderr, "FAIL perf_buffer__poll(err=%d)\n", err);
			break;
		}
	}

	printf("清理…\n");
cleanup:
	bpf_link__destroy(link);
	perf_buffer__free(pb);
	perf_bpf__destroy(skel);
	return 0;
}

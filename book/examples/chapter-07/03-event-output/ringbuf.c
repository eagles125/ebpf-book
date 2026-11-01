/* ringbuf.c —— 03-event-output 的 ringbuf 变体(用户态)
 *
 * 职责:open→load→attach(tracepoint)→ring_buffer__new 创建轮询器→
 * ring_buffer__poll 循环收事件打印。验证状态:编译级已验证;运行需 root
 * (预期输出示例 / 运行级待验证)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "ringbuf_bpf.skel.h"

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

/* ring_buffer__poll 回调:每个已提交事件一次(data 指向事件本体) */
static int on_event(void *ctx, void *data, size_t len)
{
	const struct exec_event *e = data;

	(void)ctx;	/* 本例不需要上下文 */
	(void)len;	/* 长度恒为 sizeof(struct exec_event) */
	printf("[ringbuf] pid=%-6d comm=%s\n", e->pid, e->comm);
	return 0;
}

int main(void)
{
	struct ringbuf_bpf *skel;
	struct ring_buffer *rb = NULL;
	struct bpf_link *link = NULL;
	int err, map_fd;

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	skel = ringbuf_bpf__open();
	if (!skel) {
		fprintf(stderr, "FAIL skeleton open\n");
		return 1;
	}
	err = ringbuf_bpf__load(skel);
	if (err) {
		fprintf(stderr, "FAIL skeleton load(err=%d):需 root(运行级待验证)\n", err);
		ringbuf_bpf__destroy(skel);
		return 1;
	}
	err = ringbuf_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "FAIL attach(err=%d):tracepoint 挂载失败\n", err);
		goto cleanup;
	}

	map_fd = bpf_map__fd(skel->maps.events);
	rb = ring_buffer__new(map_fd, on_event, NULL, NULL);
	if (!rb) {
		fprintf(stderr, "FAIL ring_buffer__new\n");
		goto cleanup;
	}

	signal(SIGINT, on_sigint);
	printf("[ringbuf] 监听 execve,等待事件(Ctrl-C 退出)…\n");
	while (!exiting) {
		/* 超时 100ms 轮询;负数=出错 */
		err = ring_buffer__poll(rb, 100);
		if (err == -EINTR)
			break;
		if (err < 0) {
			fprintf(stderr, "FAIL ring_buffer__poll(err=%d)\n", err);
			break;
		}
	}

	printf("清理…\n");
cleanup:
	bpf_link__destroy(link);
	ring_buffer__free(rb);
	ringbuf_bpf__destroy(skel);
	return 0;
}

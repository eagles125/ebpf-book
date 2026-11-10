// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 02-minimal-failed 的用户态 loader。
 *
 * 用法：sudo ./opensnoop_mf
 *
 * 两级输出：failed=0 的成功行按 05 同形的 minimal 三列打印；
 * failed=1 的失败行在行尾追加 ret 与 errno 名。-errno 到 errno 名的
 * 映射在用户态完成（正文 §10 检查点 2 所说「完整工具」的做法）。
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include "opensnoop_mf_common.h"
#include "opensnoop_mf.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

/* 常见 open 错误码 -> errno 名；表外的错误码回退为 "errno"。
 * 完整工具可再接 strerror_r() 输出错误描述串。 */
static const char *errno_name(int err)
{
	switch (err) {
	case EPERM:		return "EPERM";		/* Operation not permitted */
	case ENOENT:		return "ENOENT";	/* No such file or directory */
	case EACCES:		return "EACCES";	/* Permission denied */
	case EISDIR:		return "EISDIR";	/* Is a directory */
	case ENOTDIR:		return "ENOTDIR";	/* Not a directory */
	case ENFILE:		return "ENFILE";	/* Too many open files in system */
	case EMFILE:		return "EMFILE";	/* Too many open files */
	case ENOSPC:		return "ENOSPC";	/* No space left on device */
	case EROFS:		return "EROFS";		/* Read-only file system */
	case ELOOP:		return "ELOOP";		/* Too many levels of symbolic links */
	case ENAMETOOLONG:	return "ENAMETOOLONG";	/* File name too long */
	default:		return "errno";
	}
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;

	if (e->failed)
		printf("%-8u %-16s %s (ret=%ld %s)\n",
		       e->pid, e->comm, e->filename,
		       e->ret, errno_name((int)-e->ret));
	else
		printf("%-8u %-16s %s\n", e->pid, e->comm, e->filename);
	return 0;
}

int main(void)
{
	struct opensnoop_mf_bpf *skel;
	struct ring_buffer *ring;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = opensnoop_mf_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (opensnoop_mf_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败（do_sys_openat2 是否存在于当前内核？）\n");
		opensnoop_mf_bpf__destroy(skel);
		return 1;
	}

	ring = ring_buffer__new(bpf_map__fd(skel->maps.events),
				handle_event, NULL, NULL);
	if (!ring) {
		fprintf(stderr, "创建 ring buffer 失败\n");
		opensnoop_mf_bpf__destroy(skel);
		return 1;
	}

	printf("%-8s %-16s %s\n", "PID", "COMM", "FILE");
	printf("两级 opensnoop 已就绪，按 Ctrl-C 退出。\n");

	while (!exiting) {
		if (ring_buffer__poll(ring, 100) == -EINTR) {
			exiting = 1;
			break;
		}
	}

	ring_buffer__free(ring);
	opensnoop_mf_bpf__destroy(skel);
	return 0;
}

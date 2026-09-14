// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 01-hello-world 的用户态 loader。
 *
 * 职责：打开并加载 BPF 对象 -> 挂载所有自动 attach 的程序 -> 等待退出信号。
 * BPF 程序的输出去往内核 tracing buffer（trace_pipe），不在本进程 stdout。
 *
 * 说明：skeleton 头文件 hello.skel.h 由 bpftool 从 hello.bpf.o 生成，
 * 其中对象名默认为 hello_bpf，因此 API 形如 hello_bpf__open_and_load()。
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "hello.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

int main(void)
{
	struct hello_bpf *skel;

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	/* 打开 + verifier 校验 + 加载，失败返回 NULL */
	skel = hello_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败（常见原因：权限不足、内核不支持）\n");
		return 1;
	}

	/* 挂载所有可从 SEC 名推断 attach 点的程序 */
	if (hello_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		hello_bpf__destroy(skel);
		return 1;
	}

	printf("Hello World 已加载，按 Ctrl-C 退出。\n");
	printf("在另一个终端查看输出：\n");
	printf("  sudo cat /sys/kernel/tracing/trace_pipe\n");
	printf("触发方式：执行任意命令（如 /bin/true、ls、echo test）\n");

	while (!exiting)
		sleep(1);

	hello_bpf__destroy(skel);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 03-container-security 的用户态 loader。
 *
 * 用法：sudo ./container_guard -c <cgroup-id>
 *
 * cgroup id 的获取（cgroup v2）：
 *   stat -c %i /sys/fs/cgroup/<容器 cgroup 路径>
 * 例如对某个容器：
 *   stat -c %i /sys/fs/cgroup/system.slice/docker-<容器长 ID>.scope
 * bpf_get_current_cgroup_id() 返回的就是该 cgroup 的 inode 号（cgroup v2）。
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "container_guard.skel.h"

static volatile sig_atomic_t exiting = 0;

static void handle_sig(int sig)
{
	exiting = 1;
}

int main(int argc, char **argv)
{
	struct container_guard_bpf *skel;
	__u64 cgid = 0;
	__u32 policy = 0;	/* POLICY_LOG */
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
		case 'c':
			cgid = strtoull(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, "用法: %s -c <cgroup-id>\n", argv[0]);
			return 1;
		}
	}

	if (!cgid) {
		fprintf(stderr, "必须用 -c 指定目标 cgroup id（获取方式见源码注释或 README）\n");
		return 1;
	}

	signal(SIGINT, handle_sig);
	signal(SIGTERM, handle_sig);

	skel = container_guard_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "加载或校验 BPF 程序失败\n");
		return 1;
	}

	if (container_guard_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败\n");
		container_guard_bpf__destroy(skel);
		return 1;
	}

	/* 注册目标容器：cgroup id -> 策略（本示例固定 POLICY_LOG） */
	if (bpf_map_update_elem(bpf_map__fd(skel->maps.target_cgroups),
				 &cgid, &policy, BPF_ANY)) {
		fprintf(stderr, "更新 target_cgroups map 失败\n");
		container_guard_bpf__destroy(skel);
		return 1;
	}

	printf("容器安全观测已启动：cgroup id %llu（仅记录，不阻断）\n", cgid);
	printf("查看输出：sudo cat /sys/kernel/tracing/trace_pipe\n");

	while (!exiting)
		sleep(1);

	container_guard_bpf__destroy(skel);
	return 0;
}

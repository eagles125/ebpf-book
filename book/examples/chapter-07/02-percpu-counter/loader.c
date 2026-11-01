/* loader.c —— 第 7 章 02-percpu-counter 的用户态加载器(skeleton 模式)
 *
 * 职责:open→load→attach(XDP on lo)→每秒读 per-CPU 映射并聚合:
 * bpf_map_lookup_elem 对 per-CPU 映射一次性返回「全部 CPU 的值槽」
 * (num_possible_cpus × value_size),loader 逐 CPU 打印并求和。
 * 教学要点:per-CPU 的代价与回报——BPF 侧零锁,用户态必须自己做聚合;
 * 聚合数字是「读出时刻的近似快照」,逐 CPU 读数之间还会变(正文三节)。
 *
 * 验证状态:本文件随 Makefile 编译(编译级已验证);运行需 root
 * (预期输出示例 / 运行级待验证)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <net/if.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "xdp_percpu_bpf.skel.h"

static volatile sig_atomic_t exiting;

static void on_sigint(int sig)
{
	(void)sig;	/* 信号处理只置位,不使用信号编号 */
	exiting = 1;
}

int main(int argc, char **argv)
{
	const char *ifname = argc > 1 ? argv[1] : "lo";
	struct xdp_percpu_bpf *skel;
	struct bpf_link *link = NULL;
	int ifindex, err, map_fd, ncpu, i;
	__u32 key = 0;
	/* per-CPU 读取缓冲:大小 = CPU 数 × value_size,由 libbpf 按需取 */
	__u64 *values;

	ncpu = libbpf_num_possible_cpus();
	if (ncpu < 1) {
		fprintf(stderr, "FAIL libbpf_num_possible_cpus=%d\n", ncpu);
		return 1;
	}
	values = calloc(ncpu, sizeof(*values));
	if (!values) {
		fprintf(stderr, "FAIL 内存分配\n");
		return 1;
	}

	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "FAIL 接口 %s 不存在(XDP 只挂 lo 等受控接口,严禁物理网卡)\n", ifname);
		free(values);
		return 1;
	}

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	skel = xdp_percpu_bpf__open();
	if (!skel) {
		fprintf(stderr, "FAIL skeleton open\n");
		free(values);
		return 1;
	}
	err = xdp_percpu_bpf__load(skel);
	if (err) {
		fprintf(stderr, "FAIL skeleton load(err=%d):需 root 与 CAP_BPF/CAP_NET_ADMIN(运行级待验证)\n", err);
		xdp_percpu_bpf__destroy(skel);
		free(values);
		return 1;
	}
	link = bpf_program__attach_xdp(skel->progs.percpu_count, ifindex);
	if (!link) {
		fprintf(stderr, "FAIL attach_xdp(%s):需 root;XDP 只允许挂 lo 等受控接口\n", ifname);
		xdp_percpu_bpf__destroy(skel);
		free(values);
		return 1;
	}

	signal(SIGINT, on_sigint);
	printf("已挂 XDP 到 %s,每秒聚合一次 pkt_count(%d 个 CPU 槽,Ctrl-C 退出)…\n",
	       ifname, ncpu);

	map_fd = bpf_map__fd(skel->maps.pkt_count);
	while (!exiting) {
		sleep(1);
		/* 一次性读出全部 CPU 槽:per-CPU 映射的用户态读取口径 */
		if (bpf_map_lookup_elem(map_fd, &key, values)) {
			perror("bpf_map_lookup_elem");
			continue;
		}
		__u64 total = 0;
		printf("--- per-CPU 计数(读出快照) ---\n");
		for (i = 0; i < ncpu; i++) {
			if (values[i])
				printf("  cpu%-2d: %llu\n", i, (unsigned long long)values[i]);
			total += values[i];
		}
		printf("  合计: %llu 包\n", (unsigned long long)total);
	}

	printf("清理:detach + destroy…\n");
	bpf_link__destroy(link);
	xdp_percpu_bpf__destroy(skel);
	free(values);
	return 0;
}

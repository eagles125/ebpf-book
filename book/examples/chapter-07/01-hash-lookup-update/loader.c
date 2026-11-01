/* loader.c —— 第 7 章 01-hash-lookup-update 的用户态加载器(skeleton 模式)
 *
 * 职责:open→load→attach(XDP on lo 或指定口)→每秒走读映射
 * (bpf_map_get_next_key + bpf_map_lookup_elem)→Ctrl-C 清理。
 * 教学要点:hash 映射的「用户态读」视角——逐 key 遍历,与 BPF 侧的
 * lookup/update(NOEXIST) 成对照;遍历顺序无保证(hash 语义,正文二节)。
 *
 * 验证状态:本文件随 Makefile 编译(编译级已验证);运行需 root
 * (预期输出示例 / 运行级待验证)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>	/* bpf_map_get_next_key / bpf_map_lookup_elem(syscall 包装) */

#include "xdp_hash_bpf.skel.h"

static volatile sig_atomic_t exiting;

static void on_sigint(int sig)
{
	(void)sig;	/* 信号处理只置位,不使用信号编号 */
	exiting = 1;
}

/* 把网络字节序的以太类型翻成人话,遍历打印用 */
static const char *eth_type_name(__be16 proto)
{
	switch (be16toh(proto)) {
	case ETH_P_IP:	return "IPv4";
	case ETH_P_IPV6: return "IPv6";
	case ETH_P_ARP:	return "ARP";
	default:	return "其他";
	}
}

int main(int argc, char **argv)
{
	const char *ifname = argc > 1 ? argv[1] : "lo";
	struct xdp_hash_bpf *skel;
	struct bpf_link *link = NULL;
	int ifindex, err, map_fd;
	__be16 key, next_key;
	__u64 value;

	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "FAIL 接口 %s 不存在(XDP 只挂 lo 等受控接口,严禁物理网卡)\n", ifname);
		return 1;
	}

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	skel = xdp_hash_bpf__open();
	if (!skel) {
		fprintf(stderr, "FAIL skeleton open(检查 .skel.h 与 libbpf 版本)\n");
		return 1;
	}
	err = xdp_hash_bpf__load(skel);
	if (err) {
		fprintf(stderr, "FAIL skeleton load(err=%d):需 root 与 CAP_BPF/CAP_NET_ADMIN(运行级待验证)\n", err);
		xdp_hash_bpf__destroy(skel);
		return 1;
	}
	link = bpf_program__attach_xdp(skel->progs.hash_count, ifindex);
	if (!link) {
		fprintf(stderr, "FAIL attach_xdp(%s):需 root;XDP 只允许挂 lo 等受控接口\n", ifname);
		xdp_hash_bpf__destroy(skel);
		return 1;
	}

	signal(SIGINT, on_sigint);
	printf("已挂 XDP 到 %s,每秒读一次 proto_count(Ctrl-C 退出)…\n", ifname);

	map_fd = bpf_map__fd(skel->maps.proto_count);
	while (!exiting) {
		sleep(1);
		printf("--- hash 映射遍历(顺序无保证) ---\n");
		/* bpf_map_get_next_key(key=NULL) 取第一个键,此后逐键前进:
		 * hash 语义下遍历顺序不保证,这是与 array 的第一个差别。 */
		key = 0;
		while (!bpf_map_get_next_key(map_fd, &key, &next_key)) {
			if (!bpf_map_lookup_elem(map_fd, &next_key, &value))
				printf("  %-10s (0x%04x): %llu 包\n",
				       eth_type_name(next_key),
				       be16toh(next_key), (unsigned long long)value);
			key = next_key;
		}
	}

	printf("清理:detach + destroy…\n");
	bpf_link__destroy(link);
	xdp_hash_bpf__destroy(skel);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 09-netfilter-go 的数据面
 * （对应 ebpf.top BPF 入门系列实践教程九：在 Netfilter 中使用 eBPF 以及 Go 经验总结）。
 *
 * 程序类型：BPF_PROG_TYPE_NETFILTER（需要内核 6.6+），SEC("netfilter")
 * 功能：按“目标端口黑名单”丢弃本机出方向的 TCP/UDP 报文，并维护
 *       per-CPU 的 pass/drop 计数；规则由 Go 控制面（cmd/nfctl）写入。
 *
 * 与 XDP/TC 的分工（第 15-17 章展开）：XDP 在驱动收包早期，
 * TC 在 qdisc，Netfilter 位于传统防火墙路径——本示例用它把
 * eBPF 与经典防火墙概念（hook/优先级/返回动作）连接起来。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* vmlinux.h 来自 BTF，只含类型和枚举，不含宏；NF_DROP/NF_ACCEPT 与
 * IPPROTO_* 是 UAPI 宏，需在此手工定义（取值与 uapi 一致）。 */
#ifndef NF_DROP
#define NF_DROP 0
#endif
#ifndef NF_ACCEPT
#define NF_ACCEPT 1
#endif
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define ACTION_DROP 1

/* 目标端口黑名单。key: 端口（主机字节序）；value: 动作。
 * pinning 让 map 固定在 bpffs（/sys/fs/bpf/nf_guard_rules），
 * Go 控制面进程退出后规则仍在，下次 run 复用。 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
	__type(key, __u16);
	__type(value, __u8);
} nf_guard_rules SEC(".maps");

/* per-CPU 计数：key 0 = pass，key 1 = drop */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 2);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
	__type(key, __u32);
	__type(value, __u64);
} nf_guard_stats SEC(".maps");

static __always_inline void bump(__u32 idx)
{
	__u64 *cnt = bpf_map_lookup_elem(&nf_guard_stats, &idx);

	if (cnt)
		*cnt += 1;
}

SEC("netfilter")
int nf_guard(struct bpf_nf_ctx *ctx)
{
	struct sk_buff *skb = ctx->skb;
	struct iphdr ip = {};
	struct tcphdr tcp = {};
	struct udphdr udp = {};
	__u16 dport = 0;
	__u8 *action;

	/* 从网络层头开始读取 IP 头；读取失败或非 IPv4 直接放行 */
	if (bpf_skb_load_bytes_relative(skb, 0, &ip, sizeof(ip),
					BPF_HDR_START_NET) < 0) {
		bump(0);
		return NF_ACCEPT;
	}
	if (ip.version != 4 || ip.ihl != 5) {
		/* 简化处理：跳过带 IP options 的包（ihl != 5） */
		bump(0);
		return NF_ACCEPT;
	}

	if (ip.protocol == IPPROTO_TCP) {
		if (bpf_skb_load_bytes_relative(skb, sizeof(ip), &tcp,
						sizeof(tcp),
						BPF_HDR_START_NET) < 0) {
			bump(0);
			return NF_ACCEPT;
		}
		dport = __bpf_ntohs(tcp.dest);
	} else if (ip.protocol == IPPROTO_UDP) {
		if (bpf_skb_load_bytes_relative(skb, sizeof(ip), &udp,
						sizeof(udp),
						BPF_HDR_START_NET) < 0) {
			bump(0);
			return NF_ACCEPT;
		}
		dport = __bpf_ntohs(udp.dest);
	} else {
		bump(0);
		return NF_ACCEPT;
	}

	action = bpf_map_lookup_elem(&nf_guard_rules, &dport);
	if (action && *action == ACTION_DROP) {
		bump(1);
		return NF_DROP;
	}

	bump(0);
	return NF_ACCEPT;
}

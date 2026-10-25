// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②（XDP 侧）：同一报文在 XDP 挂载点的视角。
 * ctx 是 struct xdp_md：只有 data/data_end/data_meta 与少量元数据——
 * 报文解析全部手工：data/data_end 边界检查是 verifier 的硬要求
 * （第 3 章画廊 card ② 的正面用法）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* map 选型：ARRAY——单 key 全局计数，教学聚焦 ctx 差异而非 map 选型 */
struct pkt_stats {
    __u64 packets;   /* 经过的报文总数 */
    __u64 udp;       /* 其中 UDP 报文数 */
};
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct pkt_stats);
} xdp_stats SEC(".maps");

SEC("xdp")
int xdp_entry(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u32 key = 0;
    struct pkt_stats *v = bpf_map_lookup_elem(&xdp_stats, &key);
    if (!v)
        return XDP_PASS;

    __sync_fetch_and_add(&v->packets, 1);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)      /* 以太网头边界检查 */
        return XDP_PASS;
    if (eth->h_proto != bpf_htons(0x0800)) /* ETH_P_IP 不在 BTF，直接用数值 */
        return XDP_PASS;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)       /* IPv4 头边界检查 */
        return XDP_PASS;
    if (ip->protocol == 17)                /* IPPROTO_UDP */
        __sync_fetch_and_add(&v->udp, 1);

    return XDP_PASS;                       /* 只观测不干预 */
}

char LICENSE[] SEC("license") = "GPL";

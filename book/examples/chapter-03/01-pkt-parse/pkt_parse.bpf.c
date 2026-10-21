// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-pkt-parse：能通过验证的最小 XDP 报文解析程序（本章「通过路径」锚点）。
 * 教学落点：packet 指针的边界检查是 verifier 证明内存安全的核心——每层协议头
 *   解引用前先做 data + sizeof(层) > data_end 检查（缺一层就会被拒，见画廊 02）。
 * 失败模式：无 root 时 open_and_load 失败（预期，见 README）；被拒形态见画廊。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），按 libbpf-bootstrap 惯例手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

/* 选 array map：单槽全局计数器，key 恒为 0——演示「map 交互」这一最小闭环；
 * 多 CPU 并发计数用原子递增（verifier 天然放行），避免 per-CPU 读回聚合的额外讲解 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} udp_counts SEC(".maps");

SEC("xdp")
int pkt_parse(struct xdp_md *ctx)
{
    /* xdp_md 的 data/data_end 是寄存器宽度裸指针，经 (long) 中转标准转换 */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* 第一层：以太网头——解引用前先证明 [data, data+14) 在报文内 */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;                       /* 截断报文：放行交协议栈处理 */
    if (eth->h_proto != bpf_htons(ETH_P_IP))   /* h_proto 为网络字节序 */
        return XDP_PASS;

    /* 第二层：IPv4 头——边界检查以「上一层尾=这一层头」推进 */
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;
    if (ip->protocol != IPPROTO_UDP)
        return XDP_PASS;

    /* 第三层：UDP 头——同一模式再走一遍；不读 payload，无需更深的检查 */
    struct udphdr *udp = (void *)ip + sizeof(*ip);
    if ((void *)(udp + 1) > data_end)
        return XDP_PASS;

    __u32 key = 0;
    __u64 *count = bpf_map_lookup_elem(&udp_counts, &key);
    if (count)                                  /* array map 单槽恒在，防御式检查 */
        __sync_fetch_and_add(count, 1);         /* 原子递增：多网卡队列并发安全 */

    return XDP_PASS;                            /* 只观测不干预，一律放行 */
}

char LICENSE[] SEC("license") = "GPL";

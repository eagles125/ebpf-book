// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②（TC 侧）：同一报文在 TC 挂载点的视角。
 * ctx 是 struct __sk_buff：除了 data/data_end（与 xdp_md 形状相似！），还有
 * mark/protocol/len 等协议栈加工后的字段——「同一个包，两副眼镜」的另一半。
 * 冒烟结论④：TC_ACT_OK 是 uapi #define 不在 BTF，需手工定义。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define TC_ACT_OK 0    /* uapi pkt_cls.h 的 #define，不在 BTF/vmlinux.h（冒烟结论④） */

struct pkt_stats {
    __u64 packets;
    __u64 udp;
};
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct pkt_stats);
} tc_stats SEC(".maps");

SEC("tc")
int tc_entry(struct __sk_buff *skb)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 key = 0;
    struct pkt_stats *v = bpf_map_lookup_elem(&tc_stats, &key);
    if (!v)
        return TC_ACT_OK;

    __sync_fetch_and_add(&v->packets, 1);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;
    if (eth->h_proto != bpf_htons(0x0800))
        return TC_ACT_OK;

    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;
    if (ip->protocol == 17)
        __sync_fetch_and_add(&v->udp, 1);

    return TC_ACT_OK;    /* 放行：TC 的 verdict 在协议栈入口，语义与 XDP_PASS 对照 */
}

char LICENSE[] SEC("license") = "GPL";

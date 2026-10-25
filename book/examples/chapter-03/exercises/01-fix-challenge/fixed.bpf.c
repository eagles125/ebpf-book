// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 参考实现：三处错误逐一修复后的程序。
 * 修复要点与画廊对照：(1) 两层边界检查（画廊 02）(2) NULL 检查（画廊 01）
 * (3) 声明即初始化（画廊 04）；连带修复：v 的使用全部收进 NULL 检查之后。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* map 选型：ARRAY——单 key 全局计数，教学聚焦验证器行为而非 map 选型 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

SEC("xdp")
int challenge(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)         /* 修复(1a)：以太网头边界 */
        return XDP_PASS;
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)          /* 修复(1b)：IPv4 头边界 */
        return XDP_PASS;
    if (ip->protocol == 17)
        return XDP_DROP;

    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (!v)                                   /* 修复(2)：NULL 分支 */
        return XDP_PASS;
    __u64 verdict = *v;                       /* 修复(3)：有确定初值 */
    *v = verdict + 1;                         /* 连带修复：v 已定型为 map_value */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

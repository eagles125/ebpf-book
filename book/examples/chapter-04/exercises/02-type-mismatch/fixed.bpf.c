// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 2 参考实现：SEC 与 ctx 类型归位——报文解析逻辑回到 XDP。
 * 逻辑一字未动（数据/边界检查/计数），只把「程序类型三元组」换对：
 * SEC("xdp") + struct xdp_md *——ctx 访问从此在 is_valid_access 白名单内。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} mismatch_hits SEC(".maps");

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&mismatch_hits, &key);
    if (!v)
        return XDP_PASS;
    __sync_fetch_and_add(v, 1);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

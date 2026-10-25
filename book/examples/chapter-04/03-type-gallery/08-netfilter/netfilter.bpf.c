// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑧：netfilter——6.6+ 的新程序类型（本机 7.0 满足；预期加载前提：root）。
 * ctx 是 struct bpf_nf_ctx：{ state, in, out }——netfilter 钩子点的报文与状态。
 * 返回值语义：NF_ACCEPT/NF_DROP（uapi netfilter.h #define，不在 BTF——冒烟结论④）。
 * 完整可运行示例（Go 控制面 + 规则表）见第 9 章 09-netfilter-go/。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define NF_ACCEPT 1    /* uapi netfilter.h 的 #define，不在 BTF/vmlinux.h（冒烟结论④） */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} nf_seen SEC(".maps");

SEC("netfilter")
int nf(struct bpf_nf_ctx *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&nf_seen, &key);
    if (!v)
        return NF_ACCEPT;
    __sync_fetch_and_add(v, 1);
    return NF_ACCEPT;    /* 教学示例：只计数不拦截 */
}

char LICENSE[] SEC("license") = "GPL";

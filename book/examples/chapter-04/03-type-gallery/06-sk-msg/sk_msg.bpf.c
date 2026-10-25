// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑥：sk_msg——sockmap 上的报文重定向判决
 * （预期加载前提：root + sockmap 与 sockops 配对搭建，深入见第 22/23 章交叉引用）。
 * ctx 是 struct sk_msg_md：数据所在内存区间与两端 socket 元数据。
 * 返回值语义：__SK_PASS / __SK_DROP（uapi enum sk_action，成员名带下划线前缀，
 * 在 BTF——冒烟结论「常量在场」）。
 * 失败模式：sockmap 未挂对应 sockops 前该程序不会被触发。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sm_verdict SEC(".maps");

SEC("sk_msg")
int sm(struct sk_msg_md *msg)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&sm_verdict, &key);
    if (!v)
        return __SK_DROP;    /* map 异常时丢弃，教学示例从简 */
    __sync_fetch_and_add(v, 1);
    return __SK_PASS;        /* 教学示例：一律放行（真实重定向走 bpf_msg_redirect_hash） */
}

char LICENSE[] SEC("license") = "GPL";

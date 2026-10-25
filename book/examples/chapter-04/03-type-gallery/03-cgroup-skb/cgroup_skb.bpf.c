// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊③：cgroup/skb——cgroup 维度的网络 verdict
 * （预期加载前提：root + 挂到某个 cgroup；veth/netns 拓扑见第 17 章）。
 * 返回值语义：1 放行 / 0 丢弃——与 TC 的 TC_ACT_*、XDP 的 XDP_* 都不同，
 * 这是「类型决定返回值语义」的活例子。
 * map 选型：PER-CPU ARRAY——verdict 计数是高频写，per-CPU 免锁聚合
 * （第 2 章映射全景回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=放行 1=丢弃 */
    __type(value, __u64);
} cg_verdict SEC(".maps");

SEC("cgroup/skb")
int cg(struct __sk_buff *skb)
{
    __u32 key = 0;
    /* helper 选型：bpf_get_current_cgroup_id——拿到触发本程序执行的 cgroup，
     * 是 cgroup 族类型独有的上下文维度（对照：XDP/TC 没有 cgroup 语义） */
    __u64 cg_id = bpf_get_current_cgroup_id();
    (void)cg_id;             /* 教学示例：演示获取，不做过滤决策 */
    __u64 *v = bpf_map_lookup_elem(&cg_verdict, &key);
    if (!v)
        return 1;            /* map 异常时放行，避免误杀流量 */
    __sync_fetch_and_add(v, 1);
    return 1;                /* 1=放行 */
}

char LICENSE[] SEC("license") = "GPL";

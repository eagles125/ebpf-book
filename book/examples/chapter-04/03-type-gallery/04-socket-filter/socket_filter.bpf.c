// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊④：socket filter——最古老的 BPF 用途（tcpdump 的载体）
 * （预期加载前提：root + SO_ATTACH_BPF 到某个 socket）。
 * 返回值语义：返回值 = 接受拷贝给该 socket 的字节数（0 = 这个包对该 socket 不可见）。
 * 只读观测：不干预协议栈，报文照常投递——与 cgroup/skb 的「丢弃」语义本质不同。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sf_seen SEC(".maps");

SEC("socket")
int sf(struct __sk_buff *skb)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&sf_seen, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;    /* 教学示例：恒 0=不投递给观测 socket，包本身不受影响 */
}

char LICENSE[] SEC("license") = "GPL";

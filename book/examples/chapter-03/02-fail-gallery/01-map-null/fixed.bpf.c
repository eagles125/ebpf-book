// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 01-map-null / fixed：bad 的最小修复版——仅插入一个 NULL 判空分支。
 *
 * 修复原理：if (!v) return XDP_PASS; 让「v == NULL」走显式分支，验证器（verifier）
 *   据此把后续路径上 v 的类型从 map_value_or_null 定型为 map_value——联合状态
 *   收窄成单态，*v += 1 的解引用即可证明安全，加载期放行。NULL 分支的返回动作
 *   属业务语义（这里选择放行），验证器只要求「NULL 不被解引用」。
 *
 * 与 bad.bpf.c 的 diff 仅此一处语义差异（函数名 probe_fixed 与头部注释除外）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

/* 画廊公共 stats map 块：与 bad.bpf.c 完全一致 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (!v)
        return XDP_PASS;           /* 修复：NULL 分支让类型从 map_value_or_null 定型为 map_value */
    *v += 1;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

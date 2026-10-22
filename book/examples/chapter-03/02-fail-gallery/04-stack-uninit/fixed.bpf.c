// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 04-stack-uninit / fixed：bad 的最小修复版——声明处补一处初始化。
 *
 * 修复原理：__u64 verdict = 0; 让 verdict 在声明时就有确定值——v == NULL 的路径上
 *   读到的是 0 而非未定义数据，验证器（verifier）对栈槽的初始化跟踪在所有路径上
 *   都看到「先写后读」，加载期放行。初始化值按业务语义选（这里 0 表示「无计数
 *   信号」），验证器只要求「不读未初始化的栈槽」。
 *
 * 与 bad.bpf.c 的 diff 仅此一处语义差异（声明处的 = 0）；两侧同带 volatile——那是
 * 编译级可观测性装置而非修复的一部分（见 bad.bpf.c 头注释），保留它使 diff 只剩
 * 初始化这一处。函数名 probe_fixed 与头部注释亦不同。
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
    volatile __u64 verdict = 0;    /* 修复：声明即初始化，所有路径上都有确定值 */
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (v)
        verdict = *v;
    if (verdict > 10)
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

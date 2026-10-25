// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 任务体：修复挑战（预期被拒：三处组合错误）。
 * 错误不重复画廊任何单一 case，需综合运用本章知识定位：
 *   (1) 报文边界缺失（对照画廊 02）
 *   (2) 未检查 map lookup（对照画廊 01）
 *   (3) 未初始化栈变量（对照画廊 04）
 * 破解顺序建议：从日志第一处报错入手，修一处再看下一处。
 *
 * 可观测性说明：verdict 声明行上的 volatile 不是错误、也不是修复的一部分——
 *   它是编译级装置（画廊 04 同一先例）：未初始化读在 C 里是未定义行为，clang -O2
 *   会按 undef 折叠（实测：裸形式下 verdict 被寄存器分配、栈槽读整条消失，
 *   未初始化信号只剩寄存器形态）；volatile 让每次读写都落到真实栈槽，
 *   offending 指令（未初始化栈槽读取）得以保留。本文件在 clang 层面完全合法
 *   （编译通过）；拒绝发生在加载期。参考实现见同目录 fixed.bpf.c。
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
    struct ethhdr *eth = data;
    struct iphdr *ip = data + sizeof(*eth);   /* 错误(1)：两层均无 data_end 检查 */
    if (ip->protocol == 17)
        return XDP_DROP;

    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&stats, &key);   /* 错误(2)：未检查 NULL */
    volatile __u64 verdict;                   /* 错误(3)：仅条件赋值（volatile 见头注释可观测性说明） */
    if (v)
        verdict = *v;
    if (verdict > 0)
        *v = verdict + 1;                     /* 连带：v 为 NULL 时此行同样危险 */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

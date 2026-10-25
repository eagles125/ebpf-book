// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 04-stack-uninit / bad：预期被拒：栈变量仅在 if (v) 分支赋值，分支外使用可能未初始化。
 *
 * verifier 为什么会拒绝：验证器（verifier）对 512 字节栈的每个槽位都跟踪初始化状态
 *   （栈槽的初始化跟踪，见正文 2.6）。本程序里 verdict 只在 if (v) 分支内被写入；
 *   v == NULL（key 不在映射（map）里、映射内存紧张等都查不到）的那条路径上没有
 *   任何写入，但
 *   if (verdict > 10) 依然要读这个槽——「读一个从未写过的栈槽」在加载期即被拒绝
 *   （豁免条件随内核版本：6.3 起特权档 CAP_PERFMON 一类放行未初始化栈读，
 *   5.10–6.2 特权与非特权一律拒绝——见本目录 README 的特权豁免说明）。
 *   内核栈不会在使用间清零，未初始化的槽里可能残留前一次系统调用留下的敏感数据，
 *   读出并回传就是信息泄露，所以按最坏情况证明的验证器不允许任何未初始化读。
 *
 * 可观测性说明：声明行上的 volatile 不是修复、也不是语义错误的一部分——它是编译级
 *   装置：未初始化读在 C 里是未定义行为，clang -O2 会把「分支外使用未初始化值」按
 *   undef 折叠（实测：折叠后产物里没有那条未初始化读，整个程序反而变成验证器可
 *   放行的形态）；volatile 让每次读写都落到真实栈槽，offending 指令（未初始化栈槽
 *   读取）得以保留。本文件在 clang 层面完全合法（编译通过）；拒绝发生在加载期。
 *   修复形态见同目录 fixed.bpf.c——仅补一处声明初始化；卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

/* 画廊公共 stats map 块：单槽 array 映射——本对的错误点不在 map 本身，
 * 而在 lookup 返回值的条件赋值与栈初始化跟踪 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    __u32 key = 0;
    volatile __u64 verdict;        /* 预期被拒：仅在 if (v) 分支赋值，分支外使用可能未初始化（volatile 抑制编译器 UB 折叠，见头注释） */
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    if (v)
        verdict = *v;
    if (verdict > 10)
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

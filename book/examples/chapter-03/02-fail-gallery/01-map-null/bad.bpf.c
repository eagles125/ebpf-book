// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 01-map-null / bad：预期被拒：map_value_or_null 直接解引用。
 *
 * verifier 为什么会拒绝：bpf_map_lookup_elem 这个辅助函数（helper）的返回值天然
 *   可能是 NULL——key 不在映射（map）里、key 越界（本例 array 映射只有 1 个槽）
 *   都查不到。因此返回值寄存器的类型是 map_value_or_null：「映射值指针 或 NULL」
 *   两态合一。验证器（verifier）按最坏情况证明安全：不判空就 *v += 1，存在对
 *   NULL 解引用的路径——内核态空指针访问会让内核崩溃，违反「已验证程序不可能
 *   崩溃内核」的安全承诺，所以必须在加载期拒绝，而不是等运行时出错。
 *
 * 本文件在 clang 层面完全合法（编译通过）；拒绝发生在加载期。修复形态见同目录
 * fixed.bpf.c——仅插入一个 NULL 分支；卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

/* 画廊公共 stats map 块：单槽 array 映射计数器——本对的错误点不在 map 本身，
 * 而在 lookup 返回值的处理，故保留最简形态 */
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
    __u64 *v = bpf_map_lookup_elem(&stats, &key);
    *v += 1;                       /* 预期被拒：返回值可能是 NULL，直接解引用 */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

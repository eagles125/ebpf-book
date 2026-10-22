// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 03-ptr-math / fixed：bad 的最小修复版——算术移到 packet 指针上并用界检查收口。
 *
 * 修复原理：需要「越过若干字节」时，偏移运算加在 packet 指针（ctx->data 一侧）上，
 *   且结果立刻与 data_end 比较——if (data + 4 > data_end) 同时完成「做算术」与
 *   「证安全」两件事：验证器（verifier）允许 packet 指针（PTR_TO_PACKET）参与
 *   算术，但要求算术结果的后续访问仍被界检查约束；尾界 data_end 本身保持只读，
 *   信任模型不被破坏。
 *
 * 与 bad.bpf.c 的 diff 仅一处语义错误：bad 对尾界做算术 data_end += 4（其算术结果
 *   参与比较只为防编译器死代码消除，见 bad 头注释）；fixed 不触碰尾界——同一偏移
 *   运算直接放在 packet 指针上并用界检查收口（bad 命中分支返回 XDP_DROP、fixed
 *   一律返回 XDP_PASS；函数名与头部注释亦不同）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)       /* 修复：算术放在 packet 指针上，且立刻用界检查收口 */
        return XDP_PASS;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

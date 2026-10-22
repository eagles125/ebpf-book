// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 03-ptr-math / bad：预期被拒：pkt_end 指针禁止算术。
 *
 * verifier 为什么会拒绝：ctx->data_end 是报文尾界（pkt_end）——验证器（verifier）
 *   信任模型的锚点：整个「报文内安全」的证明全部建立在「指针与 data_end 比较」
 *   之上。若允许对尾界本身做算术（data_end += 4），程序就能挪动界标、把越界访问
 *   伪造成「界内」，尾界的可信性即告失效。因此内核把尾界寄存器的类型
 *   （PTR_TO_PACKET_END）登记为不可参与算术——加载期直接拒绝，而不是放行一个
 *   可能被篡改的界标。需要越过若干字节时，把算术放在 packet 指针上并立刻用界
 *   检查收口（见同目录 fixed.bpf.c）。
 *
 * 本文件在 clang 层面完全合法（编译通过）；拒绝发生在加载期。
 * 卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    data_end += 4;                 /* 预期被拒：pkt_end 指针禁止算术（内核以此保住报文尾界的可信性） */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

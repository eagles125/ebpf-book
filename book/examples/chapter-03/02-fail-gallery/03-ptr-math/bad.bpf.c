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
 * 可观测性说明：紧随其后的比较消费了算术结果——这不是修复，而是防止 clang -O2 把
 *   未使用的 data_end += 4 当死代码消除（消除后产物里没有这条 offending 指令，
 *   加载期也就看不到拒绝）。比较行写作 data > data_end 而非 data + 4 > data_end：
 *   两侧偏移不等（0 对 4），编译器无法折叠公共偏移，算术指令得以保留；真正被拒
 *   的仍是对尾界的算术本身。本文件在 clang 层面完全合法（编译通过）；拒绝发生在
 *   加载期。卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    data_end += 4;                 /* 预期被拒：pkt_end 指针禁止算术（内核以此保住报文尾界的可信性） */
    if (data > data_end)           /* 算术结果参与比较：防止 clang -O2 死代码消除掉 offending 指令 */
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

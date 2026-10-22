// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 02-pkt-bounds / bad：预期被拒：报文边界检查缺失——首个越界读在 eth->h_proto。
 *
 * verifier 为什么会拒绝：packet 指针（从 ctx->data 起）指向长度可变的报文，网络
 *   报文随时可能被硬件/上游截断，验证器（verifier）无法凭「一般报文够长」放行——
 *   每个字段的读偏移都必须先与 data_end 界标比较、证明落在报文内。本程序把
 *   eth/ip 两层指针推进后未做任何 (void *)(指针 + 1) > data_end 检查就读字段，
 *   第一个字段读 eth->h_proto（偏移 12..13）时已不可证明在报文内——越界读会泄出
 *   报文缓冲区之外的内核内存，因此加载期直接拒绝。
 *
 * 对照通过路径锚点 ../../01-pkt-parse/pkt_parse.bpf.c：同一解析链，每层解引用前
 * 先做界检查即放行。本文件在 clang 层面完全合法（编译通过）；修复形态见同目录
 * fixed.bpf.c——仅插入两层界检查；卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>    /* bpf_htons：h_proto 为网络字节序 */

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    struct iphdr *ip = data + sizeof(*eth);
    if (eth->h_proto == bpf_htons(ETH_P_IP) && ip->protocol == IPPROTO_UDP)
        return XDP_DROP;           /* 预期被拒：首个越界读在 eth->h_proto（无任何 data_end 检查） */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

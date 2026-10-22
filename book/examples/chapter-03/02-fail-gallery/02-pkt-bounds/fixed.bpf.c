// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 02-pkt-bounds / fixed：bad 的最小修复版——仅插入两层边界检查。
 *
 * 修复原理：每层协议头解引用前先证明「整层落在报文内」——第一层检查
 *   (void *)(eth + 1) > data_end，第二层检查 (void *)(ip + 1) > data_end；
 *   检查通过后验证器（verifier）才把对应字段的读标记为报文内安全。推进模式是
 *   「上一层尾 = 这一层头」：IPv4 头指针取 data + sizeof(*eth)。代码形态与通过
 *   路径锚点 ../../01-pkt-parse/pkt_parse.bpf.c 的一、二层一致，仅命中 UDP 时
 *   返回 XDP_DROP（丢弃）而非放行。
 *
 * 与 bad.bpf.c 的 diff 仅此两处界检查（IPv4 头指针声明随第一层检查后移；
 * 函数名 probe_fixed 与头部注释除外）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>    /* bpf_htons：h_proto 为网络字节序 */

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)      /* 修复①：第一层界检查——以太网头（14 字节）完整落在报文内才可读 */
        return XDP_PASS;
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)       /* 修复②：第二层界检查——IPv4 头完整落在报文内才可读 */
        return XDP_PASS;
    if (eth->h_proto == bpf_htons(ETH_P_IP) && ip->protocol == IPPROTO_UDP)
        return XDP_DROP;                   /* 与 bad 相同的判定与动作，此时读取已被界检查证明安全 */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

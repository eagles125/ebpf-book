// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 1：最小 XDP 丢弃示例。
 *
 * 闭环：XDP 挂载点（网卡驱动收包最早处）-> 动作返回码 -> loader 轮询 map 观察效果。
 * 行为：丢弃所有 IPv4 报文（XDP_DROP），放行 ARP 等（XDP_PASS），per-CPU 数组计数。
 *
 * 返回值语义（include/uapi/linux/bpf.h 中 enum xdp_action）：
 *   XDP_DROP(1)  在驱动层直接丢弃，不生成 skb、不进协议栈；
 *   XDP_PASS(2)  交给内核协议栈继续处理；
 *   XDP_TX(4)/XDP_REDIRECT(6) 本章暂不使用。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* ETH_P_IP 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP 0x0800

enum { STAT_PASS = 0, STAT_DROP };

/* 选 per-CPU 数组：每 CPU 独立副本，数据面更新无锁、零竞争；loader 侧再求和 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

/* 小工具：计数 +1；lookup 失败静默跳过，不影响主路径 */
static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

SEC("xdp")
int xdp_drop(struct xdp_md *ctx)
{
    /* xdp_md 的 data/data_end 描述报文起止； verifier 只认显式边界检查，
       「人看起来安全」的代码一律拒绝加载 */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    bump(STAT_PASS);

    /* Ethernet 头固定 14 字节；必须在解引用前确认 eth+1 不越过 data_end */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;          /* 残包交协议栈处理 */

    if (eth->h_proto == bpf_htons(ETH_P_IP)) {
        bump(STAT_DROP);
        return XDP_DROP;          /* 教学演示：IPv4 全部丢弃 */
    }
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";   /* GPL 兼容声明，必需 */

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 05-loop / fixed：bad 的最小修复版——循环上界加掩码定界。
 *
 * 修复原理：n = *(__u32 *)data & 0xF 把上界收窄到 [0, 15]——验证器（verifier）的
 *   边界推导沿 & 运算传播：任意 32 位无符号数按位与 0xF 后落回 [0, 15]，循环
 *   迭代至多 15 次、必然终止，back-edge 可证明有界（有界循环，5.3 起支持），
 *   加载期放行。掩码位数按业务语义选：上界 = 2^位数 − 1，需要更大迭代量时用
 *   更宽的掩码或把循环挪到 bpf_loop（5.17+）回调里。
 *
 * 与 bad.bpf.c 的 diff 仅此一处语义差异（掩码）；循环累加两侧同用 sum ^= i——那是
 * 编译级可观测性装置而非修复的一部分（见 bad.bpf.c 头注释），保留它使 diff 只剩
 * 掩码这一处。函数名 probe_fixed 与头部注释亦不同。
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
    if (data + 4 > data_end)
        return XDP_PASS;
    __u32 n = *(__u32 *)data & 0xF;   /* 修复：掩码定界，上界收窄到 [0, 15]（迭代至多 15 次），循环可证明终止 */
    __u64 sum = 0;
    for (__u32 i = 0; i < n; i++)
        sum ^= i;                  /* 与 bad 相同：XOR 防止归纳变量优化把循环折叠成闭式 */
    if (sum == 0xdeadbeef)
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

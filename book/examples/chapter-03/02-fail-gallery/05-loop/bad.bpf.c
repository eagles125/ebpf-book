// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 05-loop / bad：预期被拒：循环上界取自报文且无掩码，迭代次数不可证明。
 *
 * verifier 为什么会拒绝：循环体是一条回跳的 back-edge——验证器（verifier）要么证明
 *   循环必然终止（有界循环，5.3 起支持），要么拒绝。本程序循环上界 n 直接取自
 *   报文内容（*(u32 *)data，最大可达 2^32-1），且没有任何掩码收窄：验证器对 n 的
 *   取值推导只能得到「任意 32 位无符号数」，无法证明迭代有界。看似「报文一般
 *   不长」没有用——最坏情况证明要求覆盖攻击者构造的超长迭代：一个被放行的
 *   n 次循环会把 CPU 卡在收包软中断里，即拒绝服务（DoS），所以加载期直接拒绝。
 *
 * 可观测性说明：循环累加写作 sum ^= i 而非 sum += i——这不是修复的一部分，而是
 *   编译级装置：+= 是线性递推，clang -O2 的归纳变量优化（-indvars）会算出闭式
 *   n*(n-1)/2、把循环整条消除（实测：产物里没有任何 back-edge，程序反而变成
 *   验证器可放行的形态）；XOR 递推没有闭式，循环得以保留。if (sum == 0xdeadbeef)
 *   消费循环结果，同样防止循环体被当死代码删除。本文件在 clang 层面完全合法
 *   （编译通过）；拒绝发生在加载期。修复形态见同目录 fixed.bpf.c——仅加一处
 *   掩码；卡片讲解见本目录 README.md。
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
    if (data + 4 > data_end)
        return XDP_PASS;
    __u32 n = *(__u32 *)data;      /* 预期被拒：上界取自报文且无掩码，迭代次数不可证明 */
    __u64 sum = 0;
    for (__u32 i = 0; i < n; i++)
        sum ^= i;                  /* XOR 而非 +=：线性递推会被 -indvars 折叠成闭式、循环整条消除（见头注释可观测性说明） */
    if (sum == 0xdeadbeef)         /* 消费 sum，防止被优化删除 */
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

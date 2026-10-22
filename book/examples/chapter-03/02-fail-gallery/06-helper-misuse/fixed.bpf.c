// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 06-helper-misuse / fixed：bad 的最小修复版——改用 XDP 白名单内的等价能力。
 *
 * 修复原理：bpf_get_smp_processor_id() 返回当前 CPU 编号，且在 XDP 程序类型的
 *   白名单内（收包路径天然知道自己在哪个 CPU 上跑）——语义上与 bad 想做的
 *   「拿到一个执行环境标识做分流/聚合」等价：按 pid 聚合换不成，就按 CPU 聚合。
 *   验证器（verifier）核对 call 指令的 helper id 在白名单内，加载期放行。
 *   选辅助函数（helper）前先确认其程序类型适用面（include/uapi/linux/bpf.h 注释或
 *   bpftool feature probe）。
 *
 * 与 bad.bpf.c 的 diff 仅此一处语义差异（helper 换名与对应变量名、注释）；函数名
 * probe_fixed 与头部注释亦不同。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_fixed(struct xdp_md *ctx)
{
    __u32 cpu = bpf_get_smp_processor_id();         /* 修复：改用 XDP 白名单内的等价能力 */
    if (cpu)
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

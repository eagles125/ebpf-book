// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 06-helper-misuse / bad：预期被拒：tracing 类专用 helper 不在 XDP 程序类型的白名单。
 *
 * verifier 为什么会拒绝：辅助函数（helper）不是全局可用——内核按程序类型（program
 *   type）登记各自的 helper 白名单（verifier_ops，见正文 2.7）。bpf_get_current_pid_tgid()
 *   读取「当前进程上下文」，只有 tracing/kprobe 等挂在进程上下文里的程序类型才有
 *   语义；XDP 程序跑在网卡收包软中断里，没有「当前进程」可言——放行它不仅语义
 *   错乱，还会把任意进程的 pid/tgid 泄露给网卡路径上的程序。验证器在加载期检查
 *   每条 call 指令：helper id 不在本程序类型的白名单内即拒绝。
 *
 * 本文件在 clang 层面完全合法（编译通过）——clang 只负责把 call 翻译成
 *   「调用 helper id 14」，白名单核对发生在加载期。修复形态见同目录 fixed.bpf.c
 *   ——改用 XDP 白名单内的等价能力；卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* uapi 常量不在 vmlinux.h 中（BTF 只含类型），画廊 XDP 类骨架统一手工定义 */
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17

SEC("xdp")
int probe_bad(struct xdp_md *ctx)
{
    __u32 pid = (__u32)bpf_get_current_pid_tgid();  /* 预期被拒：tracing 类专用 helper，不在 XDP 白名单 */
    if (pid)
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 07-kernel-ptr / bad：预期被拒：ctx 标量字段强转指针后直接解引用。
 *
 * verifier 为什么会拒绝：tracepoint 上下文（struct trace_event_raw_sys_enter）的
 *   args[] 字段是「指针的数值」——寄存器类型是标量（SCALAR_VALUE），不是任何
 *   可解引用的指针类型。验证器（verifier）的类型系统只允许解引用「登记过的指针」：
 *   ctx 本身可按布局访问（申报访问），而 ctx 字段里存着的内核/用户地址属于
 *   「任意地址」，直接解引用 = 允许 BPF 程序读内核里任何位置（任意读漏洞），
 *   因此加载期拒绝。任意地址必须走 probe 系辅助函数（helper）（bpf_probe_read_user_str
 *   等，helper 内部做受界限检查——第 2 章预告⑦的兑现，正文 2.3 寄存器类型表）。
 *
 * 本文件在 clang 层面完全合法（编译通过）——C 的强制转换随便写，寄存器类型的
 *   核对发生在加载期。本对为 tracepoint 程序类型（无 XDP 挂载风险），画廊骨架中
 *   的 uapi 常量块按画廊骨架约定不加。修复形态见同目录 fixed.bpf.c——probe 系 helper
 *   受控读取；卡片讲解见本目录 README.md。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

SEC("tracepoint/syscalls/sys_enter_openat")
int probe_bad(struct trace_event_raw_sys_enter *ctx)
{
    char *fn = (char *)ctx->args[1];   /* 预期被拒：ctx 标量字段强转指针后直接解引用 */
    if (fn[0] == '/')                  /* 任意内核/用户地址必须走 probe 系 helper（第 2 章预告） */
        return 1;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

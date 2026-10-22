// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 画廊 07-kernel-ptr / fixed：bad 的最小修复版——probe 系 helper 受控读取到栈缓冲。
 *
 * 修复原理：ctx->args[1]（openat 的 filename 参数）指向用户态字符串——「任意
 *   地址」不能解引用，但可以通过 bpf_probe_read_user_str() 读：helper 内部把
 *   源地址限定在用户空间、按缓冲区大小受控拷贝，出错返回负值而非崩溃。读取
 *   结果落在栈缓冲 fn[64] 里——栈是验证器（verifier）逐槽跟踪的内存，fn[0]
 *   的读取在「helper 返回 > 0」（拷贝成功）的分支内，安全可证明。
 *
 * 版本演进点（保守注明）：bpf_probe_read_user_str 为 5.5+（按访问空间拆分的
 *   probe 系 helper）；更旧内核用 bpf_probe_read_str，其读取空间随架构配置
 *   而异——以所用内核文档为准。与 bad.bpf.c 的 diff 仅此一处语义差异（解引用
 *   换成 helper 受控读取）；函数名 probe_fixed 与头部注释亦不同。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

SEC("tracepoint/syscalls/sys_enter_openat")
int probe_fixed(struct trace_event_raw_sys_enter *ctx)
{
    char fn[64] = {};                  /* 修复：probe 系 helper 受控读取到栈缓冲 */
    long n = bpf_probe_read_user_str(fn, sizeof(fn), (const void *)ctx->args[1]);
    if (n > 0 && fn[0] == '/')         /* 5.5+ 用 user 变体；旧版 bpf_probe_read_str 行为随架构，保守注明 */
        return 1;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

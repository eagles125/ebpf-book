// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊①：kretprobe——在目标函数返回处触发（预期加载前提：root/特权档）。
 * 与 kprobe 的分工：kprobe 看入口参数，kretprobe 看「办成了没有」。
 * 返回值读取：BPF_KRETPROBE 宏（bpf_tracing.h）按架构展开取返回值寄存器
 * （x86 由 PT_REGS_RC——以所用架构为准）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* map 选型：ARRAY——两个 key（成功/失败）固定计数 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=成功(fd) 1=失败(负错误码) */
    __type(value, __u64);
} openat2_verdict SEC(".maps");

SEC("kretprobe/do_sys_openat2")
int BPF_KRETPROBE(krp, int ret)
{
    __u32 key = ret >= 0 ? 0 : 1;
    __u64 *v = bpf_map_lookup_elem(&openat2_verdict, &key);
    if (!v)                  /* lookup 失败分支必检（第 3 章 card ①） */
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

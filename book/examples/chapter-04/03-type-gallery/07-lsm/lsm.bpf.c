// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑦：LSM——把 BPF 程序变成一条安全策略
 * （预期加载前提：root + CONFIG_BPF_LSM=y + 内核启动参数 lsm=bpf；
 *   本机 CONFIG_BPF_LSM=y 实证，运行级待验证）。
 * ctx 是 hook 原型的参数直列：file_open hook 即 BPF_PROG(lf, struct file *file)——
 * 内核对象（struct file）直接可读，这是安全类类型与追踪/网络类的最大差别。
 * 返回值语义：0 允许；非 0 阻断该操作（各 hook 裁决语义以所用内核源码
 * include/linux/lsm_hook_defs.h 为准）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lsm_opens SEC(".maps");

SEC("lsm/file_open")
int BPF_PROG(lf, struct file *file)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&lsm_opens, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;    /* 0=允许打开；教学示例只审计不拦截 */
}

char LICENSE[] SEC("license") = "GPL";

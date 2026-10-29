// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 2 任务体：类型错位（预期被拒：ctx 访问越权——预期日志关键行
 * 「invalid bpf_context access」形态，运行级待验证）。
 * 错误：报文解析逻辑属于网络 ctx，却放在 kprobe SEC 且 ctx 声明为 xdp_md——
 * kprobe 的 is_valid_access 只放行 pt_regs 形状的字段（第 3 章 2.3/3.6 回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} mismatch_hits SEC(".maps");

SEC("kprobe/do_sys_openat2")
int probe_broken(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;       /* 预期被拒：kprobe ctx 无 data 字段 */
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&mismatch_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

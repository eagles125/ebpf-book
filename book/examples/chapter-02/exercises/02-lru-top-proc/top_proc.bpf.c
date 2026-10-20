// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 参考实现：LRU hash 记录每进程被调度次数（pid -> 计数）。
 * LRU 容量语义：满表时内核静默淘汰最久未用表项，插入仍成功——
 * 观测上表现为「旧进程从榜单消失」（口径与第 17 章阶段 3 一致）。
 * 失败模式：BPF_NOEXIST 并发竞争失败（-EEXIST）时本次调度不计数（教学近似，见注释）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define PROC_MAX 4096   /* LRU 容量：教学机典型进程远少于此；调小可观察淘汰行为 */

/* 选 LRU hash：进程集合不可预知且规模有上界诉求，满表自动淘汰而非插入失败 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, PROC_MAX);
    __type(key, __u32);      /* pid：被调度切入的进程 */
    __type(value, __u64);    /* 被调度次数 */
} proc_counts SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int top_proc(struct trace_event_raw_sched_switch *ctx)
{
    __u32 pid = ctx->next_pid;
    if (!pid)
        return 0;   /* pid 0 = idle/swapper：不入榜，否则榜单永远被刷屏 */
    __u64 *c = bpf_map_lookup_elem(&proc_counts, &pid);
    if (c) {
        (*c)++;
        return 0;
    }
    __u64 one = 1;
    bpf_map_update_elem(&proc_counts, &pid, &one, BPF_NOEXIST);
    /* -EEXIST = 另一 CPU 先插入：本事件不累计（近似）。LRU 满表不走此失败分支——
       内核静默淘汰最久未用项后插入仍成功，只能靠观察旧表项消失来发现。 */
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

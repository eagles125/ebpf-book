// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 参考实现：sched_switch 事件流（ring buffer 推送）。
 * 与 01-trace-stat 的对照：轮询（loader 定时拉）vs 事件流（数据面有事件就推）。
 * 失败模式：ringbuf 满时 reserve 返回 NULL——观测程序必须静默丢弃，
 * 不得阻塞内核路径反压（教学取舍讲解见 README）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* 事件结构必须 8 字节对齐（ringbuf 硬性要求），24 字节 = 8 的倍数 */
struct sched_event {
    __u64 ts_ns;     /* bpf_ktime_get_ns：内核单调时钟（非墙钟） */
    __u32 prev_pid;  /* 让出 CPU 的进程 */
    __u32 next_pid;  /* 接管 CPU 的进程 */
    __u32 cpu;       /* 事件发生的 CPU（bpf_get_smp_processor_id） */
    __u32 pad;       /* 对齐填充：保持整体 8 字节对齐 */
};

/* 选 ringbuf：跨 CPU 共享的高吞吐事件通道，用户态轮询消费；16MB 教学容量（满则丢事件） */
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int trace_events(struct trace_event_raw_sched_switch *ctx)
{
    struct sched_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return 0;   /* 满表丢弃：观测不得阻塞调度路径（与轮询取舍讲解呼应） */
    e->ts_ns = bpf_ktime_get_ns();
    e->prev_pid = ctx->prev_pid;
    e->next_pid = ctx->next_pid;
    e->cpu = bpf_get_smp_processor_id();
    e->pad = 0;
    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

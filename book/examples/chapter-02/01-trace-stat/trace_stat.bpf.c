// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-trace-stat：sched_switch tracepoint + per-CPU 数组统计。
 * 教学闭环（八要素落点）：程序类型与挂载点（SEC 隐式推断）-> 上下文对象（vmlinux.h
 *   CO-RE 读取）-> 辅助函数选型 -> map 选型（per-CPU array）-> 事件通道（loader 轮询）。
 * 失败模式：无 root 时 open_and_load 失败（预期，见 README）；事件推送形态见练习 1。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#define MAX_CPUS 64   /* 教学上限假设：仅统计 CPU 0–63；更多 CPU 的机器需调大并同步 loader */

/* value：一次统计同时演示「计数」与「上下文对象读取」两个教学点 */
struct cpu_stat {
    __u64 switches;      /* 本 CPU 的 sched_switch 触发次数 */
    __u64 last_next_pid; /* 最近一次被调度切入的进程 pid（来自 ctx 字段读取） */
};

/* 选 per-CPU 数组：key=CPU 下标（bpf_get_smp_processor_id 选定），每 CPU 只写自己的
 * 槽位，数据面无锁、零竞争；代价是用户态读回时要对 per-CPU 槽位求和。
 * 数组 map 对越界 key 会在 helper 内部返回 NULL（运行时定界），下方显式检查是为了
 * 把「CPU 下标异常」与「表项异常」区分开（防御式写法，非 verifier 硬性要求）。 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, MAX_CPUS);
    __type(key, __u32);
    __type(value, struct cpu_stat);
} sched_counts SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int trace_stat(struct trace_event_raw_sched_switch *ctx)
{
    __u32 cpu = bpf_get_smp_processor_id();   /* 选型原因：稳定获取当前 CPU，无需解析 ctx */
    if (cpu >= MAX_CPUS)
        return 0;                             /* 防御定界：超出教学上限的 CPU 不统计 */
    struct cpu_stat *p = bpf_map_lookup_elem(&sched_counts, &cpu);
    if (!p)
        return 0;                             /* per-CPU 槽位由内核零初始化，正常不应失败 */
    p->switches++;
    /* tracepoint 上下文字段经 vmlinux.h CO-RE 读取：内核布局变化时 libbpf 重定位。
     * prev_comm/next_comm 为定长数组（本机 BTF 实测 16 字节），直接读不越界；
     * 变长字符串才需要 bpf_probe_read_str（语义讲解见正文 5.2 与排错节）。 */
    p->last_next_pid = ctx->next_pid;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

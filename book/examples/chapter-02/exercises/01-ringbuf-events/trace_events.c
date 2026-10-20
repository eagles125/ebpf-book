// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 1 用户态 loader：ring_buffer__poll 事件循环。
 * 用法: sudo ./trace_events [打印条数上限，缺省 50]
 * 职责: open_and_load -> attach（SEC 推断）-> ring_buffer 轮询消费事件。
 * 注意: sched/sched_switch 事件速率极高，16MB 缓冲打满后新事件被静默丢弃，
 *       属预期行为（取舍讲解见 README）。
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "trace_events.skel.h"

/* 与 trace_events.bpf.c 中的定义逐字段一致（skeleton 不导出自定义 map value 类型） */
struct sched_event {
    __u64 ts_ns;
    __u32 prev_pid;
    __u32 next_pid;
    __u32 cpu;
    __u32 pad;
};

static volatile sig_atomic_t exiting = 0;
static long printed = 0;
static long limit = 50;

static void handle_sig(int sig) { exiting = 1; }

/* 事件回调：data 指向 ringbuf 租借给用户态的内存，返回后由框架自动归还 */
static int on_event(void *ctx, void *data, size_t len)
{
    if (printed >= limit)      /* 已达打印上限：丢弃积压事件，主循环随即退出 */
        return 0;
    const struct sched_event *e = data;
    printf("cpu%-2u %llu ns: prev=%u next=%u\n",
           e->cpu, (unsigned long long)e->ts_ns, e->prev_pid, e->next_pid);
    printed++;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        limit = atol(argv[1]);
    struct trace_events_bpf *skel = trace_events_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }
    if (trace_events_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 见 ../../00-env/check-env.sh）\n");
        trace_events_bpf__destroy(skel);
        return 1;
    }
    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.events),
                                              on_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "ring_buffer__new 失败\n");
        trace_events_bpf__destroy(skel);
        return 1;
    }
    signal(SIGINT, handle_sig);
    while (!exiting && printed < limit) {
        int n = ring_buffer__poll(rb, 100);  /* 100ms 超时轮询：保证 Ctrl-C 及时生效 */
        if (n < 0 && n != -EINTR)
            break;
    }
    ring_buffer__free(rb);
    trace_events_bpf__destroy(skel);
    return 0;
}

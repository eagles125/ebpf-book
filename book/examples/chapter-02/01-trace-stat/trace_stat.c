// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-trace-stat 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./trace_stat
 * 职责: open_and_load（打开对象 -> verifier 校验 -> JIT）-> attach（tracepoint 由
 *       SEC 名自动推断）-> 每秒打印各 CPU 调度计数 -> Ctrl-C 退出即卸载。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "trace_stat.skel.h"

#define MAX_CPUS 64   /* 与 trace_stat.bpf.c 的 MAX_CPUS 契约一致（skeleton 不导出宏） */

/* 与 trace_stat.bpf.c 中的定义逐字段一致（skeleton 不导出自定义 map value 类型） */
struct cpu_stat {
    __u64 switches;
    __u64 last_next_pid;
};

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

int main(void)
{
    int nr = libbpf_num_possible_cpus();
    if (nr < 1 || nr > MAX_CPUS) {
        fprintf(stderr, "CPU 数 %d 超出教学上限 %d，请同步调大两侧 MAX_CPUS\n",
                nr, MAX_CPUS);
        return 1;
    }

    /* 加载失败常见原因：权限不足（-EPERM）、verifier 拒绝（-EACCES，见正文排错节） */
    struct trace_stat_bpf *skel = trace_stat_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* tracepoint 挂载点可从 SEC("tracepoint/sched/sched_switch") 完整推断，
       无需像 XDP 那样显式传接口；失败返回负数错误码 */
    if (trace_stat_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 目标是否存在见 ../00-env/check-env.sh）\n");
        trace_stat_bpf__destroy(skel);
        return 1;
    }
    printf("已挂载 tracepoint/sched/sched_switch，每秒打印，Ctrl-C 退出。\n");

    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.sched_counts);

    while (!exiting) {
        __u64 total = 0;
        printf("--- 各 CPU 调度次数 ---\n");
        /* key 遍历 CPU 下标：只有 CPU c 写 key c，读回该 key 的 per-CPU 槽位中
           仅下标 c 非零——对全部槽位求和即该 CPU 的计数（per-CPU 读回语义见正文 5.2） */
        for (__u32 cpu = 0; cpu < MAX_CPUS; cpu++) {
            struct cpu_stat buf[MAX_CPUS] = {};
            if (bpf_map_lookup_elem(fd, &cpu, buf))
                continue;             /* 理论不发生：max_entries 覆盖全部 key */
            __u64 sum = 0;
            for (int i = 0; i < nr; i++)
                sum += buf[i].switches;
            if (sum)
                printf("cpu%-2u switches=%-10llu last_next_pid=%llu\n",
                       cpu, (unsigned long long)sum,
                       (unsigned long long)buf[cpu].last_next_pid);
            total += sum;
        }
        printf("total=%llu\n", (unsigned long long)total);
        sleep(1);
    }

    trace_stat_bpf__destroy(skel);   /* 关闭 fd + 解引用：引用计数归零，对象随之释放 */
    return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 用户态 loader：每 2 秒遍历 LRU 表、排序、打印被调度次数 top-N。
 * 用法: sudo ./top_proc [N]（N 缺省 10，教学上限 64）
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "top_proc.skel.h"

#define TOP_MAX 64

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

struct entry { __u32 pid; __u64 count; };

static int cmp_desc(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->count < y->count)
        return 1;
    if (x->count > y->count)
        return -1;
    return 0;
}

/* 弱遍历：LRU 表项可能在遍历期间被静默淘汰（与第 17 章阶段 3 口径一致），读到什么算什么 */
static int snapshot(int fd, struct entry *out, int max)
{
    __u32 cur = 0, next;          /* pid 0 永不入榜，可作遍历起点哨兵 */
    __u64 v;
    int n = 0;
    while (n < max && bpf_map_get_next_key(fd, &cur, &next) == 0) {
        if (bpf_map_lookup_elem(fd, &next, &v) == 0 && v > 0) {
            out[n].pid = next;
            out[n].count = v;
            n++;
        }
        cur = next;
    }
    return n;
}

int main(int argc, char **argv)
{
    int top_n = (argc > 1) ? atoi(argv[1]) : 10;
    if (top_n < 1 || top_n > TOP_MAX)
        top_n = 10;

    struct top_proc_bpf *skel = top_proc_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }
    if (top_proc_bpf__attach(skel)) {
        fprintf(stderr, "attach 失败（需 root；tracefs 见 ../../00-env/check-env.sh）\n");
        top_proc_bpf__destroy(skel);
        return 1;
    }
    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.proc_counts);

    while (!exiting) {
        struct entry e[TOP_MAX];
        int n = snapshot(fd, e, TOP_MAX);
        qsort(e, n, sizeof(e[0]), cmp_desc);
        printf("--- 每进程被调度次数 top-%d ---\n", top_n);
        for (int i = 0; i < n && i < top_n; i++)
            printf("%-7d %llu\n", e[i].pid, (unsigned long long)e[i].count);
        sleep(2);
    }

    top_proc_bpf__destroy(skel);
    return 0;
}

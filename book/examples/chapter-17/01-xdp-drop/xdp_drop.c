// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 1 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./xdp_drop -i <接口名>
 * 职责: open_and_load（加载+verifier 校验+JIT）-> attach XDP -> 每秒轮询 stats。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "xdp_drop.skel.h"

/* 与 BPF 侧 stats 映射的下标约定一致；skeleton 不会导出 BPF 源码中的枚举 */
enum { STAT_PASS = 0, STAT_DROP };

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }   /* 阶段 1–4 loader 共用 */

/* per-CPU map 单键求和：bpf_map_lookup_elem 一次返回 nr_cpus 份 value */
static __u64 sum_percpu(int fd, __u32 key)
{
    __u64 buf[64] = {};                 /* 教学示例假设 CPU <= 64 */
    int n = libbpf_num_possible_cpus();
    if (n > 64 || bpf_map_lookup_elem(fd, &key, buf))
        return 0;
    __u64 sum = 0;
    for (int i = 0; i < n; i++)
        sum += buf[i];
    return sum;
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_sig);    /* 注册退出信号：Ctrl-C 置位 exiting，
                                      轮询循环结束后统一销毁 skeleton */
    signal(SIGTERM, handle_sig);

    if (argc != 3 || strcmp(argv[1], "-i")) {
        fprintf(stderr, "用法: %s -i <接口名>\n", argv[0]);
        return 1;
    }
    int ifindex = if_nametoindex(argv[2]);
    if (!ifindex) {
        fprintf(stderr, "接口 %s 不存在: %s\n", argv[2], strerror(errno));
        return 1;
    }

    /* 加载 = 打开对象 -> verifier 校验 -> JIT；失败常见原因：权限不足、verifier 拒绝 */
    struct xdp_drop_bpf *skel = xdp_drop_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach */
    if (!bpf_program__attach_xdp(skel->progs.xdp_drop, ifindex)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        xdp_drop_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", argv[2], ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        printf("pass=%llu drop=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP));
        sleep(1);
    }

    xdp_drop_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}

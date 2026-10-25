// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点②用户态 loader：XDP 与 TC 同挂 lo，对照读数。
 * attach 选型：XDP 走 bpf_program__attach_xdp（返回 link）；TC 没有 link 对应物——
 * 走 netlink 的 qdisc/filter 模型（bpf_tc_hook_create 建 clsact、bpf_tc_attach 绑
 * ingress，沿第 17 章 04-nat-tc 先例）。flags=0 挂 lo 时内核一般自动回退 generic
 * （SKB）模式——具体行为随内核版本，以实测为准（运行级待验证）。
 * 失败模式：无特权 load 即 EPERM；clsact 已存在时 hook_create 返回 -EEXIST 视为成功。 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <net/if.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "xdp_count.skel.h"
#include "tc_count.skel.h"

static volatile sig_atomic_t exiting = 0;
static void on_sigint(int sig) { exiting = 1; }

/* libbpf 1.x：日志回调把 WARN/INFO 透传到 stderr（DEBUG 级滤除）——
 * libbpf_set_print 只接收回调函数指针，不能直接传级别常量（同 Task 2 教训）。 */
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level > LIBBPF_INFO)
        return 0;
    return vfprintf(stderr, format, args);
}

struct pkt_stats { __u64 packets; __u64 udp; };

static void print_stats(int map_fd, const char *tag)
{
    __u32 key = 0;
    struct pkt_stats v = {};
    if (bpf_map_lookup_elem(map_fd, &key, &v) == 0)
        printf("  [%s] packets=%-8llu udp=%-8llu\n", tag,
               (unsigned long long)v.packets, (unsigned long long)v.udp);
}

int main(void)
{
    struct xdp_count_bpf *xdp = NULL;
    struct tc_count_bpf *tc = NULL;
    struct bpf_link *xdp_link = NULL;
    unsigned int ifindex;
    int err = 1;

    signal(SIGINT, on_sigint);
    libbpf_set_print(libbpf_print_fn);

    ifindex = if_nametoindex("lo");   /* <net/if.h>；隐式声明告警的教训见第 3 章 */
    if (!ifindex) {
        fprintf(stderr, "lo 接口不可见\n");
        return 1;
    }

    xdp = xdp_count_bpf__open();
    tc = tc_count_bpf__open();
    if (!xdp || !tc) {
        fprintf(stderr, "skeleton open 失败\n");
        goto out;
    }
    if (xdp_count_bpf__load(xdp) || tc_count_bpf__load(tc)) {
        fprintf(stderr, "load 失败（无特权环境的预期失败）\n");
        goto out;
    }

    /* XDP 挂载：驱动层 hook，返回 bpf_link */
    xdp_link = bpf_program__attach_xdp(xdp->progs.xdp_entry, (int)ifindex);
    if (!xdp_link) {
        fprintf(stderr, "XDP attach 失败（需 root）\n");
        goto out;
    }

    /* TC 挂载：先建 clsact qdisc（已存在返回 -EEXIST，视为成功），再绑 ingress */
    {
        LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = ifindex,
                    .attach_point = BPF_TC_INGRESS);
        LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 1,
                    .prog_fd = bpf_program__fd(tc->progs.tc_entry));
        if (bpf_tc_hook_create(&hook) != 0 && errno != EEXIST) {
            fprintf(stderr, "clsact 创建失败（需 root）\n");
            goto out;
        }
        hook.attach_point = BPF_TC_INGRESS;
        if (bpf_tc_attach(&hook, &opts)) {
            fprintf(stderr, "TC attach 失败（需 root）\n");
            goto out;
        }
    }

    printf("XDP 与 TC 已同挂 lo，制造流量（如 ping 127.0.0.1、curl 本机服务）观察对照……\n");
    while (!exiting) {
        sleep(1);
        print_stats(bpf_map__fd(xdp->maps.xdp_stats), "xdp      ");
        print_stats(bpf_map__fd(tc->maps.tc_stats), "tc       ");
        printf("----\n");
    }
    err = 0;

out:
    {
        LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = ifindex,
                    .attach_point = BPF_TC_INGRESS);
        LIBBPF_OPTS(bpf_tc_opts, detach_opts, .handle = 1, .priority = 1);
        /* best-effort detach filter：须传填充 opts（NULL 会被 libbpf 拒绝，-EINVAL 静默 no-op），
         * handle/priority 与 attach 时一致（沿 ch17 04-nat-tc 先例）；filter 不在时 -ENOENT 属正常，
         * 返回值忽略；qdisc 留置无害 */
        bpf_tc_detach(&hook, &detach_opts);
    }
    bpf_link__destroy(xdp_link);
    xdp_count_bpf__destroy(xdp);
    tc_count_bpf__destroy(tc);
    return err;
}

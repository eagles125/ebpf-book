// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 3 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./conntrack -i <接口名>
 * 职责: open_and_load -> attach XDP -> 每 2 秒打印 7 项统计并 dump conntrack 表。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* inet_ntop、ntohs */
#include <time.h>       /* clock_gettime：与 bpf_ktime_get_ns 同源单调时钟 */
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "conntrack.skel.h"

/* skeleton 只导出 map/program 句柄、不导出 BPF 侧共享头（fw_common.h）中的类型；
 * 而 fw_common.h 依赖 vmlinux.h（内核 BTF），不能被用户态代码直接包含，
 * 故此处按契约逐字段镜像以下结构与枚举（改动任一侧都必须同步另一侧） */

/* 统计下标：与 BPF 侧 enum stat_idx（契约 C1）顺序一致，阶段 3 共 7 项 */
enum { STAT_TOTAL = 0, STAT_PASS, STAT_DROP_ACL, STAT_DROP_PARSE,
       STAT_CT_NEW, STAT_CT_ESTABLISHED, STAT_CT_FULL, STAT_MAX };

/* conntrack key/value：与 BPF 侧（契约 C2）一致，IP/端口一律网络字节序 */
struct ct_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

struct ct_value {
    __u64 last_seen_ns;
    __u64 packets;
    __u64 bytes;
    __u8  state;          /* enum ct_state */
    __u8  pad[7];
};

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

/* bpf_ktime_get_ns 基于 CLOCK_MONOTONIC；用户态取同源单调时钟对齐时间基准 */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* 遍历是「无快照保证」的弱遍历：遍历期间表项可能被 LRU 淘汰，读到啥打印啥 */
static void dump_conntrack(int fd)
{
    struct ct_key cur = {}, next;
    struct ct_value v;
    while (bpf_map_get_next_key(fd, &cur, &next) == 0) {
        if (bpf_map_lookup_elem(fd, &next, &v) == 0) {
            struct in_addr s = { .s_addr = next.src_ip }, d = { .s_addr = next.dst_ip };
            /* 不能在一条 printf 里调用两次 inet_ntoa：它返回线程内静态缓冲区，
             * 第二次调用会覆盖第一次的结果（两个 IP 打成同一个），
             * 故改用可重入的 inet_ntop 各自写入独立缓冲区 */
            char sstr[INET_ADDRSTRLEN], dstr[INET_ADDRSTRLEN];
            printf("%-15s:%-5u -> %-15s:%-5u proto=%u state=%u idle=%.1fs pkts=%llu\n",
                   inet_ntop(AF_INET, &s, sstr, sizeof(sstr)), ntohs(next.src_port),
                   inet_ntop(AF_INET, &d, dstr, sizeof(dstr)), ntohs(next.dst_port),
                   next.proto, v.state,
                   now_s() - v.last_seen_ns / 1e9,
                   (unsigned long long)v.packets);
        }
        cur = next;
    }
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_sig);    /* 注册退出信号：Ctrl-C 置位 exiting，
                                      轮询循环结束后统一销毁 skeleton */
    signal(SIGTERM, handle_sig);

    /* 阶段 3 没有规则下发，参数面只有 -i <接口名> */
    const char *ifname = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            ifname = argv[++i];
        } else {
            fprintf(stderr, "用法: %s -i <接口名>\n", argv[0]);
            return 1;
        }
    }
    if (!ifname) {
        fprintf(stderr, "缺少 -i <接口名>\n");
        return 1;
    }
    int ifindex = if_nametoindex(ifname);
    if (!ifindex) {
        fprintf(stderr, "接口 %s 不存在: %s\n", ifname, strerror(errno));
        return 1;
    }

    /* 加载 = 打开对象 -> verifier 校验 -> JIT；失败常见原因：权限不足、verifier 拒绝 */
    struct conntrack_bpf *skel = conntrack_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach。
       注意：libbpf 1.x attach 失败返回 ERR_PTR（非 NULL），必须用 libbpf_get_error 判错，
       仅判 !link 会漏掉真实失败 */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.conntrack_prog, ifindex);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        conntrack_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", ifname, ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    int ct_fd = bpf_map__fd(skel->maps.conntrack);
    while (!exiting) {
        /* 统计名与契约 C1 的展示名一致；drop_acl/drop_parse 阶段 3 恒 0（不判 ACL） */
        printf("total=%llu pass=%llu drop_acl=%llu drop_parse=%llu "
               "ct_new=%llu ct_established=%llu ct_full=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_TOTAL),
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_ACL),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_PARSE),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_NEW),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_ESTABLISHED),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_FULL));
        dump_conntrack(ct_fd);
        printf("\n");   /* 空行分隔每轮快照 */
        sleep(2);
    }

    conntrack_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}

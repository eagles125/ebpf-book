// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 04 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./ratelimit -i <接口名> [--limit 5201,2000] [--limit 53,100,500]
 * 职责: 解析 --limit <port>,<pps>[,<burst>]（可重复；burst 缺省 = rate，即 1 秒突发）
 *       -> open_and_load -> 下发限速配置到 rate_cfg -> attach XDP -> 每 2 秒轮询 stats。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* htons */
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "ratelimit.skel.h"

/* skeleton 只导出 map/program 句柄、不导出 BPF 侧共享头（fw_common.h）中的类型；
 * 而 fw_common.h 依赖 vmlinux.h（内核 BTF），不能被用户态代码直接包含，
 * 故此处按契约逐字段镜像以下结构与枚举（改动任一侧都必须同步另一侧） */

/* 统计下标：与 BPF 侧 enum stat_idx（契约 C1）顺序一致；
 * 本练习把 STAT_DROP_ACL 下标的展示名记为 drop_rl（语义 = 限速超额丢弃） */
enum { STAT_TOTAL = 0, STAT_PASS, STAT_DROP_ACL, STAT_DROP_PARSE, STAT_MAX };

/* 限速配置：与 BPF 侧 struct rl_cfg（练习 04 契约）一致 */
struct rl_cfg {
    __u64 rate_pps;    /* 每秒允许报文数（0 = 不限速） */
    __u64 burst;       /* 桶容量（报文数），吸收突发 */
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

/* 命令行限速配置缓存：参数阶段就解析（尽早发现格式错误，非 root 也能自查拼写），
 * open_and_load 成功后再统一下发——map 的 fd 在加载后才存在。
 * 上限与 BPF 侧 RL_MAX_PORTS 对应（教学从简，各写一份常量） */
#define MAX_CLI_LIMITS 256
static __u16 limit_keys[MAX_CLI_LIMITS];
static struct rl_cfg limit_vals[MAX_CLI_LIMITS];
static int n_limits;

/* 解析 --limit <port>,<pps>[,<burst>]（burst 缺省 = rate，即 1 秒突发额度）。
 * key 用 htons(port)：数据面从报文里取的 dst_port 是网络字节序，
 * 两侧「原样存原样查」才能对上（五元组同款字节序契约） */
static int parse_limit_arg(const char *s, __u16 *key, struct rl_cfg *v)
{
    unsigned port;
    unsigned long long rate, burst;
    memset(v, 0, sizeof(*v));
    int n = sscanf(s, "%u,%llu,%llu", &port, &rate, &burst);
    if (n == 2)
        burst = rate;                    /* 第三个字段缺省：1 秒突发 */
    else if (n != 3)
        return -1;                       /* 字段数不足 */
    if (port > 65535)
        return -1;
    /* 1e8 pps/burst 已远超真实教学场景；同时保证 (burst << 10) 定点化不溢出，
     * 并让 (elapsed * rate) 落在 .bpf.c 注释给出的量级分析范围内 */
    if (rate > 100000000ULL || burst > 100000000ULL)
        return -1;
    *key = htons((__u16)port);
    v->rate_pps = rate;
    v->burst = burst;
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_sig);    /* 注册退出信号：Ctrl-C 置位 exiting，
                                      轮询循环结束后统一销毁 skeleton */
    signal(SIGTERM, handle_sig);

    /* 第 1 步：解析 -i 与全部 --limit（可重复） */
    const char *ifname = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            ifname = argv[++i];
        } else if (!strcmp(argv[i], "--limit") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_limits == MAX_CLI_LIMITS) {
                fprintf(stderr, "--limit 条数超过命令行上限 %d\n", MAX_CLI_LIMITS);
                return 1;
            }
            if (parse_limit_arg(spec, &limit_keys[n_limits], &limit_vals[n_limits])) {
                fprintf(stderr, "--limit 格式错误: %s\n"
                        "  格式: port,pps[,burst]（端口 0-65535；pps=0 表示不限速；"
                        "burst 缺省 = pps，如 --limit 5201,2000 或 --limit 53,100,500）\n", spec);
                return 1;
            }
            n_limits++;
        } else {
            fprintf(stderr, "用法: %s -i <接口名> [--limit 5201,2000] [--limit 53,100,500]\n"
                    "  --limit <port>,<pps>[,<burst>] 可重复；只写 -i 则不限速、全部放行\n", argv[0]);
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
    struct ratelimit_bpf *skel = ratelimit_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* 第 2 步：遍历缓存的限速配置写入 rate_cfg（令牌桶状态 rate_state 由数据面维护）；
     * 失败常见原因：超过 map 的 max_entries */
    for (int i = 0; i < n_limits; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.rate_cfg),
                                &limit_keys[i], &limit_vals[i], BPF_ANY)) {
            fprintf(stderr, "限速配置下发失败（条数超上限或格式错误）: --limit #%d\n", i);
            ratelimit_bpf__destroy(skel);
            return 1;
        }
    }
    printf("已下发限速配置 %d 条（pps=0 表示不限速）。\n", n_limits);

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach。
       注意：libbpf 1.x attach 失败返回 ERR_PTR（非 NULL），必须用 libbpf_get_error 判错，
       仅判 !link 会漏掉真实失败 */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.ratelimit_prog, ifindex);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        ratelimit_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", ifname, ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        printf("total=%llu pass=%llu drop_rl=%llu drop_parse=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_TOTAL),
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_ACL),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_PARSE));
        sleep(2);   /* 每 2 秒刷新一次 */
    }

    ratelimit_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 01 用户态 loader（libbpf-bootstrap 风格，阶段 2 loader 的 IPv6 扩展版）。
 * 用法: sudo ./acl -i <接口名>
 *       [--rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201] [--cidr drop,10.0.0.0/24]
 *       [--rule6 drop,tcp,2001:db8::2,5201,2001:db8:2::2,5201] [--cidr6 drop,2001:db8::/32]
 * 职责: open_and_load -> 下发 --rule/--cidr/--rule6/--cidr6 规则到对应 map -> attach XDP
 *       -> 每 2 秒轮询 stats。
 * 注意: v4 参数与阶段 2 完全兼容；非 root（unprivileged_bpf_disabled=2）时
 *       open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>     /* atoi */
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* inet_pton（AF_INET/AF_INET6） */
#include <netinet/in.h> /* htons、IPPROTO_TCP/IPPROTO_UDP */
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "acl.skel.h"

/* skeleton 只导出 map/program 句柄、不导出 BPF 侧共享头（fw_common.h）中的类型；
 * 而 fw_common.h 依赖 vmlinux.h（内核 BTF），不能被用户态代码直接包含，
 * 故此处按契约逐字段镜像以下结构与枚举（改动任一侧都必须同步另一侧） */

/* 统计下标：与 BPF 侧 enum stat_idx（契约 C1）顺序一致 */
enum { STAT_TOTAL = 0, STAT_PASS, STAT_DROP_ACL, STAT_DROP_PARSE, STAT_MAX };

/* ---- 阶段 2 的 v4 结构（原样保留） ---- */

/* 五元组 key：与 BPF 侧 struct flow_key（契约 C2）一致，一律网络字节序 */
struct flow_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

/* 动作与 value：与 BPF 侧 enum fw_action / struct rule_value 一致 */
enum { FW_ALLOW = 0, FW_DROP = 1 };
struct rule_value {
    __u8 action;
    __u8 pad[7];
};

/* LPM trie 的 key/value：与 BPF 侧 struct lpm_key / struct lpm_value 一致 */
struct lpm_key {
    __u32 prefixlen;
    __be32 addr;
};
struct lpm_value {
    __u8 action;
    __u8 pad[7];
};

/* ---- 练习 01 新增：IPv6 结构镜像（与 BPF 侧 v6 结构逐字段一致） ---- */

/* IPv6 五元组 key：128 位地址按 4 个 32 位字原样存储，一律网络字节序 */
struct flow_key6 {
    __be32 src_ip[4];
    __be32 dst_ip[4];
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;        /* 仅 6(TCP)/17(UDP) */
    __u8   pad[3];
};
struct rule_value6 {
    __u8 action;
    __u8 pad[7];
};
/* IPv6 LPM key：前缀长度 0-128 */
struct lpm_key6 {
    __u32 prefixlen;
    __be32 addr[4];
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

/* 命令行规则缓存：参数阶段就解析（尽早发现格式错误，非 root 也能自查拼写），
 * open_and_load 成功后再统一下发——map 的 fd 在加载后才存在 */
#define MAX_CLI_RULES 64    /* 命令行条数上限（教学从简）；v4/v6 各自独立计数，map 上限见 fw_common.h */
static struct flow_key  rule_keys[MAX_CLI_RULES];
static struct rule_value rule_vals[MAX_CLI_RULES];
static struct lpm_key   lpm_keys[MAX_CLI_RULES];
static struct lpm_value lpm_vals[MAX_CLI_RULES];
static struct flow_key6 rule6_keys[MAX_CLI_RULES];
static struct rule_value6 rule6_vals[MAX_CLI_RULES];
static struct lpm_key6  lpm6_keys[MAX_CLI_RULES];
static struct rule_value6 lpm6_vals[MAX_CLI_RULES];
static int n_rules, n_cidrs, n_rules6, n_cidrs6;

/* 解析 --rule <action>,<proto>,<src>,<sport>,<dst>,<dport>（IPv4 精确五元组，与阶段 2 一致） */
static int parse_rule_arg(const char *s, struct flow_key *k, struct rule_value *v)
{
    char action[8], proto[8], src[64], dst[64];
    unsigned sport, dport;
    memset(k, 0, sizeof(*k));           /* pad 与端口一并清零，哈希比较行为确定 */
    memset(v, 0, sizeof(*v));
    /* %7[^,]/%63[^,] 是带宽度的扫描集：最多写 7/63 字符 + '\0'，不会越界 */
    if (sscanf(s, "%7[^,],%7[^,],%63[^,],%u,%63[^,],%u",
               action, proto, src, &sport, dst, &dport) != 6)
        return -1;
    if (sport > 65535 || dport > 65535)
        return -1;
    if (inet_pton(AF_INET, src, &k->src_ip) != 1 ||
        inet_pton(AF_INET, dst, &k->dst_ip) != 1)
        return -1;                       /* inet_pton 得到的已是网络字节序 */
    k->src_port = htons(sport);          /* 主机序 -> 网络序，与内核侧读取的报文端口一致 */
    k->dst_port = htons(dport);
    /* 教学从简：proto 只认 tcp/udp，其余取值一律按 UDP 处理 */
    k->proto = strcmp(proto, "tcp") ? IPPROTO_UDP : IPPROTO_TCP;
    /* 教学从简：action 只认 drop，其余取值一律按 allow 处理 */
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}

/* 解析 --cidr <action>,<cidr>（IPv4 源地址网段，写 acl_lpm；与阶段 2 一致） */
static int parse_cidr_arg(const char *s, struct lpm_key *k, struct lpm_value *v)
{
    char action[8], cidr[64], *slash;
    memset(k, 0, sizeof(*k));
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%7[^,],%63s", action, cidr) != 2)
        return -1;
    if ((slash = strchr(cidr, '/')) == NULL)
        return -1;                       /* 缺 '/'：不是 CIDR 记法 */
    *slash = '\0';                       /* 拆出地址与前缀长度两段 */
    k->prefixlen = (unsigned char)atoi(slash + 1);
    if (k->prefixlen > 32 || inet_pton(AF_INET, cidr, &k->addr) != 1)
        return -1;                       /* 前缀长度超界或地址非法（/256 会被截断成 /0，需输入 0-32） */
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}

/* 解析 --rule6 <action>,<proto>,<src>,<sport>,<dst>,<dport>（IPv6 精确五元组，练习 01 新增） */
static int parse_rule6_arg(const char *s, struct flow_key6 *k, struct rule_value6 *v)
{
    char action[8], proto[8], src[64], dst[64];
    unsigned sport, dport;
    memset(k, 0, sizeof(*k));           /* pad 与端口一并清零，哈希比较行为确定 */
    memset(v, 0, sizeof(*v));
    /* IPv6 字面量不含逗号，扫描集写法与 v4 相同即可安全截取 */
    if (sscanf(s, "%7[^,],%7[^,],%63[^,],%u,%63[^,],%u",
               action, proto, src, &sport, dst, &dport) != 6)
        return -1;
    if (sport > 65535 || dport > 65535)
        return -1;
    /* inet_pton(AF_INET6) 一次写满 16 字节，恰好落进 src_ip[4]/dst_ip[4] */
    if (inet_pton(AF_INET6, src, k->src_ip) != 1 ||
        inet_pton(AF_INET6, dst, k->dst_ip) != 1)
        return -1;
    k->src_port = htons(sport);
    k->dst_port = htons(dport);
    k->proto = strcmp(proto, "tcp") ? IPPROTO_UDP : IPPROTO_TCP;   /* 与 v4 同口径 */
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}

/* 解析 --cidr6 <action>,<cidr>（IPv6 源地址网段，写 acl_lpm6，前缀 0-128，练习 01 新增） */
static int parse_cidr6_arg(const char *s, struct lpm_key6 *k, struct rule_value6 *v)
{
    char action[8], cidr[64], *slash;
    memset(k, 0, sizeof(*k));
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%7[^,],%63s", action, cidr) != 2)
        return -1;
    if ((slash = strchr(cidr, '/')) == NULL)
        return -1;
    *slash = '\0';
    /* v6 侧对前缀长度做显式范围检查（0-128）：v4 侧截断式取值会把 /300 截成 /44，
       这里改为超界直接拒绝，避免静默错配 */
    int plen = atoi(slash + 1);
    if (plen < 0 || plen > 128 || inet_pton(AF_INET6, cidr, k->addr) != 1)
        return -1;
    k->prefixlen = (__u32)plen;
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_sig);    /* 注册退出信号：Ctrl-C 置位 exiting，
                                      轮询循环结束后统一销毁 skeleton */
    signal(SIGTERM, handle_sig);

    /* 第 1 步：解析 -i 与全部 --rule/--cidr/--rule6/--cidr6（均可重复） */
    const char *ifname = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            ifname = argv[++i];
        } else if (!strcmp(argv[i], "--rule") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_rules == MAX_CLI_RULES) {
                fprintf(stderr, "--rule 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_rule_arg(spec, &rule_keys[n_rules], &rule_vals[n_rules])) {
                fprintf(stderr, "--rule 格式错误: %s\n"
                        "  格式: action,proto,src,sport,dst,dport"
                        "（action=allow|drop，proto=tcp|udp，端口 0-65535，不支持通配）\n", spec);
                return 1;
            }
            n_rules++;
        } else if (!strcmp(argv[i], "--cidr") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_cidrs == MAX_CLI_RULES) {
                fprintf(stderr, "--cidr 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_cidr_arg(spec, &lpm_keys[n_cidrs], &lpm_vals[n_cidrs])) {
                fprintf(stderr, "--cidr 格式错误: %s\n"
                        "  格式: action,<cidr>（action=allow|drop，如 drop,10.0.0.0/24，前缀长度 0-32）\n", spec);
                return 1;
            }
            n_cidrs++;
        } else if (!strcmp(argv[i], "--rule6") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_rules6 == MAX_CLI_RULES) {
                fprintf(stderr, "--rule6 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_rule6_arg(spec, &rule6_keys[n_rules6], &rule6_vals[n_rules6])) {
                fprintf(stderr, "--rule6 格式错误: %s\n"
                        "  格式: action,proto,src,sport,dst,dport"
                        "（IPv6 地址，如 drop,tcp,2001:db8::2,5201,2001:db8:2::2,5201）\n", spec);
                return 1;
            }
            n_rules6++;
        } else if (!strcmp(argv[i], "--cidr6") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_cidrs6 == MAX_CLI_RULES) {
                fprintf(stderr, "--cidr6 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_cidr6_arg(spec, &lpm6_keys[n_cidrs6], &lpm6_vals[n_cidrs6])) {
                fprintf(stderr, "--cidr6 格式错误: %s\n"
                        "  格式: action,<cidr>（如 drop,2001:db8::/32，前缀长度 0-128）\n", spec);
                return 1;
            }
            n_cidrs6++;
        } else {
            fprintf(stderr, "用法: %s -i <接口名> "
                    "[--rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201] [--cidr drop,10.0.0.0/24] "
                    "[--rule6 drop,tcp,2001:db8::2,5201,2001:db8:2::2,5201] [--cidr6 drop,2001:db8::/32]\n"
                    "  --rule/--cidr/--rule6/--cidr6 可重复；只写 -i 则不下发规则、全部放行\n", argv[0]);
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
    struct acl_bpf *skel = acl_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* 第 2 步：遍历缓存的规则写入 v4/v6 各自的 map（互不混用）；
     * 失败常见原因：超过 map 的 max_entries */
    for (int i = 0; i < n_rules; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.acl_rules),
                                &rule_keys[i], &rule_vals[i], BPF_ANY)) {
            fprintf(stderr, "规则下发失败（规则数超上限或格式错误）: --rule #%d\n", i);
            acl_bpf__destroy(skel);
            return 1;
        }
    }
    for (int i = 0; i < n_cidrs; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.acl_lpm),
                                &lpm_keys[i], &lpm_vals[i], BPF_ANY)) {
            fprintf(stderr, "规则下发失败（规则数超上限或格式错误）: --cidr #%d\n", i);
            acl_bpf__destroy(skel);
            return 1;
        }
    }
    for (int i = 0; i < n_rules6; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.acl_rules6),
                                &rule6_keys[i], &rule6_vals[i], BPF_ANY)) {
            fprintf(stderr, "规则下发失败（规则数超上限或格式错误）: --rule6 #%d\n", i);
            acl_bpf__destroy(skel);
            return 1;
        }
    }
    for (int i = 0; i < n_cidrs6; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.acl_lpm6),
                                &lpm6_keys[i], &lpm6_vals[i], BPF_ANY)) {
            fprintf(stderr, "规则下发失败（规则数超上限或格式错误）: --cidr6 #%d\n", i);
            acl_bpf__destroy(skel);
            return 1;
        }
    }
    printf("已下发规则：精确五元组 v4 %d 条、v6 %d 条；源地址网段 v4 %d 条、v6 %d 条。\n",
           n_rules, n_rules6, n_cidrs, n_cidrs6);

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach。
       注意：libbpf 1.x attach 失败返回 ERR_PTR（非 NULL），必须用 libbpf_get_error 判错，
       仅判 !link 会漏掉真实失败 */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.acl_prog, ifindex);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        acl_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", ifname, ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        printf("total=%llu pass=%llu drop_acl=%llu drop_parse=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_TOTAL),
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_ACL),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_PARSE));
        sleep(2);   /* 每 2 秒刷新一次 */
    }

    acl_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}

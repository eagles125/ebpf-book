// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 4 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./nat -i <DNAT入口接口> -e <SNAT出口接口>
 *       [--dnat tcp,10.0.1.1,8080,10.0.2.2,8080] [--snat 10.0.0.2,10.0.2.1]
 * 职责: open_and_load -> 下发 --dnat/--snat 规则到 nat_map -> TC attach
 *       （-i 接口 ingress 挂 nat_ingress 做 DNAT，-e 接口 egress 挂 nat_egress 做 SNAT）
 *       -> 每 2 秒打印 8 项统计；退出时显式 detach（TC filter 不随进程退出自动卸载）。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* inet_pton */
#include <netinet/in.h> /* htons、IPPROTO_TCP/IPPROTO_UDP */
#include <bpf/libbpf.h> /* LIBBPF_OPTS / bpf_tc_* */
#include <bpf/bpf.h>
#include "nat.skel.h"

/* skeleton 只导出 map/program 句柄、不导出 BPF 侧共享头（fw_common.h）中的类型；
 * 而 fw_common.h 依赖 vmlinux.h（内核 BTF），不能被用户态代码直接包含，
 * 故此处按契约逐字段镜像以下结构与枚举（改动任一侧都必须同步另一侧） */

/* 统计下标：与 BPF 侧 enum stat_idx（契约 C1）顺序一致，阶段 4 共 8 项 */
enum { STAT_TOTAL = 0, STAT_PASS, STAT_DROP_ACL, STAT_DROP_PARSE,
       STAT_CT_NEW, STAT_CT_ESTABLISHED, STAT_CT_FULL, STAT_NAT, STAT_MAX };

/* NAT key/value：与 BPF 侧（契约 C2）一致，IP/端口一律网络字节序；
 * key 中为 0 的字段是通配维度（数据面精确未命中后清零侧字段二次查找命中） */
#define NAT_MAX_ENTRIES 4096    /* 与 BPF 侧 fw_common.h 一致，报错信息引用 */
struct nat_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

/* is_snat：1=改源（SNAT，egress）0=改目的（DNAT，ingress）；
 * 数据面用 dir 过滤：DNAT 程序只认 is_snat==0 的表项，反之亦然 */
struct nat_value {
    __be32 new_addr;
    __be16 new_port;      /* 0 = 只改地址不改端口（SNAT 场景） */
    __u8   is_snat;
    __u8   pad;
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
#define MAX_CLI_RULES 64    /* 命令行条数上限（教学从简）；map 上限见 fw_common.h */
static struct nat_key  nat_keys[MAX_CLI_RULES];
static struct nat_value nat_vals[MAX_CLI_RULES];
static int n_nat, n_dnat, n_snat;

/* 解析 --dnat <proto>,<原目的IP>,<原目的端口>,<新IP>,<新端口>
 * 写通配 key：src_ip/src_port 置 0（源侧通配），目的侧精确——数据面 DNAT
 * 二次查找时清零的正是源侧字段，两侧必须按同一约定填 0 */
static int parse_dnat_arg(const char *s, struct nat_key *k, struct nat_value *v)
{
    char proto[8], dst[64], new_ip[64];
    unsigned dport, new_port;
    memset(k, 0, sizeof(*k));           /* 源侧与 pad 一并清零 = 通配维度 */
    memset(v, 0, sizeof(*v));
    /* %7[^,]/%63[^,] 是带宽度的扫描集：最多写 7/63 字符 + '\0'，不会越界 */
    if (sscanf(s, "%7[^,],%63[^,],%u,%63[^,],%u",
               proto, dst, &dport, new_ip, &new_port) != 5)
        return -1;
    if (dport > 65535 || new_port > 65535)
        return -1;
    if (inet_pton(AF_INET, dst, &k->dst_ip) != 1 ||
        inet_pton(AF_INET, new_ip, &v->new_addr) != 1)
        return -1;                       /* inet_pton 得到的已是网络字节序 */
    k->dst_port = htons(dport);          /* 主机序 -> 网络序，与内核侧读取的报文端口一致 */
    /* 教学从简：proto 只认 tcp/udp，其余取值一律按 UDP 处理 */
    k->proto = strcmp(proto, "tcp") ? IPPROTO_UDP : IPPROTO_TCP;
    v->new_port = htons(new_port);       /* 新端口 0 = 只改地址不改端口 */
    v->is_snat = 0;                      /* DNAT 表项：nat_ingress（dir=0）只认它 */
    return 0;
}

/* 解析 --snat <原源IP>,<新IP>（固定口径不带 proto：复用 --dnat 的协议）
 * 写通配 key：dst_ip/dst_port/src_port 置 0，源 IP 精确——数据面 SNAT
 * 二次查找清零目的侧与源端口（源端口是内核随机分配的临时值，静态规则写不出） */
static int parse_snat_arg(const char *s, struct nat_key *k, struct nat_value *v, __u8 proto)
{
    char src[64], new_ip[64];
    memset(k, 0, sizeof(*k));           /* 目的侧/源端口与 pad 一并清零 = 通配维度 */
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%63[^,],%63[^,]", src, new_ip) != 2)
        return -1;
    if (inet_pton(AF_INET, src, &k->src_ip) != 1 ||
        inet_pton(AF_INET, new_ip, &v->new_addr) != 1)
        return -1;
    k->proto = proto;                    /* 同 --dnat 的协议（流量过 DNAT 后协议不变） */
    v->new_port = 0;                     /* 只改地址不改端口 */
    v->is_snat = 1;                      /* SNAT 表项：nat_egress（dir=1）只认它 */
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_sig);    /* 注册退出信号：Ctrl-C 置位 exiting，
                                      轮询循环结束后统一 detach + 销毁 skeleton */
    signal(SIGTERM, handle_sig);

    /* 第 1 步：解析 -i/-e 与全部 --dnat/--snat（均可重复）。
       --snat 不带协议字段，固定口径复用第一条 --dnat 的 proto，故必须先给 --dnat */
    const char *ifname_in = NULL, *ifname_eg = NULL;
    __u8 snat_proto = 0;
    int have_dnat_proto = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            ifname_in = argv[++i];
        } else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
            ifname_eg = argv[++i];
        } else if (!strcmp(argv[i], "--dnat") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (n_nat == MAX_CLI_RULES) {
                fprintf(stderr, "--dnat/--snat 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_dnat_arg(spec, &nat_keys[n_nat], &nat_vals[n_nat])) {
                fprintf(stderr, "--dnat 格式错误: %s\n"
                        "  格式: proto,原目的IP,原目的端口,新IP,新端口"
                        "（proto=tcp|udp，端口 0-65535，新端口 0 表示只改地址）\n", spec);
                return 1;
            }
            if (!have_dnat_proto) {      /* --snat 复用第一条 --dnat 的协议 */
                snat_proto = nat_keys[n_nat].proto;
                have_dnat_proto = 1;
            }
            n_nat++; n_dnat++;
        } else if (!strcmp(argv[i], "--snat") && i + 1 < argc) {
            const char *spec = argv[++i];
            if (!have_dnat_proto) {
                fprintf(stderr, "--snat 需要先给 --dnat（--snat 不带协议字段，"
                        "固定口径复用 --dnat 的 proto）\n");
                return 1;
            }
            if (n_nat == MAX_CLI_RULES) {
                fprintf(stderr, "--dnat/--snat 条数超过命令行上限 %d\n", MAX_CLI_RULES);
                return 1;
            }
            if (parse_snat_arg(spec, &nat_keys[n_nat], &nat_vals[n_nat], snat_proto)) {
                fprintf(stderr, "--snat 格式错误: %s\n"
                        "  格式: 原源IP,新IP（协议复用 --dnat；只改源地址不改端口）\n", spec);
                return 1;
            }
            n_nat++; n_snat++;
        } else {
            fprintf(stderr, "用法: %s -i <DNAT入口接口> -e <SNAT出口接口> "
                    "[--dnat tcp,10.0.1.1,8080,10.0.2.2,8080] [--snat 10.0.0.2,10.0.2.1]\n"
                    "  -i 接口 ingress 挂 nat_ingress（DNAT）；-e 接口 egress 挂 nat_egress（SNAT）\n"
                    "  --dnat/--snat 可重复；--snat 复用 --dnat 的 proto，须先给 --dnat\n", argv[0]);
            return 1;
        }
    }
    if (!ifname_in || !ifname_eg) {
        fprintf(stderr, "缺少 -i <DNAT入口接口> / -e <SNAT出口接口>\n");
        return 1;
    }
    int ifindex_in = if_nametoindex(ifname_in);
    int ifindex_eg = if_nametoindex(ifname_eg);
    if (!ifindex_in || !ifindex_eg) {
        fprintf(stderr, "接口不存在: %s 或 %s: %s\n",
                ifname_in, ifname_eg, strerror(errno));
        return 1;
    }

    /* 加载 = 打开对象 -> verifier 校验 -> JIT；失败常见原因：权限不足、verifier 拒绝 */
    struct nat_bpf *skel = nat_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* 第 2 步：遍历缓存的规则写入 nat_map；失败常见原因：超过 map 的 max_entries */
    for (int i = 0; i < n_nat; i++) {
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.nat_map),
                                &nat_keys[i], &nat_vals[i], BPF_ANY)) {
            fprintf(stderr, "NAT 规则下发失败（超上限 %d 条或内存不足）: #%d\n",
                    NAT_MAX_ENTRIES, i);
            nat_bpf__destroy(skel);
            return 1;
        }
    }
    printf("已下发 NAT 规则：DNAT %d 条，SNAT %d 条。\n", n_dnat, n_snat);

    /* 第 3 步：TC attach。XDP 的 attach_xdp 返回 link 对象，TC 没有对应物——
     * TC 走 netlink 的 qdisc/filter 模型：先 bpf_tc_hook_create 在接口上创建
     * clsact qdisc（已存在时返回 -EEXIST，视为成功），再 bpf_tc_attach 把程序
     * 绑到具体方向（ingress/egress）。注意 TC 程序不能靠 skeleton __attach 自动挂载 */
    LIBBPF_OPTS(bpf_tc_hook, hook_in, .ifindex = ifindex_in, .attach_point = BPF_TC_INGRESS);
    if (bpf_tc_hook_create(&hook_in) != 0 && errno != EEXIST) {
        fprintf(stderr, "创建 clsact（%s）失败: %s\n", ifname_in, strerror(errno));
        nat_bpf__destroy(skel);
        return 1;
    }
    /* handle/priority 是 filter 的定位键（1/1 与 2/1 分属两个接口互不冲突） */
    LIBBPF_OPTS(bpf_tc_opts, opt_in, .handle = 1, .priority = 1,
                .prog_fd = bpf_program__fd(skel->progs.nat_ingress));
    if (bpf_tc_attach(&hook_in, &opt_in)) {
        fprintf(stderr, "attach nat_ingress 到 %s ingress 失败: %s"
                "（同 handle/priority 已被占用？先 tc filter del dev %s ingress）\n",
                ifname_in, strerror(errno), ifname_in);
        nat_bpf__destroy(skel);
        return 1;
    }

    LIBBPF_OPTS(bpf_tc_hook, hook_eg, .ifindex = ifindex_eg, .attach_point = BPF_TC_EGRESS);
    if (bpf_tc_hook_create(&hook_eg) != 0 && errno != EEXIST) {
        fprintf(stderr, "创建 clsact（%s）失败: %s\n", ifname_eg, strerror(errno));
        bpf_tc_detach(&hook_in, &opt_in);
        nat_bpf__destroy(skel);
        return 1;
    }
    LIBBPF_OPTS(bpf_tc_opts, opt_eg, .handle = 2, .priority = 1,
                .prog_fd = bpf_program__fd(skel->progs.nat_egress));
    if (bpf_tc_attach(&hook_eg, &opt_eg)) {
        fprintf(stderr, "attach nat_egress 到 %s egress 失败: %s"
                "（同 handle/priority 已被占用？先 tc filter del dev %s egress）\n",
                ifname_eg, strerror(errno), ifname_eg);
        bpf_tc_detach(&hook_in, &opt_in);
        nat_bpf__destroy(skel);
        return 1;
    }
    printf("TC 已挂载：%s ingress <- nat_ingress（DNAT），%s egress <- nat_egress（SNAT）。"
           "Ctrl-C 退出。\n", ifname_in, ifname_eg);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        /* 统计名与契约 C1 的展示名一致；阶段 4 数据面只维护 nat（改写成功计数），
         * total/pass 等 7 项属于阶段 2/3/5 的数据面，本阶段恒 0，仍按契约全量打印 */
        printf("total=%llu pass=%llu drop_acl=%llu drop_parse=%llu "
               "ct_new=%llu ct_established=%llu ct_full=%llu nat=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_TOTAL),
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_ACL),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP_PARSE),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_NEW),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_ESTABLISHED),
               (unsigned long long)sum_percpu(stats_fd, STAT_CT_FULL),
               (unsigned long long)sum_percpu(stats_fd, STAT_NAT));
        sleep(2);   /* 每 2 秒刷新一次 */
    }

    /* TC filter 持有程序引用、不随进程退出自动卸载（与 XDP link 对象的生命周期不同），
     * 退出前必须显式 detach，否则重跑时同 handle/priority 的 attach 会报 EEXIST */
    bpf_tc_detach(&hook_in, &opt_in);
    bpf_tc_detach(&hook_eg, &opt_eg);
    nat_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}

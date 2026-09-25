// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 2 数据面：Ethernet/IPv4/TCP/UDP 解析 + ACL 判定。
 * 查找优先级（正文与 README 必须同口径）：
 *   1) acl_rules  精确五元组（最具体） -> 命中即返回其 action；
 *   2) acl_lpm    源地址 CIDR 最长前缀匹配（黑/白名单由 action 区分）；
 *   3) 默认 FW_ALLOW（教学基线；生产默认策略应可配置）。
 */
#include "fw_common.h"

/* ETH_P_IP 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP 0x0800

/* hash：key 全等匹配，O(1)；适合精确封禁，不适合网段/端口范围 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key);
    __type(value, struct rule_value);
} acl_rules SEC(".maps");

/* LPM trie：按最长前缀匹配网段；BPF_F_NO_PREALLOC 必需（节省内存、允许大 max_entries） */
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, MAX_LPM_KEYS);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct lpm_key);
    __type(value, struct lpm_value);
} acl_lpm SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

/*
 * 逐层解析：每访问一层头之前都做一次边界检查。
 * verifier 把 (eth+1) > data_end 这类比较当作「指针定界」证据；少写一次就加载失败。
 * 成功填充 *key 并返回 1；非 IPv4/TCP/UDP 或残包返回 0。
 */
static __always_inline int parse_flow(struct xdp_md *ctx, struct flow_key *key)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return 0;                       /* ARP/IPv6 等不在本章策略范围 */

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return 0;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return 0;

    /* IPv4 头长以 4 字节为单位（ihl 最小 5=20B）；跳过可选字段前必须重查边界 */
    __u32 ihl_len = ip->ihl * 4;
    if (ihl_len < sizeof(*ip))
        return 0;
    void *l4 = (void *)ip + ihl_len;

    key->src_ip  = ip->saddr;           /* 网络字节序原样保存 */
    key->dst_ip  = ip->daddr;
    key->proto   = ip->protocol;
    key->pad[0] = key->pad[1] = key->pad[2] = 0;

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end)
            return 0;
        key->src_port = tcp->source;
        key->dst_port = tcp->dest;
    } else {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end)
            return 0;
        key->src_port = udp->source;
        key->dst_port = udp->dest;
    }
    return 1;
}

static __always_inline __u8 acl_verdict(const struct flow_key *flow)
{
    struct rule_value *exact = bpf_map_lookup_elem(&acl_rules, flow);
    if (exact)
        return exact->action;

    /* LPM 查找 key 的 prefixlen 固定 32：按主机地址查，由 trie 返回最长前缀规则 */
    struct lpm_key lk = { .prefixlen = 32, .addr = flow->src_ip };
    struct lpm_value *lv = bpf_map_lookup_elem(&acl_lpm, &lk);
    if (lv)
        return lv->action;

    return FW_ALLOW;
}

SEC("xdp")
int acl_prog(struct xdp_md *ctx)
{
    struct flow_key flow;
    if (!parse_flow(ctx, &flow)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    if (acl_verdict(&flow) == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

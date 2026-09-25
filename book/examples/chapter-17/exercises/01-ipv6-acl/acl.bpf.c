// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 01 数据面：Ethernet/IPv4/IPv6/TCP/UDP 解析 + ACL 判定（阶段 2 的 IPv6 扩展版）。
 * 查找优先级（正文与 README 必须同口径，v4/v6 各查各的 map，互不混用）：
 *   IPv4：1) acl_rules  精确五元组 -> 2) acl_lpm  源地址 CIDR -> 3) 默认 FW_ALLOW；
 *   IPv6：1) acl_rules6 精确五元组 -> 2) acl_lpm6 源地址 CIDR -> 3) 默认 FW_ALLOW。
 */
#include "fw_common.h"

/* ETH_P_IP/ETH_P_IPV6 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它们——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP   0x0800
#define ETH_P_IPV6 0x86DD

/* ---- 阶段 2 的 v4 两级 ACL 映射（原样保留） ---- */

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

/* ---- 练习 01 新增：v6 两级 ACL 映射（与 v4 同构，key 换成 IPv6 版） ---- */

/* hash：key 为 128 位地址版五元组 flow_key6，全等匹配，O(1) */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key6);
    __type(value, struct rule_value6);
} acl_rules6 SEC(".maps");

/* LPM trie：key 前缀长度 0-128（IPv6 地址 128 位），其余语义与 v4 的 acl_lpm 相同 */
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, MAX_LPM_KEYS);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct lpm_key6);
    __type(value, struct rule_value6);
} acl_lpm6 SEC(".maps");

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
 * IPv4 分支与阶段 2 完全一致；IPv6 分支为练习 01 新增。
 * 成功填充 *key4（IPv4）或 *key6（IPv6）并置 *is_v6 后返回 1；
 * 非 IPv4/IPv6、TCP/UDP 之外的协议或残包返回 0。
 */
static __always_inline int parse_flow(struct xdp_md *ctx, struct flow_key *key4,
                                      struct flow_key6 *key6, int *is_v6)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;

    if (eth->h_proto == bpf_htons(ETH_P_IP)) {
        /* ---- IPv4 分支：与阶段 2 逐字一致 ---- */
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

        key4->src_ip  = ip->saddr;          /* 网络字节序原样保存 */
        key4->dst_ip  = ip->daddr;
        key4->proto   = ip->protocol;
        key4->pad[0] = key4->pad[1] = key4->pad[2] = 0;

        if (ip->protocol == IPPROTO_TCP) {
            struct tcphdr *tcp = l4;
            if ((void *)(tcp + 1) > data_end)
                return 0;
            key4->src_port = tcp->source;
            key4->dst_port = tcp->dest;
        } else {
            struct udphdr *udp = l4;
            if ((void *)(udp + 1) > data_end)
                return 0;
            key4->src_port = udp->source;
            key4->dst_port = udp->dest;
        }
        *is_v6 = 0;
        return 1;
    }

    if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
        /* ---- IPv6 分支（练习 01 新增）----
         * IPv6 基本头固定 40 字节，没有 ihl 类比：sizeof(*ip6) 即头长，
         * 其后紧邻的就是 L4（本练习只识别基本头之后直接跟 TCP/UDP 的报文） */
        struct ipv6hdr *ip6 = (void *)(eth + 1);
        if ((void *)(ip6 + 1) > data_end)
            return 0;
        if (ip6->nexthdr != IPPROTO_TCP && ip6->nexthdr != IPPROTO_UDP)
            return 0;   /* 扩展头（nexthdr 非 6/17）：完整扩展头链解析超出练习范围，
                           保守放行——返回 0 走主程序「解析失败」路径（bump(STAT_PASS)
                           后 XDP_PASS），绝不能把看不懂的报文误丢 */

        key6->proto = ip6->nexthdr;
        key6->pad[0] = key6->pad[1] = key6->pad[2] = 0;
        /* 128 位地址按字节整块拷入（16 字节，网络字节序原样保存），
           不依赖 vmlinux.h 里 in6_addr 的成员命名 */
        __builtin_memcpy(key6->src_ip, &ip6->saddr, sizeof(key6->src_ip));
        __builtin_memcpy(key6->dst_ip, &ip6->daddr, sizeof(key6->dst_ip));

        void *l4 = (__u8 *)ip6 + sizeof(*ip6);   /* 基本头之后即 L4，边界检查同 v4 */
        if (ip6->nexthdr == IPPROTO_TCP) {
            struct tcphdr *tcp = l4;
            if ((void *)(tcp + 1) > data_end)
                return 0;
            key6->src_port = tcp->source;
            key6->dst_port = tcp->dest;
        } else {
            struct udphdr *udp = l4;
            if ((void *)(udp + 1) > data_end)
                return 0;
            key6->src_port = udp->source;
            key6->dst_port = udp->dest;
        }
        *is_v6 = 1;
        return 1;
    }

    return 0;   /* ARP 等其他协议：不在本章策略范围 */
}

/* v4 判定：与阶段 2 的 acl_verdict 逐字一致（只查 v4 两张 map） */
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

/* v6 判定（练习 01 新增）：与 acl_verdict 同构，但只查 v6 两张 map（互不混用） */
static __always_inline __u8 acl_verdict6(const struct flow_key6 *flow)
{
    struct rule_value6 *exact = bpf_map_lookup_elem(&acl_rules6, flow);
    if (exact)
        return exact->action;

    /* LPM 查找 key 的 prefixlen 固定 128：按完整地址查，由 trie 返回最长前缀规则 */
    struct lpm_key6 lk = { .prefixlen = 128 };
    __builtin_memcpy(lk.addr, flow->src_ip, sizeof(lk.addr));
    struct rule_value6 *lv = bpf_map_lookup_elem(&acl_lpm6, &lk);
    if (lv)
        return lv->action;

    return FW_ALLOW;
}

SEC("xdp")
int acl_prog(struct xdp_md *ctx)
{
    struct flow_key flow4;
    struct flow_key6 flow6;
    int is_v6 = 0;

    if (!parse_flow(ctx, &flow4, &flow6, &is_v6)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    __u8 action = is_v6 ? acl_verdict6(&flow6) : acl_verdict(&flow4);
    if (action == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

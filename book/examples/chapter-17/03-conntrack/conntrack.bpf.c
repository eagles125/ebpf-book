// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 3 数据面：连接跟踪（只观测，不做策略，恒返回 XDP_PASS）。
 * 每条连接以正反两个 ct_key 之一登记：先查正向（报文自身方向），未命中再查反向
 * （回包方向），命中即同一连接；TCP 必须从 SYN 开始登记，非 SYN 的中途报文跳过。
 * 最小状态机：CT_NEW --反向报文--> CT_ESTABLISHED；RST --> CT_CLOSED；FIN --> CT_FIN_WAIT。
 * conntrack 为 LRU hash：容量 CT_MAX_ENTRIES，满时自动静默淘汰最久未用项。
 * 阶段 2 的 acl_rules/acl_lpm/acl_verdict 本阶段不需要——只观测，不判 ACL。
 */
#include "fw_common.h"

/* ETH_P_IP 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP 0x0800

/* 阶段 3：parse_flow 增加出参 tcp_flags（仅 TCP 有效；位含义见下方宏） */
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_ACK 0x08

/* per-CPU 统计：每 CPU 独立副本、数据面更新无锁（阶段 3 不判 ACL，DROP_* 恒 0） */
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
 * 成功填充 *key 与 *tcp_flags 并返回 1；非 IPv4/TCP/UDP 或残包返回 0（不写出参，
 * 调用方已把 tcp_flags 预初始化为 0）。
 */
static __always_inline int parse_flow(struct xdp_md *ctx, struct flow_key *key,
                                      __u8 *tcp_flags)
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
        /* TCP 标志位打包为 4 bit 出参。fin/syn/rst/ack 是位域成员，位域布局依赖
         * 编译器与字节序：x86_64/arm64 均为小端，vmlinux.h 的 tcphdr 位域顺序与
         * 报文中标志位顺序一致，此处访问安全；大端平台需改用位运算自行解析 */
        *tcp_flags = (tcp->fin ? TCP_FLAG_FIN : 0) | (tcp->syn ? TCP_FLAG_SYN : 0)
                   | (tcp->rst ? TCP_FLAG_RST : 0) | (tcp->ack ? TCP_FLAG_ACK : 0);
    } else {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end)
            return 0;
        key->src_port = udp->source;
        key->dst_port = udp->dest;
        *tcp_flags = 0;                 /* UDP 没有 TCP 标志位 */
    }
    return 1;
}

/* LRU hash：容量固定，满时自动淘汰最久未用项——避免表无限增长拖垮内存，
 * 代价是「淘汰是静默的」，只能靠统计与控制面观测 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, CT_MAX_ENTRIES);
    __type(key, struct ct_key);
    __type(value, struct ct_value);
} conntrack SEC(".maps");

/* 正反 key 构造：reverse=0 取报文自身方向（正向）；reverse=1 源/目的互换（反向）。
 * 协议不变；pad 清零保证与查找侧逐字节一致 */
static __always_inline void fill_ct_key(struct ct_key *k, const struct flow_key *f, __u8 reverse)
{
    k->src_ip   = reverse ? f->dst_ip   : f->src_ip;
    k->dst_ip   = reverse ? f->src_ip   : f->dst_ip;
    k->src_port = reverse ? f->dst_port : f->src_port;
    k->dst_port = reverse ? f->src_port : f->dst_port;
    k->proto    = f->proto;
    k->pad[0] = k->pad[1] = k->pad[2] = 0;
}

SEC("xdp")
int conntrack_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u64 pkt_len = data_end - data;

    struct flow_key flow;
    __u8 flags = 0;
    if (!parse_flow(ctx, &flow, &flags)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    struct ct_key fwd, rev;
    fill_ct_key(&fwd, &flow, 0);
    fill_ct_key(&rev, &flow, 1);
    __u64 now = bpf_ktime_get_ns();

    /* 先查正向（报文自身方向），再查反向（回包方向） */
    struct ct_value *ct = bpf_map_lookup_elem(&conntrack, &fwd);
    __u8 from_reply = 0;
    if (!ct) {
        ct = bpf_map_lookup_elem(&conntrack, &rev);
        from_reply = 1;
    }

    if (ct) {
        /* 多 CPU 同时更新同一表项存在写竞争（后写覆盖）；教学实现接受该近似，
           正文与注释必须说明，生产可改 per-CPU 计量或 spin_lock */
        ct->last_seen_ns = now;
        ct->packets++;
        ct->bytes += pkt_len;
        if (ct->state == CT_NEW && from_reply) {
            ct->state = CT_ESTABLISHED;          /* 反向报文 = 连接建立 */
            bump(STAT_CT_ESTABLISHED);
        }
        if (flow.proto == IPPROTO_TCP) {
            if (flags & TCP_FLAG_RST)
                ct->state = CT_CLOSED;
            else if (flags & TCP_FLAG_FIN)
                ct->state = CT_FIN_WAIT;
        }
        bump(STAT_PASS);
        return XDP_PASS;                         /* 阶段 3 只观测，不做策略 */
    }

    /* 新连接登记：TCP 必须从 SYN 开始（非 SYN 且无记录的中途报文跳过登记） */
    if (flow.proto == IPPROTO_TCP && !(flags & TCP_FLAG_SYN)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    struct ct_value nv = {
        .state = CT_NEW,
        .last_seen_ns = now,
        .packets = 1,
        .bytes = pkt_len,
    };
    if (bpf_map_update_elem(&conntrack, &fwd, &nv, BPF_NOEXIST) == 0)
        bump(STAT_CT_NEW);
    else
        bump(STAT_CT_FULL);   /* 表满/LRU 异常：放行并计数，正文讨论 DROP 策略取舍 */
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

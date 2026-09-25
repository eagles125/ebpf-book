// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 5 数据面（XDP 侧）：解析 + 有状态 ACL + 连接跟踪，快路径先于 ACL。
 *
 * 与阶段 2/3 的代码来源（逐段合并，未发明新逻辑）：
 *   parse_flow/acl_verdict/bump 取自阶段 2 acl.bpf.c（parse_flow 升级为阶段 3 的
 *   三参版本，带 tcp_flags 出参——状态机需要它）；
 *   conntrack 登记/状态迁移/fill_ct_key 取自阶段 3 conntrack.bpf.c（原样）。
 *
 * 与阶段 2/3 的关键差异只有一个：**快路径先于 ACL**——conntrack 命中且
 * state==CT_ESTABLISHED 的报文直接刷新表项并放行，跳过 ACL 查找。理由：连接的
 * 合法性在其首包过 ACL 时已判定，后续报文不必逐包重查——N 条规则的策略在已建立
 * 连接上的成本为 0，这正是有状态防火墙相对无状态防火墙的性能来源。
 *
 * 主流程（SEC("xdp") int fw_xdp(struct xdp_md *ctx)）：
 *   parse_flow 失败 -> PASS
 *   bump(STAT_TOTAL)
 *   conntrack 查 fwd/rev：
 *     命中且 state==CT_ESTABLISHED -> 刷新表项、bump(STAT_CT_ESTABLISHED)、PASS（快路径）
 *     命中但未建立 -> ACL 判定（阶段 2 acl_verdict 原样）-> DROP 则 bump(STAT_DROP_ACL)
 *                    +XDP_DROP；放行则走阶段 3 的刷新与状态迁移（原样）
 *     未命中 -> ACL 判定 -> DROP 同上；放行后新连接登记（阶段 3 逻辑原样：SYN/UDP
 *              首包、BPF_NOEXIST、STAT_CT_FULL）。未命中也必须先过 ACL：首包若被
 *              策略拒绝则连接不登记、永远进不了快路径——否则任何连接的 SYN 都能
 *              绕过策略，快路径反而成了后门
 *   状态迁移（ESTABLISHED/FIN_WAIT/CLOSED）与阶段 3 完全一致：快路径同样保留
 *   RST/FIN 迁移，否则已建立连接永远无法进入 CLOSED/FIN_WAIT
 *
 * map 共享（C3 同名必为同一实例）：本文件的 acl_rules/acl_lpm/conntrack/stats 与
 * fw_tc.bpf.c 的 nat_map/stats 通过 user/maps.go 的 mergeSpecs 合并为一次加载并
 * pin 到 /sys/fs/bpf/fw17——XDP 与 TC 看到同一份规则/连接/NAT/统计。若两个 .o 各
 * 自加载出独立 map 实例，XDP 放行的连接 TC 不认识、规则改一半只见一半，是这类
 * 多程序防火墙最常见的部署事故（正文重点）。
 *
 * 挂载约定（与 C5 拓扑一致）：fw_xdp 挂 veth-fw-c（只接收客户端方向报文）。
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

/* hash：key 全等匹配，O(1)；适合精确封禁，不适合网段/端口范围。
 * __uint(pinning, LIBBPF_PIN_BY_NAME)：加载时按 map 名 pin 到 user/maps.go 传入的
 * PinPath（/sys/fs/bpf/fw17），已 pin 的同名同定义 map 直接复用——这是「XDP 与 TC
 * 共享同一实例」与「独立子命令读写同一份规则」的基础 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key);
    __type(value, struct rule_value);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} acl_rules SEC(".maps");

/* LPM trie：按最长前缀匹配网段；BPF_F_NO_PREALLOC 必需（节省内存、允许大 max_entries） */
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, MAX_LPM_KEYS);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct lpm_key);
    __type(value, struct lpm_value);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} acl_lpm SEC(".maps");

/* per-CPU 统计：每 CPU 独立副本、数据面更新无锁（Go 侧逐 CPU 求和展示） */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} stats SEC(".maps");

/* LRU hash：容量固定，满时自动淘汰最久未用项——避免表无限增长拖垮内存，
 * 代价是「淘汰是静默的」，只能靠统计与控制面观测 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, CT_MAX_ENTRIES);
    __type(key, struct ct_key);
    __type(value, struct ct_value);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} conntrack SEC(".maps");

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

/* ACL 判定（阶段 2 acl_verdict 原样）：精确五元组 -> 源地址最长前缀 -> 默认放行 */
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
int fw_xdp(struct xdp_md *ctx)
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
        /* 快路径（阶段 5 关键差异）：连接已建立，跳过 ACL 查找直接放行。
         * 首包过 ACL 时已判定该连接的合法性，这里只刷新计量与状态。
         * RST/FIN 迁移必须保留（与阶段 3 一致），否则已建立连接永远无法收尾 */
        if (ct->state == CT_ESTABLISHED) {
            /* 多 CPU 同时更新同一表项存在写竞争（后写覆盖）；教学实现接受该近似，
               生产可改 per-CPU 计量或 spin_lock（与阶段 3 同一口径） */
            ct->last_seen_ns = now;
            ct->packets++;
            ct->bytes += pkt_len;
            if (flow.proto == IPPROTO_TCP) {
                if (flags & TCP_FLAG_RST)
                    ct->state = CT_CLOSED;
                else if (flags & TCP_FLAG_FIN)
                    ct->state = CT_FIN_WAIT;
            }
            bump(STAT_CT_ESTABLISHED);   /* 阶段 5 起本计数兼记快路径放行数 */
            return XDP_PASS;
        }

        /* 慢路径（命中但未建立）：先 ACL 判定（阶段 2 acl_verdict 原样）。
         * 被 ACL 拒绝的报文不刷新表项——被拒的连接不因后续报文保持活跃 */
        if (acl_verdict(&flow) == FW_DROP) {
            bump(STAT_DROP_ACL);
            return XDP_DROP;
        }
        /* ACL 放行：阶段 3 的刷新与状态迁移原样（先 CT_NEW->ESTABLISHED 转移、
         * 再 RST/FIN 覆盖，顺序与阶段 3 一致） */
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
        return XDP_PASS;                         /* ACL 已放行 + 状态已刷新 */
    }

    /* 未命中：同样先 ACL——首包若被策略拒绝，连接不登记、不建立快路径。
       登记动作必须发生在策略放行之后，否则快路径反而成了绕过策略的后门 */
    if (acl_verdict(&flow) == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }

    /* 新连接登记（阶段 3 逻辑原样）：TCP 必须从 SYN 开始（非 SYN 且无记录的中途报文跳过登记） */
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
        bump(STAT_CT_FULL);   /* 非 0 = 同 key 并发登记竞争（-EEXIST）或分配失败：放行并计数。
                                 LRU 满表不走此分支——内核静默淘汰最久未用项后插入仍成功
                                 （LRU「不阻塞新流」的设计行为）；是否对失败 DROP 由正文讨论取舍 */
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 04 数据面：Ethernet/IPv4/TCP/UDP 解析 + 按目的端口令牌桶限速。
 * 解析与计数骨架（parse_flow/bump）与阶段 2 acl.bpf.c 逐字一致，但按本练习口径
 * 省略 acl_rules/acl_lpm 两张 map 与 acl_verdict——策略只有限速，超额 XDP_DROP。
 */
#include "fw_common.h"

/* ETH_P_IP 是 uapi 头文件里的 #define：BTF 只导出类型、不导出宏，
   因此 vmlinux.h 中没有它——CO-RE 程序遇到这类宏需自行补定义 */
#define ETH_P_IP 0x0800

/* 限速配置：loader 用 --limit 下发。key 为目的端口（网络字节序，与报文里的
 * dst_port「原样存原样查」，见 loader 侧 htons 的注释） */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, RL_MAX_PORTS);
    __type(key, __u16);
    __type(value, struct rl_cfg);
} rate_cfg SEC(".maps");

/* 令牌桶状态：数据面运行时维护（loader 不写）。LRU hash：限速端口至多 RL_MAX_PORTS
 * 个（与 rate_cfg 同上限），状态条目数不会超过配置条目数，LRU 只是兜底淘汰 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, RL_MAX_PORTS);
    __type(key, __u16);
    __type(value, struct rl_value);
} rate_state SEC(".maps");

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

/* 令牌补充：tokens(Q8.10) += elapsed * rate 的折算见函数内注释（移位代替除法，
 * 规避 verifier 对除数非零的约束与除零路径）。rate==0 直接放行（未配置端口不限速）。 */
static __always_inline __u8 rl_allow(__u16 dport, __u64 now)
{
    struct rl_cfg *cfg = bpf_map_lookup_elem(&rate_cfg, &dport);
    if (!cfg || cfg->rate_pps == 0)
        return 1;                         /* 未配置端口：放行 */
    struct rl_value *st = bpf_map_lookup_elem(&rate_state, &dport);
    if (!st) {
        struct rl_value init = { .tokens = cfg->burst << 10, .last_ns = now };
        bpf_map_update_elem(&rate_state, &dport, &init, BPF_ANY);
        return 1;
    }
    __u64 elapsed = now - st->last_ns;
    /* tokens(Q8.10) += elapsed_ns * rate_pps / 1e9 * 1024 ≈ (elapsed * rate) >> 20。
     * 量级分析：elapsed ≤ 1e11（约 100s）、rate ≤ 1e7 时乘积 ≤ 1e18 < u64 上限，
     * 不会溢出；超长空闲后首包的少量少补由 burst 封顶掩盖，教学可接受 */
    st->tokens += (elapsed * cfg->rate_pps) >> 20;
    if (st->tokens > (cfg->burst << 10))
        st->tokens = cfg->burst << 10;               /* 上限封顶 */
    st->last_ns = now;
    if (st->tokens < (1 << 10))
        return 0;                                    /* 不足 1 个额度：超额 DROP */
    st->tokens -= (1 << 10);
    return 1;
}

SEC("xdp")
int ratelimit_prog(struct xdp_md *ctx)
{
    struct flow_key flow;
    if (!parse_flow(ctx, &flow)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    /* 限速按目的端口；now 与 last_ns 同用 bpf_ktime_get_ns 的
     * CLOCK_MONOTONIC 基准（阶段 3 的时间口径，勿混用墙钟） */
    if (!rl_allow(flow.dst_port, bpf_ktime_get_ns())) {
        /* 复用 C1 的 STAT_DROP_ACL 下标计「策略命中拒绝」（本练习即限速超额；
         * 下标顺序遵循 C1 不改 enum，loader 侧展示名记为 drop_rl） */
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

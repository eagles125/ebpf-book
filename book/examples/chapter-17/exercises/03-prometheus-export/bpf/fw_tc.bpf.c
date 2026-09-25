// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 5 数据面（TC 侧）：TC clsact 上做 SNAT/DNAT + 增量校验和。
 * 与阶段 4 的差异仅程序名与共享 map pin：
 *   程序名 nat_ingress/nat_egress -> fw_tc_ingress/fw_tc_egress（SEC 仍为 "tc"，
 *   Go 控制面以 `tc filter replace ... pinned` 方式挂载这两个程序）；
 *   nat_map/stats 与 fw_xdp.bpf.c 的 acl_rules/acl_lpm/conntrack/stats 共用同一份
 *   pin 实例（user/maps.go 的 mergeSpecs 把两个 .o 的同名 map 合并为一次加载）：
 *   XDP 与 TC 的统计汇入同一张 stats 表，规则/连接/NAT 表全系统唯一。
 * do_nat 的通配查找、方向过滤、先改字段后修校验和、UDP 0 校验和跳过等逻辑与
 * 阶段 4 逐字一致（含 SNAT 通配源端口清零——出方向源端口是内核临时值，
 * 按 (源 IP, 协议) 命中）。
 *
 * 挂载约定（与 C5 拓扑一致）：
 *   fw_tc_ingress 挂 veth-fw-c ingress 做 DNAT（目的改写）；
 *   fw_tc_egress  挂 veth-fw-s egress  做 SNAT（源改写）。
 */
#include "fw_common.h"

/* ETH_P_IP 与 TC_ACT_OK 都是 uapi 头文件里的 #define（前者在 if_ether.h，后者在
   pkt_cls.h）：BTF 只导出类型、不导出宏，因此 vmlinux.h 中都没有它们——CO-RE
   程序遇到这类宏需自行补定义（取值以 uapi 为准：0x0800 / 0） */
#define ETH_P_IP 0x0800
#define TC_ACT_OK 0

/* __uint(pinning, LIBBPF_PIN_BY_NAME)：加载时按 map 名 pin 到 user/maps.go 传入的
 * PinPath（/sys/fs/bpf/fw17），与 fw_xdp.bpf.c 的同名 map 合并为同一实例（C3） */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NAT_MAX_ENTRIES);
    __type(key, struct nat_key);
    __type(value, struct nat_value);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} nat_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} stats SEC(".maps");

static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

/* 用 bpf_skb_load_bytes 读端口（读 helper 不改动报文，不会使包指针失效） */
static __always_inline int load_ports(struct __sk_buff *skb, __u32 l4_off, __u8 proto,
                                      __be16 *sport, __be16 *dport)
{
    if (proto == IPPROTO_TCP) {
        struct tcphdr t;
        if (bpf_skb_load_bytes(skb, l4_off, &t, sizeof(t)))
            return 0;
        *sport = t.source; *dport = t.dest;
        return 1;
    }
    struct udphdr u;
    if (bpf_skb_load_bytes(skb, l4_off, &u, sizeof(u)))
        return 0;
    *sport = u.source; *dport = u.dest;
    return 1;
}

/*
 * 统一改写核心：dir=0 DNAT（改目的地址/端口），dir=1 SNAT（改源地址/端口）。
 *
 * 实现要点（正文逐条讲解）：
 *  1) 所有包指针必须在第一个改写 helper 调用之前用完——bpf_skb_store_bytes /
 *     csum_replace 可能使 verifier 判定包指针失效；
 *  2) 改写字段用 store_bytes（绝对偏移），旧值/新值都先读入局部变量；
 *  3) 校验和用增量算法：改动 4 字节地址等价于在原校验和上叠加 ~(old)+new 的进位折叠，
 *     bpf_l4_csum_replace 带 BPF_F_PSEUDO_HDR 处理 TCP/UDP 伪头中的地址项；
 *  4) UDP 校验和为 0 表示「不校验」，此时不得调用 helper（会被改坏语义）；
 *  5) 通配规则：精确 key 未命中时，把「非 NAT 侧」的地址/端口清零再查一次；
 *  6) new_port==0 表示「只改地址不改端口」（SNAT 场景），此时跳过端口改写与端口增量。
 */
static __always_inline int do_nat(struct __sk_buff *skb, __u8 dir)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 l3_off = sizeof(struct ethhdr);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;                /* 残包直接放行：verifier 要求先定界再解引用 */
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return TC_ACT_OK;                /* ARP/IPv6 等不参与 NAT */

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return TC_ACT_OK;                /* 教学实现只改写 TCP/UDP（校验和增量的两种原型） */

    __u32 l4_off = l3_off + ip->ihl * 4;
    __be16 sport = 0, dport = 0;
    if (!load_ports(skb, l4_off, ip->protocol, &sport, &dport))
        return TC_ACT_OK;

    struct nat_key key = {
        .src_ip = ip->saddr, .dst_ip = ip->daddr,
        .src_port = sport, .dst_port = dport,
        .proto = ip->protocol,
    };
    struct nat_value *v = bpf_map_lookup_elem(&nat_map, &key);
    if (!v) {
        struct nat_key wild = key;
        if (dir == 0) { wild.src_ip = 0; wild.src_port = 0; }   /* DNAT：源侧通配 */
        else          { wild.dst_ip = 0; wild.dst_port = 0;     /* SNAT：目的侧通配 */
                        wild.src_port = 0; }   /* 源端口一并通配：--snat 只有 IP 入参，
                                                  出方向源端口是内核随机分配的临时值，
                                                  静态规则写不出，按 (源 IP, 协议) 命中 */
        v = bpf_map_lookup_elem(&nat_map, &wild);
    }
    if (!v || v->is_snat != dir)         /* 方向过滤：DNAT 程序只认 is_snat==0，反之亦然 */
        return TC_ACT_OK;

    __u8  is_tcp    = (ip->protocol == IPPROTO_TCP);
    __u32 old_addr  = (dir == 0) ? ip->daddr : ip->saddr;
    __u32 new_addr  = v->new_addr;
    __be16 old_port = (dir == 0) ? dport : sport;
    __be16 new_port = v->new_port;
    __u8  do_port   = (v->new_port != 0);   /* new_port==0：仅改地址（SNAT 场景） */
    __u32 addr_off  = l3_off + (dir == 0 ? offsetof(struct iphdr, daddr)
                                         : offsetof(struct iphdr, saddr));
    /* tcphdr/udphdr 的 source/dest 布局相同：SNAT 改 offset 0，DNAT 改 offset 2 */
    __u32 port_off  = l4_off + (dir == 0 ? 2 : 0);
    __u32 l4_ck_off = l4_off + (is_tcp ? offsetof(struct tcphdr, check)
                                       : offsetof(struct udphdr, check));

    /* 1) 改写端口与地址（先改字段，后修校验和：helper 参数自带 old/new，顺序无关正确性） */
    if (do_port)
        bpf_skb_store_bytes(skb, port_off, &new_port, sizeof(new_port), 0);
    bpf_skb_store_bytes(skb, addr_off, &new_addr, sizeof(new_addr), 0);

    /* 2) L4 校验和：地址增量按伪头计（BPF_F_PSEUDO_HDR），端口增量 2 字节 */
    if (is_tcp) {
        bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                            sizeof(__u32) | BPF_F_PSEUDO_HDR);
        if (do_port)
            bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
    } else {
        __u16 ck = 0;
        bpf_skb_load_bytes(skb, l4_ck_off, &ck, sizeof(ck));
        if (ck != 0) {                       /* UDP 校验和 0 = 不校验，跳过 */
            bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                                sizeof(__u32) | BPF_F_PSEUDO_HDR);
            if (do_port)
                bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
        }
    }

    /* 3) IP 头校验和最后更新（只含地址增量，与端口无关） */
    bpf_l3_csum_replace(skb, l3_off + offsetof(struct iphdr, check),
                        old_addr, new_addr, sizeof(__u32));

    bump(STAT_NAT);                          /* 只计实际改写成功的报文 */
    return TC_ACT_OK;
}

SEC("tc")
int fw_tc_ingress(struct __sk_buff *skb)   /* DNAT */
{
    return do_nat(skb, 0);
}

SEC("tc")
int fw_tc_egress(struct __sk_buff *skb)    /* SNAT */
{
    return do_nat(skb, 1);
}

char LICENSE[] SEC("license") = "GPL";

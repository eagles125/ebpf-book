/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 阶段 5 共享头：完整防火墙（ACL + conntrack + NAT）所需的全部结构。
 * 本文件为阶段 2–4 的严格并集，正文按新增字段来源讲解；
 * 字段语义/字节序/pad 一经定义不再改动（正文与 Go 控制面按此契约镜像，
 * user 目录下的镜像结构体必须与本文件逐字节一致）。
 */
#ifndef FW_COMMON_H
#define FW_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* ===== 常量区：各阶段累计的容量上限 ===== */
#define MAX_RULES    1024   /* hash 精确规则上限：防 map 膨胀，超限下发直接失败 */
#define MAX_LPM_KEYS 4096   /* LPM trie 条目上限 */
#define CT_MAX_ENTRIES 65536    /* LRU 上限：满后内核静默淘汰最久未用项，新连接登记仍成功 */
#define NAT_MAX_ENTRIES 4096    /* hash 规则上限：NAT 表项由控制面静态下发，容量防 map 膨胀 */

/* per-CPU 统计下标（顺序即契约，Go 侧按顺序展示） */
enum stat_idx {
    STAT_TOTAL = 0,      /* 阶段 2 新增：成功解析出五元组的报文数 */
    STAT_PASS,           /* 阶段 2 新增：放行 */
    STAT_DROP_ACL,       /* 阶段 2 新增：ACL 命中拒绝 */
    STAT_DROP_PARSE,     /* 阶段 2 新增：保留（阶段 2 暂不使用，残包按 PASS 处理） */
    STAT_CT_NEW,         /* 阶段 3 新增：新连接登记成功（TCP SYN 或 UDP 首包） */
    STAT_CT_ESTABLISHED, /* 阶段 3 新增：反向报文命中，连接转入 ESTABLISHED */
    STAT_CT_FULL,        /* 阶段 3 新增：登记失败计数（同 key 并发竞争/分配失败；LRU 满表不触发） */
    STAT_NAT,            /* 阶段 4 新增：NAT 改写成功次数（DNAT/SNAT 任一命中并改写即 +1） */
    STAT_MAX,            /* 统计计数个数 =8（阶段 5 汇总沿用同一顺序，不再追加） */
};

/* ===== 阶段 2 定义：报文解析与 ACL ===== */

/* 五元组 key：IP/端口一律网络字节序存储（原样存原样查即可匹配，无需主机序转换）；
 * pad[3] 使结构体 4 字节对齐，哈希与字节级比较行为确定 */
struct flow_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

enum fw_action {
    FW_ALLOW = 0,
    FW_DROP  = 1,
};

/* hash 精确规则 value */
struct rule_value {
    __u8 action;        /* enum fw_action */
    __u8 pad[7];
};

/* LPM trie key：前缀长度（bit）+ 地址；LPM 要求 key 以 prefixlen 开头 */
struct lpm_key {
    __u32 prefixlen;
    __be32 addr;
};

/* LPM value：最长前缀命中的动作（黑/白名单由 action 区分，网段越具体优先级越高） */
struct lpm_value {
    __u8 action;
    __u8 pad[7];
};

/* ===== 阶段 3 追加：连接跟踪 ===== */

/* 最小可用 TCP 状态机（不复刻 netfilter conntrack 全量语义） */
enum ct_state {
    CT_NEW         = 1,   /* 首包登记（TCP SYN 或 UDP 首包） */
    CT_ESTABLISHED = 2,   /* 看到反向报文 */
    CT_FIN_WAIT    = 3,   /* 收到 FIN，等待对端收尾 */
    CT_CLOSED      = 4,   /* 收到 RST 或双向收尾；控制面超时后可清理 */
};

/* conntrack key：与 flow_key 同构；正反两个方向各构成一个 key */
struct ct_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

/* 时间戳用 bpf_ktime_get_ns()（单调时钟，非墙钟）；超时回收由控制面完成 */
struct ct_value {
    __u64 last_seen_ns;
    __u64 packets;
    __u64 bytes;
    __u8  state;          /* enum ct_state */
    __u8  pad[7];
};

/* ===== 阶段 4 追加：NAT 映射 ===== */

/* NAT key：与 flow_key 同构；key 中为 0 的字段是「通配维度」——数据面精确 key
 * 未命中时把报文侧对应字段清零再查一次即命中（两段查找），下发侧据此写通配表项 */
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

#endif /* FW_COMMON_H */

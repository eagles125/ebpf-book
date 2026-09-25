/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 练习 01 共享头：以阶段 2（../../02-parse-acl/fw_common.h）为基线逐字复用，
 * 文末追加 IPv6 扩展结构（练习 01 新增部分，不动既有契约）。
 * 字段语义/字节序/pad 一经定义不再改动（loader 侧按同一契约逐字段镜像）。
 */
#ifndef FW_COMMON_H
#define FW_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define MAX_RULES    1024   /* hash 精确规则上限：防 map 膨胀，超限下发直接失败 */
#define MAX_LPM_KEYS 4096   /* LPM trie 条目上限（v4/v6 各一张，上限相同） */

/* per-CPU 统计下标（顺序即契约，loader 侧按顺序展示；练习 01 不新增下标） */
enum stat_idx {
    STAT_TOTAL = 0,     /* 成功解析出五元组的报文数（v4 + v6） */
    STAT_PASS,          /* 放行 */
    STAT_DROP_ACL,      /* ACL 命中拒绝 */
    STAT_DROP_PARSE,    /* 保留（练习 01 暂不使用，残包按 PASS 处理） */
    STAT_MAX,           /* 统计计数个数 =4（与阶段 2 相同） */
};

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

/* ===== 练习 01 新增：IPv6 五元组与规则 ===== */
struct flow_key6 {
    __be32 src_ip[4];    /* 128 位地址，网络字节序，4 个 32 位字原样存储 */
    __be32 dst_ip[4];
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;        /* 仅 6(TCP)/17(UDP) */
    __u8   pad[3];
};

struct rule_value6 { __u8 action; __u8 pad[7]; };
struct lpm_key6 { __u32 prefixlen; __be32 addr[4]; };   /* LPM key，前缀最长 128 */

#endif /* FW_COMMON_H */

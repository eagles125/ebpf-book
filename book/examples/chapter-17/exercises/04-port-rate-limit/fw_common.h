/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 练习 04 共享头：复用阶段 2（../../02-parse-acl/fw_common.h）的统计下标与五元组结构，
 * 文末追加按端口令牌桶的练习专用结构。
 * 按本练习口径省略阶段 2 的 ACL 专用类型（fw_action/rule_value/lpm_key/lpm_value）：
 * 本练习的策略只有限速，不含 ACL（对应 ratelimit.bpf.c 省略 acl_rules/acl_lpm/acl_verdict）。
 * 保留的字段语义/字节序/pad 一经定义不再改动（loader 侧按同一契约逐字段镜像）。
 */
#ifndef FW_COMMON_H
#define FW_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* per-CPU 统计下标（顺序即契约，loader 侧按顺序展示；练习 04 不新增下标） */
enum stat_idx {
    STAT_TOTAL = 0,     /* 成功解析出五元组的报文数 */
    STAT_PASS,          /* 放行 */
    STAT_DROP_ACL,      /* 本练习复用该下标计「策略命中拒绝」= 限速超额丢弃 */
    STAT_DROP_PARSE,    /* 保留（练习 04 暂不使用，残包按 PASS 处理） */
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

/* ===== 练习 04 新增：按端口令牌桶 ===== */
#define RL_MAX_PORTS 256

/* 令牌桶状态：tokens 定点数（1 = 1/1024 个报文额度），避免浮点（verifier 限制） */
struct rl_value {
    __u64 tokens;      /* 当前令牌，Q8.10 定点 */
    __u64 last_ns;     /* 上次补充时间 */
};

struct rl_cfg {
    __u64 rate_pps;    /* 每秒允许报文数（0 = 不限速） */
    __u64 burst;       /* 桶容量（报文数），吸收突发 */
};

#endif /* FW_COMMON_H */

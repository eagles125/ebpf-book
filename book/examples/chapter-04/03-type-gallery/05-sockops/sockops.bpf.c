// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊⑤：sockops——TCP 连接生命周期回调
 * （预期加载前提：root + cgroup 挂载；事件由内核 TCP 状态机驱动）。
 * ctx 是 struct bpf_sock_ops：含本地/对端地址端口、TCP 状态与 snd_cwnd 等。
 * 返回值语义：1=本 callback 已处理该事件，0=交给默认路径。
 * 视角价值：socket 级状态可见——这是 XDP/TC（报文级）没有的维度。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/* map 选型：ARRAY——两类连接建立事件固定计数 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);      /* 0=被动建立 1=主动建立 */
    __type(value, __u64);
} tcp_estab SEC(".maps");

SEC("sockops")
int so(struct bpf_sock_ops *ops)
{
    __u32 key;
    /* ops->op 是事件码：BPF_SOCK_OPS_* 枚举在 BTF（冒烟结论「常量在场」）
     * 失败模式：未知 op 一律放行——sockops 回调种类多，教学只关心两种建立 */
    switch (ops->op) {
    case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:   /* 服务端：三次握手完成 */
        key = 0;
        break;
    case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:    /* 客户端：connect 完成 */
        key = 1;
        break;
    default:
        return 0;
    }
    __u64 *v = bpf_map_lookup_elem(&tcp_estab, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 1;
}

char LICENSE[] SEC("license") = "GPL";

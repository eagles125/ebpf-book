// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 练习 1 参考实现：三个场景的正确选型合并为一个可编译对象。
 * 场景A 容器内文件打开审计 → LSM（lsm/file_open）：安全决策点、内核对象直读。
 * 场景B TCP 连接建立观测 → sockops：socket 级生命周期回调，握手完成即可见。
 * 场景C 内核函数入口参数抓取 → fentry：BTF 类型化直参，签名加载期校验。
 * 选型论证（含错误选项为何不行）见本目录 README 分界线之后。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* 三场景共用一张 ARRAY 计数表（key=场景编号），map 选型：ARRAY——key 恒定 3 个 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);
    __type(key, __u32);
    __type(value, __u64);
} scenario_hits SEC(".maps");

/* 场景A：file_open hook——审计容器内文件打开（返回 0=允许，只审计不拦截） */
SEC("lsm/file_open")
int BPF_PROG(audit_open, struct file *file)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

/* 场景B：passive established——服务端视角的连接建立计数 */
SEC("sockops")
int so_conn(struct bpf_sock_ops *ops)
{
    if (ops->op != BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB)
        return 0;
    __u32 key = 1;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 1;
}

/* 场景C：fentry 抓 do_sys_openat2 入口参数——dfd/filename 均可直接命名 */
SEC("fentry/do_sys_openat2")
int BPF_PROG(grab_params, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 2;
    __u64 *v = bpf_map_lookup_elem(&scenario_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

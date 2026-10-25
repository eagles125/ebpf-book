// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 4 章锚点①：同一事件五副眼镜——openat2 经 5 类追踪程序观察。
 * 事件统一为 do_sys_openat2(dfd, filename, how)：openat/openat2 系统调用的公共实现。
 * 签名在本机 BTF 中实证（bpftool btf dump）；上游 5.10+ 同签名——fentry/fexit 的参数
 * 类型在加载期由 BTF 校验，签名随内核版本演进（正文 2.4）。
 * 失败模式：签名与目标内核不一致时加载被拒（预期日志见 README 排错表）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>   /* BPF_PROG/BPF_KPROBE 宏在此（冒烟结论③） */

/*
 * 共享分镜 map：ARRAY 5 槽，key=镜编号（0..4，见下）。
 * map 选型：ARRAY——key 恒定 5 个、无删除、O(1) 定位，教学聚焦类型差异而非 map 选型。
 */
struct lens_val {
    __u64 count;         /* 本镜触发次数 */
    char last_name[64];  /* 最近一次打开的文件名（bpf_probe_read_user_str 截断安全） */
    __s64 last_ret;      /* 仅 fexit 镜写：openat2 返回值（fd 或负错误码） */
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 5);
    __type(key, __u32);
    __type(value, struct lens_val);
} lenses SEC(".maps");

/*
 * helper 选型：bpf_probe_read_user_str——filename 是 __user 指针，任何「眼镜」下都
 * 不得直接解引用（第 3 章画廊 card ⑦ 的界限）；helper 读入 map 值，长度截断安全。
 */

/* 镜 0：tracepoint——带类型的事件 ctx（syscall 类复用通用布局，args[1]=filename） */
SEC("tracepoint/syscalls/sys_enter_openat2")
int tp_entry(struct trace_event_raw_sys_enter *ctx)
{
    const char *filename = (const char *)ctx->args[1];
    __u32 key = 0;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)                       /* lookup 失败分支必检（第 3 章 card ①） */
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/*
 * 镜 1：raw_tp——原始参数无类型：ctx 是 u64 数组，形状随事件类不同
 * （syscall 类为 (regs, id)，以内核源码 kernel/trace/trace_syscalls.c 为准）。
 * 本镜只读形状不拆字段：count 计数，ctx[1] 原样存进 last_ret 供对照。
 */
SEC("raw_tp/sys_enter_openat2")
int raw_entry(__u64 *ctx)
{
    __u32 key = 1;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    v->last_ret = (__s64)ctx[1];
    return 0;
}

/* 镜 2：kprobe——ctx 是 pt_regs；BPF_KPROBE 宏按 C 原型从寄存器解包参数（x86 展开，
 * 「以所用架构为准」）。第 1、2 参数即 dfd/filename。 */
SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(kp_entry, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 2;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/* 镜 3：fentry——BTF 直接类型化参数：无 pt_regs 中转，原型即函数原型（本机 BTF 实证）。
 * filename 仍是 __user 指针：类型精确不豁免 probe 界限（正文 3.4）。 */
SEC("fentry/do_sys_openat2")
int BPF_PROG(fe_entry, int dfd, const char *filename, struct open_how *how)
{
    __u32 key = 3;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    bpf_probe_read_user_str(v->last_name, sizeof(v->last_name), filename);
    return 0;
}

/* 镜 4：fexit——在 fentry 基础上多一个返回值参数：openat2 成功返回 fd、失败返回负错误码。
 * 五镜中只有 fexit 能直接看到「这次调用办成了没有」。 */
SEC("fexit/do_sys_openat2")
int BPF_PROG(fx_entry, int dfd, const char *filename, struct open_how *how, int ret)
{
    __u32 key = 4;
    struct lens_val *v = bpf_map_lookup_elem(&lenses, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(&v->count, 1);
    v->last_ret = ret;
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

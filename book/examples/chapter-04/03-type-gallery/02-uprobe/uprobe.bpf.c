// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 类型画廊②：uprobe——用户态函数探针（预期加载前提：root + 指定二进制路径与偏移）。
 * SEC 命名规则：uprobe/<二进制路径>:<偏移>——路径与偏移由 loader/bpftool 提供，
 * 编译期 SEC 串只是标记（libbpf 解析后转成 KPROBE 类型的 uprobe 变体）。
 * 失败模式：路径/偏移错→attach 失败；用户内存读必须走 probe 系 helper
 * （第 3 章画廊 card ⑦ 界限回指）。 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} uprobe_hits SEC(".maps");

SEC("uprobe")
int up(struct pt_regs *ctx)
{
    __u32 key = 0;
    __u64 *v = bpf_map_lookup_elem(&uprobe_hits, &key);
    if (!v)
        return 0;
    __sync_fetch_and_add(v, 1);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

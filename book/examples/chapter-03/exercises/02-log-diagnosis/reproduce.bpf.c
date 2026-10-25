// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 练习 2 参考实现：复现「R2 min value is negative, either use unsigned
 * or 'var &= const'」拒绝日志的最小程序（预期被拒）。
 * 根因：报文前 4 字节按有符号数解读后直接当 packet 指针偏移，smin 为负——
 * verifier 拒绝一切「可能向报文头前方走」的指针算术（对照画廊 03：pkt_end
 * 禁止算术；本题是 pkt 指针允许算术但范围必须非负）。
 * 修复方向二选一：改无符号类型，或先掩码定界（正文 2.5）。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

SEC("xdp")
int reproduce(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    if (data + 4 > data_end)          /* 先读 4 字节的合法性要单独证明 */
        return XDP_PASS;
    __s32 off = *(__s32 *)data;       /* 有符号字段：smin = -2^31 */
    if (data + off > data_end)        /* 预期被拒：负偏移指针算术 */
        return XDP_DROP;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

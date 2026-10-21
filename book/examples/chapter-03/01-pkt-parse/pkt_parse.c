// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 01-pkt-parse 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./pkt_parse <网卡名>   （建议 lo 或 veth，勿挂生产物理网卡）
 * 职责: open_and_load（打开对象 -> verifier 校验 -> JIT）-> attach XDP ->
 *       每秒打印 UDP 计数 -> Ctrl-C 退出即卸载。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "pkt_parse.skel.h"

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: sudo %s <网卡名>（建议 lo 或 veth）\n", argv[0]);
        return 1;
    }

    /* 加载失败常见原因：权限不足（-EPERM）、verifier 拒绝（-EACCES，见正文排错节） */
    struct pkt_parse_bpf *skel = pkt_parse_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    int ifindex = if_nametoindex(argv[1]);
    if (!ifindex) {
        fprintf(stderr, "网卡 %s 不存在\n", argv[1]);
        pkt_parse_bpf__destroy(skel);
        return 1;
    }

    /* XDP 挂载点必须显式指定接口（与 tracepoint 的 SEC 推断不同，见第 2 章 1.5）；
     * flags=0 时驱动不支持原生 XDP 一般会回退 generic 模式（随内核版本，以实测为准） */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.pkt_parse, ifindex);
    if (!link) {
        fprintf(stderr, "attach XDP 失败（需 root/CAP_NET_ADMIN；勿挂生产物理网卡）\n");
        pkt_parse_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 已挂载到 %s，每秒打印 UDP 计数，Ctrl-C 退出。\n", argv[1]);

    signal(SIGINT, handle_sig);
    int fd = bpf_map__fd(skel->maps.udp_counts);
    while (!exiting) {
        __u32 key = 0;
        __u64 v = 0;
        if (!bpf_map_lookup_elem(fd, &key, &v))
            printf("udp_packets=%llu\n", (unsigned long long)v);
        sleep(1);
    }

    bpf_link__destroy(link);        /* link 释放 + skeleton fd 关闭：引用归零即卸载 */
    pkt_parse_bpf__destroy(skel);
    return 0;
}

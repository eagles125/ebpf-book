// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* 第 4 章锚点①用户态 loader：同一事件五副眼镜的分镜读数器。
 * 挂载方式：bpf_program__attach() 逐程序自动挂载——libbpf 按 SEC 前缀解析挂载点
 * （tracepoint→tracefs 事件、kprobe→kprobe 接口、fentry/fexit→BTF 驱动的
 * tracing link、raw_tp→原始 tracepoint），比逐类型手写 attach 更能体现
 * 「类型决定挂载方式」。
 * 失败模式：无特权时 open/load 报 EPERM、fentry attach 报ENOENT/EINVAL
 * （目标函数签名不匹配），README 排错表逐一对应。 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "multi_observe.skel.h"

static volatile sig_atomic_t exiting = 0;
static void on_sigint(int sig) { exiting = 1; }

/* libbpf 1.x：错误走标准 errno；日志回调把 WARN/INFO 透传到 stderr（DEBUG 级滤除），
 * 便于教学观察挂载过程。libbpf_set_print 只接收回调函数指针，不能直接传级别常量。 */
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level > LIBBPF_INFO)
        return 0;
    return vfprintf(stderr, format, args);
}

/* 与 multi_observe.bpf.c 的 struct lens_val 保持一致（skeleton 不导出 map 值类型） */
struct lens_val {
    __u64 count;
    char last_name[64];
    __s64 last_ret;
};

static const char *LENS_NAMES[5] = {
    "tracepoint", "raw_tp", "kprobe", "fentry", "fexit",
};

int main(void)
{
    struct multi_observe_bpf *skel;
    struct bpf_link *links[5] = {0};
    struct bpf_program *progs[5];
    int err = 0;

    signal(SIGINT, on_sigint);
    libbpf_set_print(libbpf_print_fn);

    skel = multi_observe_bpf__open();
    if (!skel) {
        fprintf(stderr, "skeleton open 失败：见上方 libbpf 日志\n");
        return 1;
    }
    /* 加载：这一步内部依次过 ELF 解析、重定位、verifier——第 3 章的全部内容
     * 都发生在 multi_observe_bpf__load() 内部 */
    err = multi_observe_bpf__load(skel);
    if (err) {
        fprintf(stderr, "load 失败（无特权环境的预期失败：%s）\n", strerror(-err));
        multi_observe_bpf__destroy(skel);
        return 1;
    }

    progs[0] = skel->progs.tp_entry;
    progs[1] = skel->progs.raw_entry;
    progs[2] = skel->progs.kp_entry;
    progs[3] = skel->progs.fe_entry;
    progs[4] = skel->progs.fx_entry;
    for (int i = 0; i < 5; i++) {
        links[i] = bpf_program__attach(progs[i]);
        if (!links[i]) {
            fprintf(stderr, "attach %s 失败（需 root/特权档；fentry 还需签名匹配）\n",
                    LENS_NAMES[i]);
            err = 1;
            goto out;
        }
    }
    printf("五副眼镜已挂上 openat2，随便跑几条命令（如 ls、cat）观察计数……\n");

    while (!exiting) {
        sleep(1);
        for (__u32 key = 0; key < 5; key++) {
            struct lens_val v = {};
            if (bpf_map_lookup_elem(bpf_map__fd(skel->maps.lenses),
                                    &key, &v))
                continue;
            printf("  [%-10s] count=%-6llu last_ret=%-4lld name=%s\n",
                   LENS_NAMES[key], (unsigned long long)v.count,
                   (long long)v.last_ret,
                   v.last_name[0] ? v.last_name : "(本镜不读文件名)");
        }
        printf("----\n");
    }

out:
    for (int i = 0; i < 5; i++)
        bpf_link__destroy(links[i]);   /* link 销毁即卸载（第 2 章生命周期回指） */
    multi_observe_bpf__destroy(skel);
    return err;
}

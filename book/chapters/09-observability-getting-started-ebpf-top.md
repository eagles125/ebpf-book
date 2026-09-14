# 第 9 章 eBPF 可观测性入门：基于 ebpf.top 入门系列

> 本章当前为扩展版深度骨架，已经按 ebpf.top 的 BPF 入门系列 12 篇文章重组学习路径。
> 本章规划的独立示例已全部落地到 [`book/examples/chapter-09/`](../examples/chapter-09/)，
> 每个示例包含 BPF 源码、用户态 loader、Makefile、README（运行说明与验证状态），
> 已在本仓库编写环境完成编译验证；运行验证需特权环境，状态标注见各示例 README。
> 后续完整章节应逐篇核对原文细节，把正文从骨架扩写为成稿。

## 学习目标

完成本章后，读者应能够：

- 按照 ebpf.top BPF 入门系列的 12 篇文章建立从概念、环境、过滤、数据传输到网络钩子的完整入门路径。
- 编写 Hello World、PID 过滤、容器安全过滤、ring buffer、文件打开跟踪、perf_event、返回值跟踪、tracepoint、Netfilter、Go 控制面等示例。
- 理解每个示例的挂载点、上下文结构、map 设计、数据输出方式和 verifier 约束。
- 判断 `bpf_printk`、perf buffer、ring buffer、map 聚合和 Go loader 分别适合什么场景。
- 为后续 BCC、bpftrace、系统诊断、网络性能和 CO-RE/libbpf 章节打下可操作基础。

## 本章导读

第 1 章已经用一个 tracepoint Hello World 展示了 eBPF 工具链的最小闭环。本章不再停留在“能输出一行日志”，而是以 ebpf.top 的 BPF 入门系列为主线，逐步增加真实工具需要的能力：如何准备环境，如何过滤 PID，如何区分容器，如何使用 ring buffer 传输结构化事件，如何跟踪文件打开，如何使用 perf_event，如何抓函数返回值，如何使用 tracepoint，如何把 BPF 挂到 Netfilter，以及如何用 Go 写用户态控制面。

根据当前核对，ebpf.top BPF 入门系列包含 12 篇，路径如下：

1. eBPF 技术简介。
2. eBPF 开发学习路径总结。
3. eBPF 开发环境搭建。
4. eBPF 入门开发实践教程一：Hello World。
5. eBPF 入门开发实践教程二：通过 PID 过滤。
6. eBPF 入门开发实践教程三：借助 eBPF 保护容器安全。
7. eBPF 入门开发实践教程四：在 eBPF 中使用 ring buffer。
8. eBPF 入门开发实践教程五：捕获进程打开文件信息。
9. eBPF 入门开发实践教程六：捕获进程打开文件信息 perf_event 篇。
10. eBPF 入门开发实践教程七：捕获进程打开文件信息返回值。
11. eBPF 入门开发实践教程八：捕获进程打开文件信息 tracepoint 篇。
12. eBPF 入门开发实践教程九：在 Netfilter 中使用 eBPF 以及 Go 经验总结。

本章完整写作时要做到两件事：第一，尊重 ebpf.top 系列的学习顺序；第二，把每篇文章的核心知识转写成更系统的书籍内容，包括代码、命令、输出和解释，而不是简单罗列链接。

## 实验目录（已落地）

本章的独立示例已全部生成于 [`book/examples/chapter-09/`](../examples/chapter-09/)，目录总览和环境前提见其中的 [README](../examples/chapter-09/README.md)：

```text
book/examples/chapter-09/
  README.md                      目录总览、统一构建模式、验证状态说明
  00-env/check-env.sh            环境自检脚本
  01-hello-world/                hello.bpf.c + hello.c + Makefile + README
  02-pid-filter/                 pid_filter.bpf.c + pid_filter.c + Makefile + README
  03-container-security/         container_guard.bpf.c + container_guard.c + Makefile + README
  04-ring-buffer/                ringbuf_exec.bpf.c + ringbuf_exec.c + *_common.h + Makefile + README
  05-open-file-kprobe/           opensnoop_kprobe.bpf.c + opensnoop_kprobe.c + *_common.h + Makefile + README
  06-open-file-perf-event/       opensnoop_perf.bpf.c + opensnoop_perf.c + *_common.h + Makefile + README
  07-open-file-return/           openret.bpf.c + openret.c + *_common.h + Makefile + README
  08-open-file-tracepoint/       opensnoop_tp.bpf.c + opensnoop_tp.c + *_common.h + Makefile + README
  09-netfilter-go/               bpf/nf_guard.bpf.c + cmd/nfctl/main.go + go.mod + Makefile
                                 + scripts/netns-lab.sh（隔离拓扑） + README
```

每个示例都包含：内核态 BPF 程序、用户态 loader、编译命令、运行命令、预期输出、逐段解释、常见 verifier/权限/内核版本问题，以及明确的验证状态标注。示例 01–08 采用 libbpf-bootstrap 式三步构建（vmlinux.h -> BPF 编译 -> skeleton -> loader），`*_common.h` 是内核态与用户态共享的事件结构定义。

## 1. eBPF 技术简介

这一篇对应全系列的概念入口。书中应把它和第 1 章衔接起来，但不重复讲历史，而是强调“为什么后续示例都围绕可观测性展开”。

这一节需要把五个基础概念讲透。

第一，eBPF 程序不是一个独立运行的用户态线程。它不会像普通进程那样自己调度、睡眠、打开文件或发起网络连接，而是在某个内核事件发生时被内核调用。比如 Hello World 示例挂在 `tracepoint/syscalls/sys_enter_execve` 上，只有进程进入 `execve` 系统调用时才会执行；Netfilter 示例挂在网络钩子上，只有报文经过对应 hook 时才会执行。这一点决定了 eBPF 程序的写法：它必须快速、受限、无阻塞，并且围绕“当前事件上下文”读数据。

第二，程序类型决定上下文、helper 和返回动作。tracepoint 程序拿到的是 tracepoint 上下文，适合读取事件字段并返回 0；XDP 程序拿到 `struct xdp_md`，可以返回 `XDP_PASS`、`XDP_DROP`、`XDP_REDIRECT`；TC 程序拿到 `struct __sk_buff`，可以改包、重定向或继续放行。也就是说，“eBPF 能不能做某件事”不能泛泛回答，要先问程序挂在哪里。

第三，map 是保存状态和用户态交互的基础。没有 map 的 Hello World 只能输出一行日志；加入 map 后，用户态可以把目标 PID 写入 `target_pid`，内核态程序每次触发时读取它并过滤事件；再进一步，open 返回值示例可以用临时 hash map 关联 enter/exit 两个阶段。map 让 eBPF 从“无状态回调”变成“可配置、可聚合、可观察的工具”。

第四，verifier 是每个示例都绕不开的门槛。示例里每次读取字符串、读取包头、访问 map 返回指针，都必须让 verifier 能证明安全。例如 `bpf_map_lookup_elem()` 可能返回 NULL，所以代码必须先判断指针再解引用；XDP 解析报文时，每访问一个头部都要检查 `data + sizeof(header) <= data_end`。这不是语法洁癖，而是 eBPF 能在内核里安全运行的前提。

第五，libbpf/CO-RE 是现代可移植开发的主线。早期教程常依赖内核头文件和固定结构偏移，跨内核版本容易失效。现代实践通过 BTF 提供内核类型信息，配合 CO-RE 在加载时重定位字段偏移，再由 libbpf 负责加载、创建 map、attach 程序和生成 skeleton。后续示例即使先写成入门形态，也要逐步靠近这个工作流。

可视化：

```text
事件源：syscall / kprobe / tracepoint / netfilter / perf_event
        |
        v
eBPF 程序：读取上下文 -> 过滤 -> 读写 map -> 输出事件
        |
        v
用户态：loader / CLI / daemon / dashboard
```

## 2. eBPF 开发学习路径总结

这一篇应转化为本书的“实践学习路线图”。读者不应一开始就被 verifier、CO-RE、网络、Cilium 全部淹没，而应按能力递进：

```text
阶段 1：能编译和加载 Hello World
阶段 2：能在内核事件中读取上下文
阶段 3：能用 PID、命名空间、cgroup 做过滤
阶段 4：能用 map 保存状态
阶段 5：能用 perf/ring buffer 输出结构化事件
阶段 6：能处理函数返回值和错误码
阶段 7：能使用 tracepoint 提高稳定性
阶段 8：能进入网络钩子和 Go 控制面
```

本章后续示例正好对应这条路径。

## 3. eBPF 开发环境搭建

**独立示例**：[`book/examples/chapter-09/00-env/`](../examples/chapter-09/00-env/)（`check-env.sh` 环境自检脚本）。

完整章节应提供发行版命令和环境验证脚本。计划脚本：

```bash
#!/usr/bin/env bash
set -euo pipefail

printf 'kernel: '
uname -r

printf 'bpftool: '
bpftool version | head -n 1

if [ -r /sys/kernel/btf/vmlinux ]; then
  echo 'BTF: /sys/kernel/btf/vmlinux exists'
else
  echo 'BTF: missing; CO-RE examples may fail'
fi

if [ -d /sys/kernel/tracing/events ]; then
  echo 'tracefs: mounted at /sys/kernel/tracing'
else
  echo 'tracefs: not mounted; try sudo mount -t tracefs tracefs /sys/kernel/tracing'
fi
```

这些环境项不是“装上就行”的依赖清单，而是 eBPF 工具链的五个关键部件。

`clang/LLVM` 负责把受限 C 编译为 BPF 字节码。普通 C 程序编译成 x86_64 或 arm64 机器码，而 eBPF 内核态程序通常使用 `clang -target bpf` 编译成 BPF ELF 对象。例如 Hello World 的 `hello.bpf.c` 会生成 `hello.bpf.o`，里面包含 BPF 指令、map 定义、license、section 名称和 BTF/重定位信息。这里说“受限 C”，是因为 eBPF C 不是完整用户态 C：不能随意调用 libc，不能 malloc，不能阻塞睡眠，栈空间有限，循环和指针访问都要让 verifier 能证明安全。

`bpftool` 是学习和排错阶段最重要的瑞士军刀。它可以用 `bpftool feature probe` 检查当前内核支持哪些程序类型、map 类型和 helper；可以用 `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h` 从内核 BTF 生成 `vmlinux.h`；可以用 `bpftool gen skeleton hello.bpf.o > hello.skel.h` 生成 libbpf skeleton；还可以用 `bpftool prog list`、`bpftool map list` 查看系统里已经加载的程序和 map。当示例加载失败时，先用 bpftool 确认系统能力，通常比盲目改代码更快。

`libbpf-dev` 提供两类东西：用户态 loader API 和 BPF 侧头文件。用户态代码会调用 `bpf_object__open()`、`bpf_object__load()`、`bpf_program__attach()`，或者使用 skeleton 生成的 `xxx_bpf__open_and_load()`、`xxx_bpf__attach()`；BPF 侧代码会包含 `<bpf/bpf_helpers.h>`，使用 `SEC()`、`__uint()`、`__type()` 等宏来声明 section 和 map。没有 libbpf-dev，你可能能写 BPF C 文件，但很难用现代方式加载和管理它。

`BTF` 是 CO-RE 的类型信息基础。BTF 可以理解为更紧凑、面向 BPF 的内核类型描述。`/sys/kernel/btf/vmlinux` 存在时，工具可以知道当前内核里的 `task_struct`、tracepoint 上下文、网络结构等类型长什么样。CO-RE 依赖这些类型信息，在程序加载时把“我要读这个字段”重定位到当前内核的真实偏移。没有 BTF，很多老式示例仍可运行，但可移植性会明显下降。

`tracefs/debugfs` 是早期学习和排错的重要输出入口。Hello World 用 `bpf_printk()` 输出的文本不是写到 stdout，而是进入内核 tracing buffer，通常通过 `/sys/kernel/tracing/trace_pipe` 查看。tracefs 还暴露 tracepoint 列表和字段格式，例如 `events/syscalls/sys_enter_openat/format`。debugfs 在一些老系统上仍承载 tracing 路径。入门阶段如果“程序好像运行了但没有输出”，tracefs 是否挂载、trace_pipe 路径是否正确，是第一批要检查的问题。

## 4. Hello World

**独立示例**：[`book/examples/chapter-09/01-hello-world/`](../examples/chapter-09/01-hello-world/)。

Hello World 示例的目标是让读者第一次看到 eBPF 输出。相较第 1 章的 tracepoint skeleton 示例，本节应给出更贴近 ebpf.top 入门系列的版本，并解释 `bpf_trace_printk` 与 `bpf_printk` 的关系。

计划内核态代码：

```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

SEC("tracepoint/syscalls/sys_enter_execve")
int hello(void *ctx)
{
    bpf_printk("Hello, eBPF world!\n");
    return 0;
}
```

计划命令：

```bash
make
sudo ./hello
sudo cat /sys/kernel/tracing/trace_pipe
```

预期输出：

```text
... bpf_trace_printk: Hello, eBPF world!
```

这个 Hello World 示例要解释三个选择。

选择 `execve` 是因为它容易触发、语义清楚，而且输出不会像调度器事件那样刷屏。读者启动 loader 后，只要在另一个终端执行 `/bin/true`、`ls` 或 `echo test`，系统就会进入 `execve`，从而触发 `tracepoint/syscalls/sys_enter_execve`。这让读者能把“我执行了一个命令”和“trace_pipe 出现一行 eBPF 输出”直接对应起来。

`trace_pipe` 适合教学，是因为它把内核 tracing buffer 以文本流形式暴露出来。`bpf_printk("Hello, eBPF world!
")` 的结果不会出现在 loader 的 stdout，而是进入 `/sys/kernel/tracing/trace_pipe`。这能帮助读者理解 eBPF 程序运行在内核事件上下文中，输出路径和用户态 printf 不同。

生产代码不应大量使用 `bpf_printk`，原因是它做字符串格式化并写 tracing buffer，开销高且容易在高频事件下丢数据或干扰系统。真实工具通常只在开发和排错阶段保留少量 `bpf_printk`。当需要持续输出事件时，应使用 ring buffer 或 perf buffer；当只需要统计时，应使用 map 聚合，让用户态周期性读取。

## 5. 通过 PID 过滤

**独立示例**：[`book/examples/chapter-09/02-pid-filter/`](../examples/chapter-09/02-pid-filter/)。

没有过滤的 eBPF 程序会捕获系统中所有匹配事件。入门阶段很快会遇到噪声问题：你只想观察目标进程，却看到 shell、systemd、容器 runtime、监控 agent 的大量事件。PID 过滤是最小过滤能力。

计划 map 设计：

```c
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} target_pid SEC(".maps");
```

计划 eBPF 过滤逻辑：

```c
__u32 key = 0;
__u32 current = bpf_get_current_pid_tgid() >> 32;
__u32 *target = bpf_map_lookup_elem(&target_pid, &key);

if (target && *target != 0 && *target != current)
    return 0;
```

用户态运行方式：

```bash
sudo ./pid_filter --pid 12345
```

这里的关键是理解 PID 过滤为什么要通过 helper 和 map 组合完成。

`bpf_get_current_pid_tgid()` 返回一个 64 位值，高 32 位通常是线程组 ID，也就是用户经常理解的进程 PID；低 32 位是当前线程 ID。很多入门工具写 `bpf_get_current_pid_tgid() >> 32`，是为了按进程过滤，而不是按单个线程过滤。如果读者要跟踪多线程程序中的某个具体线程，才需要关注低 32 位。

用 array map 存目标 PID，比把 PID 写死在 BPF 程序里更灵活。假设把 `12345` 直接写进 `pid_filter.bpf.c`，每换一个进程都要重新编译、重新加载、重新挂载程序。使用 `BPF_MAP_TYPE_ARRAY` 后，内核态程序只读 key 0 对应的 value；用户态 loader 可以在启动时写入 `--pid 12345`，也可以运行中更新为另一个 PID。

用户态更新 map 后，内核态程序下一次触发即可看到新值，因为 map 是内核对象，不是编译期常量。示例里的逻辑 `if (target && *target != 0 && *target != current) return 0;` 表示：如果配置了非 0 目标 PID，并且当前进程不匹配，就直接返回，不输出事件。这样过滤发生在内核事件现场，噪声不会再被传到用户态。

## 6. 借助 eBPF 保护容器安全

**独立示例**：[`book/examples/chapter-09/03-container-security/`](../examples/chapter-09/03-container-security/)。

这一篇把过滤维度从 PID 扩展到容器。容器环境中，单纯 PID 不稳定：容器会重启，进程会变化，同一个宿主机上有多个 namespace。更实际的做法是结合 cgroup id、mount namespace、pid namespace 或容器 runtime 暴露的元数据。

计划示例目标：拦截或记录容器内进程访问敏感路径的行为。

计划事件：

```text
进程打开文件 -> eBPF 读取 comm/pid/cgroup_id/path 信息 -> 判断是否属于目标容器 -> 输出事件或拒绝
```

计划使用的 helper：

```c
__u64 cgid = bpf_get_current_cgroup_id();
```

计划 map：

```c
// key: cgroup id
// value: policy flags，例如 allow/deny/log-only
BPF_MAP_TYPE_HASH target_cgroups;
```

容器安全示例要把“识别容器”和“执行安全策略”分开讲。

cgroup id 适合作为容器级过滤线索，是因为现代容器运行时通常把容器进程放进独立 cgroup。PID 会随着容器重启变化，进程名也可能伪装，但 cgroup 层级更接近资源隔离和容器生命周期。eBPF 程序可以用 `bpf_get_current_cgroup_id()` 获取当前任务所属 cgroup id，再到 `target_cgroups` map 中查询是否需要记录或应用策略。

安全策略必须区分“观测”和“阻断”。如果程序挂在 tracepoint 或普通 kprobe 上，它通常只能记录事件，不能可靠地改变系统调用结果。比如你可以在 openat tracepoint 上看到容器进程正在打开 `/etc/shadow`，但不能因此声称这个 tracepoint 程序已经阻止了打开。要做阻断，应选择有决策语义的挂载点，例如 LSM BPF 钩子、cgroup hooks，或网络路径上的 XDP/TC/Netfilter 返回动作。

因此，本章示例要明确边界：容器安全的入门版先做“识别目标容器并输出敏感行为事件”。如果要扩展为阻断版，需要切换到合适的程序类型，并说明返回值如何影响内核决策。这样读者不会把观测型探针误用成访问控制系统。

## 7. 在 eBPF 中使用 ring buffer

**独立示例**：[`book/examples/chapter-09/04-ring-buffer/`](../examples/chapter-09/04-ring-buffer/)。

ring buffer 是现代 eBPF 事件输出的重要机制。相比 `bpf_printk`，它传输结构化数据；相比 perf buffer，它提供 MPSC ring buffer 语义，用户态读取更直接。

计划事件结构：

```c
struct event {
    __u32 pid;
    __u64 cgroup_id;
    char comm[16];
};
```

计划 map：

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");
```

计划 eBPF 输出逻辑：

```c
struct event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
if (!e)
    return 0;

e->pid = bpf_get_current_pid_tgid() >> 32;
e->cgroup_id = bpf_get_current_cgroup_id();
bpf_get_current_comm(&e->comm, sizeof(e->comm));
bpf_ringbuf_submit(e, 0);
```

ring buffer 示例的核心是事件对象的生命周期。

`bpf_ringbuf_reserve()` 从 ring buffer 中预留一块连续空间，返回指向事件结构的指针。只有 reserve 成功后，eBPF 程序才能填充 `pid`、`cgroup_id`、`comm` 等字段。填充完成后必须调用 `bpf_ringbuf_submit()` 提交事件，用户态才可能读到它。如果中途发现事件不应输出，或者读取字符串失败，可以调用 `bpf_ringbuf_discard()` 丢弃预留空间。reserve 之后既不 submit 也不 discard，会导致 verifier 拒绝或运行路径不完整。

ring buffer 满时，`bpf_ringbuf_reserve()` 会返回 NULL。示例里的 `if (!e) return 0;` 不是随便写的防御代码，而是高频事件下必须考虑的背压路径。入门程序可以简单丢弃事件；生产程序通常还会增加 per-CPU drop counter，让用户态知道有多少事件因为缓冲区不足被丢掉。

事件结构建议固定大小，是为了让 verifier 和用户态 reader 都能明确事件边界。比如 `char comm[16]` 对应内核任务名长度，文件路径可以设置为 `char filename[256]` 或 `char filename[128]`。读取字符串时要用 `bpf_probe_read_user_str()` 或相关 helper，并传入目标缓冲区大小，不能假设用户指针一定有效，也不能越界写入事件结构。

用户态 `ring_buffer__poll()` 负责等待和消费事件。它通常接收一个超时时间，例如 100ms 或 1000ms。超时不是错误，只表示这段时间没有事件；返回负值才需要按错误处理。这个行为适合写 daemon：主循环可以一边 poll 事件，一边响应退出信号或刷新统计。

## 8. 捕获进程打开文件信息

**独立示例**：[`book/examples/chapter-09/05-open-file-kprobe/`](../examples/chapter-09/05-open-file-kprobe/)。

这一篇进入真实 opensnoop 类工具。目标是捕获进程打开文件时的文件名、进程名、PID 等信息。

可选挂载点：

- kprobe：`do_sys_openat2`、`do_filp_open` 或架构相关 syscall wrapper。
- tracepoint：`syscalls:sys_enter_openat`、`syscalls:sys_enter_openat2`。

kprobe 版本计划读取：

```c
// 从函数参数中读取 filename 指针，再用 bpf_probe_read_user_str 复制字符串。
```

预期输出：

```text
PID     COMM             FILE
12345   cat              /etc/hosts
12346   nginx            /var/log/nginx/access.log
```

文件打开跟踪示例最容易踩指针和时序两个坑。

用户指针与内核指针不能混用。系统调用参数里的 filename 通常是用户态地址，指向调用进程地址空间中的字符串；内核结构里的字段则是内核地址。eBPF verifier 会跟踪指针类型，不允许你把任意整数当指针解引用。读取用户态 filename 时应使用 `bpf_probe_read_user_str()`，读取内核结构字符串时才考虑 `bpf_probe_read_kernel_str()`。

`bpf_probe_read_user_str()` 和 `bpf_probe_read_kernel_str()` 的区别在于来源地址空间不同。前者处理用户地址，可能遇到缺页、无效地址或字符串过长；后者处理内核地址，也同样受 verifier 和 helper 约束。示例要把这两者分开讲，否则读者在 kprobe、tracepoint、LSM 示例之间迁移代码时很容易出现 verifier 错误或运行时读取失败。

文件名通常在 enter 阶段最容易拿到，因为系统调用刚进入内核时参数仍然可见。但 open 是否成功、返回的 fd 是多少、错误码是什么，要到 exit 阶段才知道。这也是为什么后续返回值示例需要在 enter 阶段把 filename 暂存到 map，exit 阶段再按 pid 查回它并补上返回值。

## 9. 捕获进程打开文件信息 perf_event 篇

**独立示例**：[`book/examples/chapter-09/06-open-file-perf-event/`](../examples/chapter-09/06-open-file-perf-event/)。

这一篇与 ring buffer 形成对照：同样输出结构化事件，但使用 perf event array。

计划 map：

```c
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} events SEC(".maps");
```

计划输出：

```c
struct event event = {};
bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event));
```

perf_event 篇要解释为什么同样是输出事件，还要学习 perf buffer。

perf buffer 基于 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`，通常按 CPU 建立事件通道。eBPF 程序调用 `bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event))` 时，会把事件写到当前 CPU 对应的 perf buffer。用户态 reader 需要为这些 per-CPU buffer 建立回调，并持续 poll。这样做可以减少跨 CPU 竞争，但也意味着用户态要处理每个 CPU 的缓冲和丢事件统计。

libbpf 用户态一般使用 `perf_buffer__new()` 创建 reader，传入 map fd、每 CPU 页数、事件回调和丢失回调，再用 `perf_buffer__poll()` 进入循环。事件回调负责把原始字节解释成 `struct event`；丢失回调负责记录 lost samples。完整示例要让读者看到：内核态输出结构体，用户态按同一个结构体布局解析。

与 ring buffer 相比，perf buffer API 更老，使用面更广，很多 BCC 时代工具和旧内核环境仍然依赖它。ring buffer 在现代 libbpf 项目中更简洁，但不能因此跳过 perf buffer。给同一 opensnoop 事件写两个版本，可以让读者直观看到两者差异：BPF 侧一个调用 `bpf_perf_event_output()`，另一个调用 `bpf_ringbuf_reserve/submit()`；用户态一个用 `perf_buffer__poll()`，另一个用 `ring_buffer__poll()`。

## 10. 捕获进程打开文件信息返回值

**独立示例**：[`book/examples/chapter-09/07-open-file-return/`](../examples/chapter-09/07-open-file-return/)。

只捕获 open enter 事件还不够。很多排障问题关心的是“打开失败了吗，错误码是什么”。返回值需要在函数返回或 syscall exit 时捕获。

计划路径：

```text
enter 阶段：记录 pid -> filename
exit 阶段：读取返回值 ret，关联 pid 查 filename，输出并删除临时状态
```

计划临时 map：

```c
// key: pid
// value: filename/comm/timestamp
BPF_MAP_TYPE_HASH inflight;
```

计划输出：

```text
PID     COMM        RET     FILE
1001    cat         3       /etc/hosts
1002    cat         -2      /not-exist
```

返回值示例要把“两个事件拼成一次完整操作”讲清楚。

enter 和 exit 需要通过 map 关联，是因为 openat 进入时能看到 filename，但还不知道结果；退出时能看到返回值，但 filename 参数未必还方便读取。常见做法是在 enter 阶段用 pid/tid 作为 key，把 filename、comm、时间戳写入 `inflight` map；exit 阶段用同一个 key 查回临时状态，加上返回值后输出完整事件。

返回值为负数时通常表示 `-errno`。例如 `-2` 对应 `ENOENT`，表示文件不存在；非负值通常是文件描述符 fd。示例输出不能只打印数字，还应解释如何把负返回值映射到错误原因。完整工具可以在用户态把 `-ret` 转换为 errno 名称和错误字符串。

临时 map 必须在 exit 阶段删除。否则一个进程多次打开文件后，旧 filename 会残留，后续 exit 可能关联到错误数据；长时间运行的工具还会让 map 持续增长。正确路径是：enter 更新 map，exit 查找 map，输出事件后 `bpf_map_delete_elem()` 清理。即使 exit 阶段发现读取返回值异常，也应考虑清理临时项。

key 设计要谨慎。只用 tgid 可能在多线程程序中冲突，因为同一进程多个线程可能同时执行 open；通常更安全的是使用 `pid_tgid` 的完整 64 位值，或者至少用低 32 位 tid 区分线程。遇到异步、嵌套或递归场景时，还可能需要额外时间戳或栈层次信息。

## 11. 捕获进程打开文件信息 tracepoint 篇

**独立示例**：[`book/examples/chapter-09/08-open-file-tracepoint/`](../examples/chapter-09/08-open-file-tracepoint/)。

tracepoint 版本强调稳定性。与 kprobe 绑定内核函数名不同，tracepoint 是内核显式提供的事件点，并通过 tracefs 暴露字段格式。

发现 tracepoint：

```bash
sudo ls /sys/kernel/tracing/events/syscalls/sys_enter_openat
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/format
sudo cat /sys/kernel/tracing/events/syscalls/sys_exit_openat/format
```

计划 section：

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int enter_openat(struct trace_event_raw_sys_enter *ctx) { ... }

SEC("tracepoint/syscalls/sys_exit_openat")
int exit_openat(struct trace_event_raw_sys_exit *ctx) { ... }
```

tracepoint 篇要强调“稳定，但仍要核对”。

tracepoint 字段来自 tracefs 的 `format` 文件。比如 `syscalls/sys_enter_openat/format` 会列出事件字段、偏移、大小和字段名。使用 tracepoint 时，读者不需要猜某个内核内部函数在当前版本叫什么、参数顺序是什么，而是按照内核暴露的事件格式读取上下文。这就是 tracepoint 比 kprobe 更适合长期维护工具的原因。

syscall tracepoint 的参数通常放在 `ctx->args[]` 中。以 openat 为例，`args[0]` 可能是 `dfd`，`args[1]` 是用户态 filename 指针，`args[2]` 是 flags，具体要以 `format` 为准。示例应展示如何先查看 `format`，再在 BPF 代码中把 `ctx->args[1]` 转成用户指针并用 `bpf_probe_read_user_str()` 读取。

tracepoint 的稳定性不等于永远不变。不同架构、不同 syscall ABI、不同内核配置可能导致字段或事件可用性差异。完整章节应要求读者先运行 `ls /sys/kernel/tracing/events/syscalls/` 和 `cat .../format`，再编译对应示例。这样做比直接复制别人机器上的上下文结构更可靠。

## 12. 在 Netfilter 中使用 eBPF 以及 Go 经验总结

**独立示例**：[`book/examples/chapter-09/09-netfilter-go/`](../examples/chapter-09/09-netfilter-go/)（含 `scripts/netns-lab.sh` 隔离拓扑脚本）。

系列最后一篇把学习从观测推进到网络钩子和 Go 控制面。Netfilter BPF 与 XDP/TC 的侧重点不同：它接近传统防火墙路径，适合解释报文过滤、策略判断和用户态控制面的关系。

计划示例目标：

```text
Go 控制面加载 BPF 程序 -> 挂到 Netfilter hook -> map 下发阻断规则 -> 输出命中统计
```

计划 Go 结构：

```go
package main

// 后续完整章节使用 cilium/ebpf 或 libbpfgo/libbpf-rs 方案之一。
// 目标：加载对象、打开 map、写入规则、attach netfilter hook、打印统计。
func main() {}
```

Netfilter + Go 示例要让读者第一次看到“控制面/数据面分离”的工程形态。

Go 用户态负责控制面，C eBPF 程序负责数据面。数据面在内核网络路径上高频执行，只做解析报文、查 map、更新统计、返回动作；Go 控制面负责加载对象、挂载程序、解析命令行、把规则写入 map、读取统计、优雅退出。这样分工后，规则变更不需要重新编译 BPF 程序，控制面也不会进入高频报文处理路径。

map pinning 用来把 map 固定到 bpffs，例如 `/sys/fs/bpf/nf_guard/rules`。如果不 pin，map 生命周期通常跟 loader 进程和 fd 绑定，进程退出后 map 可能被释放。pin 后，另一个控制面进程可以重新打开同一个 map，读取或更新规则。生产工具常用 pinning 支持 daemon 重启、状态保留和多进程协作，但也要做好清理，避免留下过期 map。

Netfilter、XDP、TC 的钩子位置不同。XDP 在驱动收包早期，适合极高吞吐的早期丢包；TC 位于 qdisc ingress/egress，和 skb、容器 veth、改包动作结合更自然；Netfilter 位于传统防火墙路径，适合理解 hook、协议栈阶段和策略过滤。入门示例选择 Netfilter，是为了把 eBPF 与经典防火墙概念连接起来；第 15 至第 17 章会系统比较 XDP 和 TC。

网络路径示例必须在隔离拓扑中验证。一个错误的 drop 规则可能让当前 SSH 连接断开，一个错误的 NAT 校验和可能污染宿主机网络排障。完整章节应使用 network namespace、veth pair 和专用测试地址构造实验环境，所有 attach/detach 脚本都要有清理路径。只有在隔离拓扑通过后，才考虑真实网卡实验。

## 横向对比：输出机制选择

| 输出机制 | 适用阶段 | 优点 | 限制 |
|---|---|---|---|
| `bpf_printk` | Hello World、排错 | 最简单，直接看 `trace_pipe` | 高开销，不适合生产高频事件 |
| map 计数 | 聚合统计 | 低输出量，适合指标 | 只能表达聚合状态，不适合详细事件流 |
| perf buffer | 结构化事件 | 兼容老内核，生态成熟 | per-CPU buffer 管理较复杂 |
| ring buffer | 结构化事件 | API 简洁，现代 libbpf 推荐 | 需要较新内核 |
| Prometheus exporter | 生产观测 | 易接入 Grafana | 需要用户态 daemon 和指标设计 |

## 可视化：12 篇文章能力递进图

```text
技术简介
  |
学习路径
  |
环境搭建
  |
Hello World
  |
PID 过滤
  |
容器安全过滤
  |
ring buffer 结构化输出
  |
打开文件 enter 事件
  |
perf_event 输出版本
  |
返回值关联 enter/exit
  |
tracepoint 稳定版本
  |
Netfilter + Go 控制面
```

## 常见问题与排错

**为什么第一个示例建议从 tracepoint 开始，而不是 kprobe？**

tracepoint 更稳定，字段格式可通过 tracefs 查看。kprobe 灵活，但绑定具体内核函数名和参数布局，初学者更容易遇到内核版本差异。

**为什么同样是打开文件，要写 kprobe、perf_event、返回值、tracepoint 多个版本？**

这是为了让读者理解同一业务问题可以用不同 eBPF 机制表达。kprobe 展示动态追踪，perf_event 展示结构化输出，返回值版本展示 enter/exit 状态关联，tracepoint 版本展示稳定事件接口。

**ring buffer 是否可以完全替代 perf buffer？**

新项目通常优先考虑 ring buffer，但老内核、既有代码和部分工具链仍会使用 perf buffer。本章保留两者对照，避免读者只会一种输出路径。

**容器安全示例能不能直接阻断危险操作？**

要看挂载点。tracepoint/kprobe 更适合观测，不应被描述成通用阻断机制。真正要做阻断，应考虑 LSM BPF、cgroup hooks、网络 hooks 或其他具备返回决策能力的程序类型。

## 完整章节验收清单

独立示例资产已落地并通过编译验证（见各示例 README 的验证状态）；
以下清单用于完整章节定稿前的最终验收：

- [ ] 逐篇核对 ebpf.top BPF 入门系列 12 篇文章标题与核心内容。
- [ ] 每篇文章对应一个小节，且不是只列提纲。
- [x] Hello World 示例已落地（编译验证通过；运行验证待特权环境）。
- [x] PID 过滤示例包含 map 配置和用户态参数下发。
- [x] 容器安全示例明确 cgroup/container 过滤依据和阻断边界。
- [x] ring buffer 示例包含 reserve/submit 和用户态 poll。
- [x] open 文件 kprobe 示例包含文件名读取解释。
- [x] perf_event 版本包含 perf buffer 用户态读取。
- [x] 返回值版本包含 enter/exit 关联 map。
- [x] tracepoint 版本引用 tracefs `format`。
- [x] Netfilter + Go 示例包含隔离网络拓扑和控制面说明。
- [x] 每个示例包含编译命令、运行命令、预期输出和排错。
- [ ] 在特权 Linux eBPF 环境完成全部示例的运行验证（或保持显式标注）。

## 总结

本章应成为全书第一个真正“动手密集”的章节。它不是列出几个工具名，而是围绕 ebpf.top BPF 入门系列的 12 篇文章，把读者从概念带到一组递进代码：先输出文本，再过滤 PID，再识别容器，再传输结构化事件，再跟踪文件打开，再关联返回值，再迁移到 tracepoint，最后进入 Netfilter 和 Go 控制面。本章规划的 10 个独立示例已全部落地于 `book/examples/chapter-09/` 并通过编译验证；剩余工作是在特权环境完成运行验证，并把正文从骨架扩写为成稿。

## 本章参考来源

- ebpf.top: BPF 入门系列 1-12，包括技术简介、学习路径、环境搭建、Hello World、PID 过滤、容器安全、ring buffer、文件打开跟踪、perf_event、返回值、tracepoint、Netfilter 与 Go 经验总结。
- Linux kernel documentation: BPF maps, helper functions, tracing programs, perf event output, ring buffer, Netfilter BPF hooks。
- BCC tutorial and examples。
- bpftrace one-liners tutorial。
- libbpf and libbpf-bootstrap examples。
- cilium/ebpf Go library documentation。

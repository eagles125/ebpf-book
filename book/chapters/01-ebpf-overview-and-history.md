# 第 1 章 eBPF 概述与历史

## 学习目标

读完本章后，你应当能够：

- 解释 classic BPF 与 eBPF 的关系，以及 eBPF 为什么从一个包过滤机制演进为通用内核可编程基础设施。
- 说清 eBPF 在可观测性、网络和安全领域的典型应用，并能判断哪些问题适合用 eBPF 解决。
- 理解 eBPF 与传统内核模块相比的安全边界、发布方式和工程取舍。
- 识别 eBPF 程序从编译、加载、验证、JIT 编译到挂载执行的基本路径。
- 编写并理解一个最小 tracepoint eBPF 程序，知道内核态程序、用户态 loader、Makefile 和运行输出分别承担什么职责。

## 本章导读

eBPF 常被描述为“在 Linux 内核中运行的安全小程序”。这个说法方便入门，但不够完整。更准确地说，eBPF 是 Linux 内核提供的一套受限指令集、加载接口、验证器（verifier）、映射存储（map）、辅助函数（helper）、程序类型（program type）和挂载点（attachment point）机制。用户态程序把 eBPF 字节码提交给内核；内核验证它不会破坏安全边界；通过验证后，程序被解释执行或 JIT 编译为本机机器码；当内核事件、网络包、系统调用、tracepoint 或安全钩子触发时，eBPF 程序被调用。

这种机制让开发者在不修改内核源码、不重启系统、不加载传统内核模块的情况下，把一小段逻辑插入内核关键路径。它既能回答“哪个进程导致磁盘延迟升高”，也能在网卡收包早期丢弃恶意流量，还能在 Kubernetes 集群里替代部分 iptables/kube-proxy 数据路径。理解 eBPF 的关键，不是把它看成某一个工具，而是把它看成一组“内核可编程基础设施”：指令集定义程序能表达什么，verifier 定义程序能安全做到什么，map 定义程序如何保留状态，helper 定义程序能请求内核帮它做什么，挂载点定义程序在什么时候被执行。

## 从 classic BPF 到 eBPF

BPF 最早指 Berkeley Packet Filter。classic BPF 的目标很明确：让抓包工具在内核中快速过滤网络包，避免把所有报文都复制到用户空间。tcpdump 使用的过滤表达式，例如 `tcp port 80`，会被编译为 classic BPF 指令，由内核在包路径上执行。这样做的收益很直接：如果应用只关心少量报文，内核就不必把无关报文全部复制给用户态进程。

classic BPF 的指令模型很小，寄存器和可表达能力有限，适合包过滤，但不适合表达更复杂的内核观测和网络处理逻辑。它的世界主要围绕“给定一个包，返回接受还是丢弃”。随着 Linux 对动态追踪、网络虚拟化、容器隔离和安全审计的需求增加，内核需要一种更通用但仍然可控的机制。

eBPF 中的 “e” 来自 extended。扩展后的 BPF 引入了 64 位寄存器、更多指令、map、helper、尾调用、BPF-to-BPF 函数调用、更多程序类型和更丰富的挂载点。它仍然继承了 BPF 的核心思想：让内核在受控环境中运行小程序。但现代 eBPF 的目标已经从“包过滤表达式”扩展为“安全、可验证、可组合的内核可编程机制”。

可以把这段演进粗略理解为三步：

```text
classic BPF
  目标：抓包过滤
  特征：小指令集、少量寄存器、网络包上下文
        |
        v
early eBPF
  目标：更通用的内核事件处理
  特征：64 位寄存器、map、helper、verifier、JIT
        |
        v
modern eBPF
  目标：可观测性、网络、安全、调度、存储等内核可编程基础设施
  特征：BTF、CO-RE、fentry/fexit、LSM、ring buffer、libbpf skeleton
```

这并不表示 classic BPF 消失了。Linux 内核中仍然能看到 classic BPF 兼容路径和 socket filter 场景。只是今天多数工程实践谈到 “BPF” 或 “eBPF” 时，通常指现代 eBPF 子系统。你在阅读内核源码和工具文档时也会看到“BPF”这个简称，它经常泛指现代 eBPF，而不是只指早期 classic BPF。

## eBPF 解决了什么问题

Linux 内核是系统中最关键的执行层。很多问题只有内核能看见：调度延迟、阻塞 I/O、TCP 重传、容器 veth 路径、系统调用参数、文件打开失败、内存分配热点、网络包在协议栈中的命运。传统做法主要有三类：

- 修改内核源码并重新编译。
- 编写并加载内核模块。
- 从用户态通过 `/proc`、`/sys`、日志或采样工具间接观察。

这些方法各有问题。修改内核和加载模块灵活但风险高，发布慢，错误可能直接导致内核崩溃。用户态间接观察安全但视野有限，很多关键上下文丢失。eBPF 试图在二者之间提供折中：允许动态插入逻辑，但把逻辑限制在 verifier 可以证明安全的范围内。

从工程角度看，eBPF 的价值主要体现在五点。

第一是动态性。传统内核改动通常需要构建、发布、重启或至少重新加载模块。eBPF 程序可以由用户态 loader 动态加载、挂载、卸载，适合现场诊断和快速迭代。

第二是低开销。eBPF 程序在事件发生处运行，可以在内核里先过滤和聚合，再把少量有价值的数据送到用户态。JIT 开启后，很多程序会被编译为本机指令执行，避免解释器开销。

第三是安全边界。eBPF 程序加载前必须经过 verifier。verifier 会检查控制流、寄存器状态、指针类型、内存边界、栈使用、helper 调用约束等。它不能证明程序“业务逻辑一定正确”，但会拒绝明显可能破坏内核安全的程序。

第四是共享状态。map 让 eBPF 程序可以在事件之间保存状态，也让用户态控制面可以读取和更新这些状态。例如网络防火墙可以把 ACL 规则放进 map，观测工具可以把延迟直方图放进 per-CPU array。

第五是生态工具。BCC、bpftrace、libbpf、bpftool、Cilium、Pixie 等项目分别面向快速诊断、脚本化追踪、生产级可移植开发、系统检查和平台化网络/观测场景。学习 eBPF 时不需要一开始就掌握所有工具，但要知道它们处在同一套内核能力之上。

## 典型应用领域

### 可观测性

可观测性是很多人接触 eBPF 的入口。eBPF 可以挂到 kprobe、tracepoint、raw tracepoint、fentry/fexit、uprobes、perf event 等位置，采集内核函数调用、系统调用参数、延迟分布、进程生命周期和网络事件。

例如，线上服务突然出现请求延迟升高。只看应用日志可能只能看到“请求慢了”，但不知道慢在 CPU、磁盘、锁等待、DNS、TCP 重传还是下游服务。eBPF 工具可以在多个层面插桩：在调度器事件上看线程等待，在块设备请求上看 I/O 延迟，在 TCP 事件上看重传，在系统调用上看文件和网络操作。由于程序在内核事件处运行，很多信息不需要事后猜测。

典型问题包括：

- 哪些进程频繁调用 `execve`？
- 哪些文件被打开失败，错误码是什么？
- 块设备 I/O 延迟分布长什么样？
- TCP 连接建立、重传、RTO 和零窗口事件发生在哪里？
- CPU 火焰图和 off-CPU 火焰图分别揭示了什么瓶颈？

BCC 和 bpftrace 是这一领域的代表工具。BCC 适合写 Python 驱动的追踪工具，bpftrace 适合快速写一行命令或小脚本做现场诊断。后续第 10 章和第 11 章会分别展开。

### 网络

eBPF 在网络路径中的价值更直接：它可以在网络包进入协议栈前执行，也可以在 TC、cgroup、socket 等层次执行。XDP（eXpress Data Path）允许程序在驱动收包早期处理报文，常用于 DDoS 防御、快速丢包、负载均衡和重定向。TC BPF 则适合在内核网络栈中进行分类、限速、封装、策略路由和容器网络处理。

一个简单例子是黑名单丢包。如果用传统防火墙在协议栈较晚阶段处理，内核可能已经为报文分配了 skb，并走过一段协议栈逻辑。XDP 程序可以在更早的位置读取以太网头、IP 头和端口，命中规则后直接返回 `XDP_DROP`。在高 PPS 攻击流量下，越早丢包越能保护后面的协议栈和应用。

Cilium 是 eBPF 网络工程化的重要代表。它使用 eBPF 实现 Kubernetes 网络策略、服务负载均衡、可观测性和部分 kube-proxy 替代能力。Hubble 则在 Cilium 之上提供服务流量可视化和网络事件观测。

### 安全

安全领域关注“谁做了什么”以及“是否允许这么做”。eBPF 可以用于系统调用审计、文件访问审计、网络访问控制、容器逃逸检测和 LSM（Linux Security Module）钩子策略。与传统内核模块相比，eBPF 程序被 verifier 限制，不能随意写内核内存；与纯用户态审计相比，它又能在更接近事件源的位置观察和决策。

安全领域也必须看到另一面：eBPF 能力很强，如果不限制谁能加载程序，攻击者也可能利用它隐藏行为或绕过观测。因此生产环境需要关注 `kernel.unprivileged_bpf_disabled`、能力边界、程序来源、bpffs 挂载、JIT 设置和审计策略。第 24 章会专门讨论生产化安全加固。

## eBPF 与内核模块的对比

传统内核模块几乎可以做任何事。它能调用大量内核符号、创建设备、修改内核状态，也可能因为一个空指针或竞态让系统崩溃。eBPF 的设计目标不是替代所有内核模块，而是在常见观测和数据路径扩展场景中提供更安全、更轻量的机制。

| 维度 | eBPF | 内核模块 |
|---|---|---|
| 加载前检查 | verifier 静态验证程序安全性 | 主要依赖模块作者和内核接口约束 |
| 能力范围 | 受程序类型、helper、上下文、map 限制 | 接近完整内核权限 |
| 发布方式 | 可动态加载卸载，常由用户态 loader 管理 | 需要编译匹配内核 ABI，风险更高 |
| 崩溃风险 | verifier 和运行时限制降低风险 | bug 可能直接导致 kernel panic |
| 性能 | JIT 后很高，但受限制 | 理论上更自由，可做更复杂优化 |
| 适用场景 | 观测、过滤、策略、轻量数据路径处理 | 驱动、文件系统、复杂内核子系统扩展 |

一个实用判断是：如果需求可以表达为“在某类内核事件发生时读取上下文、更新 map、输出事件、改变有限的返回值或包处理动作”，优先考虑 eBPF。如果需求需要长期持有复杂内核对象、深度修改内核子系统或实现完整驱动，内核模块仍然可能是正确工具。

## eBPF 的基本执行路径

一个现代 eBPF 程序通常由两部分组成：

- 内核态 eBPF 程序：通常用受限 C 写成，由 clang 编译为 BPF 字节码。
- 用户态 loader 或控制面：负责加载对象文件、设置 map 初值、挂载程序、读取事件和展示结果。

基本路径如下：

```text
开发者编写 C 代码
        |
        v
clang -target bpf 编译为 ELF 对象
        |
        v
用户态 loader 调用 bpf() 系统调用
        |
        v
kernel/bpf/syscall.c 处理 BPF_PROG_LOAD / BPF_MAP_CREATE
        |
        v
kernel/bpf/verifier.c 验证控制流、寄存器状态、指针安全、边界
        |
        v
解释执行或 arch/*/net/bpf_jit*.c JIT 编译
        |
        v
挂载到 kprobe / tracepoint / XDP / TC / cgroup / LSM 等位置
        |
        v
事件触发时运行，读写 map，调用 helper，向用户态输出结果
```

这里有几个关键组件：指令集、map、helper、verifier、JIT、BTF 与 CO-RE。第 2 章会展开指令集、寄存器和 map；第 3 章会专讲 verifier；第 5 至第 7 章会进入 `kernel/bpf/`、`net/core/filter.c` 和 JIT 实现；第 8、20、21 章会围绕 BTF、CO-RE 和 libbpf 可移植开发展开。

## 实战：最小 tracepoint eBPF 程序

前面的介绍仍然偏概念。下面用一个最小程序把 eBPF 的几个部件串起来：当进程进入 `execve` 系统调用时，eBPF 程序输出当前进程名和 PID。这个例子不是生产工具，但它足以展示内核态程序、用户态 loader、编译命令、挂载点和输出路径如何配合。

### 环境要求

```bash
sudo apt-get install -y clang llvm make gcc libbpf-dev bpftool
```

检查基础能力：

```bash
uname -a
bpftool version
ls -lh /sys/kernel/btf/vmlinux || true
sudo mount -t tracefs tracefs /sys/kernel/tracing 2>/dev/null || true
```

如果 `/sys/kernel/btf/vmlinux` 不存在，本例仍可改成包含发行版内核头的传统方式；但后续 CO-RE 章节会强烈依赖 BTF。为了让第一个例子尽量贴近现代 libbpf，本节采用 `vmlinux.h + skeleton` 工作流。

### 文件结构

```text
hello-exec/
  hello_exec.bpf.c
  hello_exec.c
  Makefile
```

### 内核态程序：`hello_exec.bpf.c`

```c
// hello_exec.bpf.c
// 这个文件会被 clang 编译为 BPF 字节码，最终在内核 tracepoint 上运行。
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(struct trace_event_raw_sys_enter *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;
    char comm[16];

    bpf_get_current_comm(&comm, sizeof(comm));
    bpf_printk("hello eBPF: pid=%d comm=%s\n", pid, comm);
    return 0;
}
```

这段代码有几个关键点。

`SEC("tracepoint/syscalls/sys_enter_execve")` 把函数放进 ELF 的特定 section。libbpf loader 读取 ELF section 名称后，会知道这个程序应该挂载到 `syscalls:sys_enter_execve` tracepoint。section 名称不是普通注释，它是用户态 loader 和内核挂载点之间的约定。

`struct trace_event_raw_sys_enter *ctx` 是 tracepoint 上下文。这个 Hello World 示例没有读取系统调用参数，只用它满足函数签名。第 9 章会进一步读取 `execve` 的文件名参数，并解释 tracefs `format` 文件如何描述 tracepoint 字段。

`bpf_get_current_pid_tgid()` 和 `bpf_get_current_comm()` 是 helper。eBPF 程序不能任意调用内核函数，只能调用当前程序类型允许的 helper。helper 的可用性由内核和程序类型共同决定，后续第 7 章会从内核实现角度展开。

`bpf_printk()` 是教学阶段常用输出方式。它最终把文本写入 tracing 缓冲区，可通过 `trace_pipe` 查看。它简单直观，但高频场景开销高，生产工具应优先使用 perf buffer、ring buffer 或 map 聚合。

### 用户态 loader：`hello_exec.c`

```c
// hello_exec.c
// 这个文件在用户空间运行，负责加载、验证触发、挂载和保持程序运行。
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "hello_exec.skel.h"

static volatile sig_atomic_t exiting;

static void handle_signal(int sig)
{
    exiting = 1;
}

int main(void)
{
    struct hello_exec_bpf *skel;
    int err;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    skel = hello_exec_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "failed to open and load BPF skeleton\n");
        return 1;
    }

    err = hello_exec_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "failed to attach BPF programs: %d\n", err);
        hello_exec_bpf__destroy(skel);
        return 1;
    }

    printf("Tracing execve. Run another command, then read trace_pipe. Ctrl-C to exit.\n");
    while (!exiting)
        sleep(1);

    hello_exec_bpf__destroy(skel);
    return 0;
}
```

用户态 loader 做了三件事。

第一，`hello_exec_bpf__open_and_load()` 打开 BPF ELF 对象，把程序和 map 信息交给 libbpf。libbpf 随后通过 `bpf()` 系统调用请求内核加载程序。真正的安全检查发生在内核 verifier 中。如果 verifier 发现越界访问、非法 helper 调用、无法证明的指针范围或不满足程序类型约束，加载会失败。

第二，`hello_exec_bpf__attach()` 根据 skeleton 里的 section 信息把程序挂载到 tracepoint。对这个例子来说，挂载点是 `syscalls:sys_enter_execve`。挂载成功后，只要系统中有进程调用 `execve`，内核就会执行我们的 eBPF 函数。

第三，loader 保持进程运行。很多 eBPF link 的生命周期与用户态进程或 fd 绑定。进程退出、fd 关闭后，程序可能自动 detach。生产程序通常会使用 daemon、systemd 或 pin link 的方式管理生命周期。

### Makefile

```makefile
CLANG ?= clang
CC ?= gcc
BPFTOOL ?= bpftool
CFLAGS := -O2 -g -Wall

all: hello_exec

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

hello_exec.bpf.o: hello_exec.bpf.c vmlinux.h
	$(CLANG) -O2 -g -target bpf -D__TARGET_ARCH_x86 -c $< -o $@

hello_exec.skel.h: hello_exec.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

hello_exec: hello_exec.c hello_exec.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f hello_exec hello_exec.bpf.o hello_exec.skel.h vmlinux.h
```

Makefile 展示了现代 libbpf CO-RE 工作流的雏形。`vmlinux.h` 来自内核 BTF，提供内核类型定义。`hello_exec.bpf.o` 是 BPF 字节码对象。`bpftool gen skeleton` 生成的 skeleton header 把 BPF 对象、map、program、attach 逻辑包装成 C API。第 20 章和第 21 章会系统讲解 skeleton、CO-RE relocation 和分发策略。

### 编译与运行

```bash
make
sudo ./hello_exec
```

另开一个终端触发 `execve`：

```bash
/bin/true
/bin/echo test
```

再打开 trace pipe：

```bash
sudo cat /sys/kernel/tracing/trace_pipe
```

预期输出示例：

```text
bash-12345 [003] ...: bpf_trace_printk: hello eBPF: pid=12345 comm=bash
true-12346 [002] ...: bpf_trace_printk: hello eBPF: pid=12346 comm=true
```

如果没有输出，先检查 tracefs 是否挂载、loader 是否仍在运行、内核是否启用了相关 tracepoint、当前用户是否有足够权限。也可以用下面命令确认 tracepoint 是否存在：

```bash
sudo ls /sys/kernel/tracing/events/syscalls/sys_enter_execve
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_execve/format
```

### 这个示例把哪些概念串起来了

```text
hello_exec.bpf.c
  -> clang -target bpf
  -> hello_exec.bpf.o
  -> bpftool gen skeleton
  -> hello_exec.skel.h
  -> hello_exec.c 调用 libbpf
  -> bpf(BPF_PROG_LOAD)
  -> verifier 检查
  -> attach tracepoint/syscalls/sys_enter_execve
  -> execve 触发
  -> bpf_printk 写入 trace_pipe
```

这条链路就是后续大多数 eBPF 工具的缩影。复杂工具可能会增加 map、ring buffer、多个程序、多个挂载点、配置文件和指标导出，但“BPF 对象 + 用户态 loader + verifier + attach + 数据输出”的骨架不会变。

## 内核实现入口速览

本章不展开源码细节，但先给出阅读地图，便于后续章节定位：

- `include/uapi/linux/bpf.h`：用户态可见的 BPF 命令、map 类型、程序类型、helper 编号和核心结构。
- `kernel/bpf/syscall.c`：`bpf()` 系统调用的主要入口，处理 `BPF_PROG_LOAD`、`BPF_MAP_CREATE`、map lookup/update/delete 等命令。
- `kernel/bpf/verifier.c`：verifier 主体，负责控制流、寄存器状态、指针范围、栈和 helper 约束检查。
- `kernel/bpf/core.c`：BPF core 运行时、解释器等基础逻辑。
- `arch/x86/net/bpf_jit_comp.c`、`arch/arm64/net/bpf_jit_comp.c`：典型架构的 JIT 编译实现。
- `net/core/filter.c`：网络相关 BPF helper、socket filter、skb/XDP 相关处理的重要位置。

当用户态 loader 调用 `bpf(BPF_PROG_LOAD, ...)` 时，内核会复制用户传入的属性，创建 `bpf_prog` 结构，运行 verifier，必要时执行 BTF/CO-RE 相关检查和重定位后的加载流程，再决定解释执行或 JIT 编译。这个流程的细节很多，但核心目标只有一个：让动态加载的程序在内核里运行，同时尽量把风险限制在可证明的边界内。

## 可视化：eBPF 的控制面与数据面

下面的文本图展示了一个典型 eBPF 工具的控制面与数据面分工：

```text
                         用户空间
┌─────────────────────────────────────────────────────────┐
│ loader / CLI / daemon                                   │
│  - 读取配置                                             │
│  - 加载 BPF 对象                                        │
│  - 初始化 map                                           │
│  - 展示统计、事件、火焰图或 Grafana 指标                │
└───────────────┬───────────────────────────────┬─────────┘
                │ bpf() syscall                 │ map/event fd
                v                               ^
                         内核空间               │
┌───────────────────────────────────────────────┴─────────┐
│ kernel/bpf/syscall.c                                     │
│        │                                                  │
│        v                                                  │
│ kernel/bpf/verifier.c  ->  JIT / interpreter              │
│        │                                                  │
│        v                                                  │
│ 挂载点：kprobe / tracepoint / XDP / TC / cgroup / LSM     │
│        │                                                  │
│        v                                                  │
│ eBPF 程序：读取上下文、调用 helper、读写 map、输出事件    │
└───────────────────────────────────────────────────────────┘
```

这个图里最重要的边界是 `bpf()` 系统调用和 verifier。用户态可以提交程序和 map，但不能绕过 verifier 直接把任意机器码塞进内核关键路径。map 和 event fd 则是控制面与数据面之间的通道：控制面通过它们下发配置、读取结果，数据面通过它们保存状态、输出事件。

## 常见问题与排错

**`bpftool` 不存在怎么办？**

先安装发行版对应工具包。也可以从内核源码树的 `tools/bpf/bpftool/` 编译，但初学阶段优先使用发行版包，减少工具链变量。

**`/sys/kernel/btf/vmlinux` 不存在是不是不能学 eBPF？**

不是。没有 BTF 仍可学习基础 BPF、BCC、bpftrace、kprobe、tracepoint 和 map。BTF 对 CO-RE 和现代 libbpf 工作流非常重要，但不是所有入门实验的前置条件。

**编译时报 `asm/types.h` 找不到怎么办？**

这通常是内核头文件或架构 include 路径问题。CO-RE 工作流使用 `vmlinux.h` 可以减少对内核头的依赖，但仍需要 libbpf 头文件。传统方式可能需要加上类似 `-I/usr/include/x86_64-linux-gnu` 的 include 路径。

**运行时报权限错误怎么办？**

eBPF 可以观察或影响内核行为。现代内核把 BPF、perf、网络管理等能力拆分到不同 capability，但很多发行版仍然需要 root 或接近 root 的能力集。生产环境不要简单地把所有容器设为 privileged，第 24 章会讨论最小权限。

**为什么 trace_pipe 没有输出？**

先确认 loader 没有退出，再确认 tracefs 路径。不同系统可能使用 `/sys/kernel/tracing/trace_pipe` 或 `/sys/kernel/debug/tracing/trace_pipe`。还要确认你的触发命令确实调用了对应 tracepoint。最后检查 verifier 日志和 `dmesg`，加载失败时程序根本不会挂载。

**eBPF 程序会不会拖垮系统？**

有可能。verifier 保证的是内存安全和受限执行，不保证你的统计逻辑一定低开销。高频路径上更新全局 hash map、频繁输出 perf event、在 XDP 中做复杂解析，都可能造成明显开销。后续章节会反复强调采样、聚合、per-CPU map、ring buffer、批处理和压测。

## 总结

eBPF 起源于 classic BPF 的包过滤思想，但现代 eBPF 已经发展为 Linux 内核的通用可编程基础设施。它通过受限指令集、map、helper、程序类型、挂载点、verifier 和 JIT，把“动态扩展内核”的能力放在相对可控的安全边界内。可观测性、网络和安全是 eBPF 最成熟的三个应用方向；BCC、bpftrace、libbpf、Cilium 等生态项目则提供了从临时诊断到生产平台的不同入口。

本章的 tracepoint 示例展示了一个最小但完整的 eBPF 工具链：内核态 BPF C 程序、用户态 libbpf loader、BTF 生成的 `vmlinux.h`、skeleton、编译命令、挂载点和 `trace_pipe` 输出。后续章节会不断扩展这个骨架：第 2 章补齐指令集和 map，第 3 章深入 verifier，第 9 章用一系列入门程序建立观测能力，第 20 和第 21 章把 CO-RE/libbpf 工作流推进到可分发工具。

## 本章参考来源

- ebpf.io: What is eBPF, eBPF infrastructure, eBPF use cases。
- Linux kernel documentation: BPF subsystem documentation, BPF instruction set, BPF maps, helpers, BTF。
- Linux kernel source paths: `include/uapi/linux/bpf.h`, `kernel/bpf/syscall.c`, `kernel/bpf/verifier.c`, `kernel/bpf/core.c`, `net/core/filter.c`。
- Brendan Gregg: Linux performance analysis and flame graph practice。
- BCC project documentation and tutorial。
- bpftrace project documentation and one-liners tutorial。
- Cilium documentation: eBPF-based networking, security, observability and Hubble。
- Andrii Nakryiko: BTF, CO-RE and libbpf articles。
- libbpf and libbpf-bootstrap upstream projects。
- ebpf.top: 中文 eBPF 教程和 BPF 入门系列。

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

本节与下一节是纯概念节，不设动手实践清单；自测采用思辨式，答案都能在本节五个概念里找到。

**检查点**(自测后看提示):

1. “eBPF 程序不会自己调度、不会睡眠”——那它什么时候才会执行？本章最靠前与最靠后的两个示例各在等什么事件？——提示：第一个概念；Hello World 等 `tracepoint/syscalls/sys_enter_execve`，Netfilter 示例等报文经过网络钩子。
2. “eBPF 能不能做某件事”为什么不能泛泛回答？开口前要先问哪一句？——提示：第二个概念；先问程序挂在哪里——程序类型决定上下文、可用 helper 与返回动作（XDP 可返回 `XDP_DROP`，tracepoint 返回 0）。
3. `bpf_map_lookup_elem()` 的返回值为什么必须先判空再解引用？这是语法洁癖还是安全前提？——提示：第四个概念；helper 可能返回 NULL，verifier 必须能证明每一次内存访问安全，否则拒绝加载。

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

**检查点**(自测后看提示):

1. 阶段 1“能编译和加载 Hello World”与阶段 2“能在内核事件中读取上下文”的实质差别是什么？对应本章哪两个示例之间的跨越？——提示：01 只输出一行常量文本；从 02 起程序开始从事件现场读 pid/comm，为过滤与结构化输出打底。
2. 阶段 3“过滤”与阶段 4“map 保存状态”为什么排在阶段 5“结构化事件输出”之前？——提示：map 先让 eBPF 从无状态回调变成可配置、可聚合的工具（02/03 用它下发过滤配置）；输出机制解决的是“事件怎么搬到用户态”，是建立在状态之上的下一层能力。
3. 同一个“捕获进程打开文件”业务，为什么要按 6→7 的顺序先写返回值版、再迁 tracepoint 版，最后才进阶段 8 的网络钩子？——提示：10→11 是把同一工具迁移到稳定事件接口的练习；网络钩子的返回值是 drop/accept 决策语义，观测语义练稳之后再换决策语义（第 12 节）。

## 3. eBPF 开发环境搭建

**独立示例**：[`book/examples/chapter-09/00-env/`](../examples/chapter-09/00-env/)（`check-env.sh` 环境自检脚本）。

本章是全书第一个“动手密集”的章节，环境说明按五层组织：**依赖**（要装什么）、**编译**（怎么编出来）、**部署**（编出来之后，运行还差什么）、**运行**（怎么加载和卸载）、**效果**（跑起来能看到什么）。五层不是并列清单，而是依赖链：依赖不齐则编译停摆，权限不足则运行降级，每层把守一个失败面——第 2、5、8 章的五层环境同此结构，本章把它落到 9 个入门示例上。

`check-env.sh` 把层①–③的前提做成一次无 root 自检，按五段输出：内核、BTF/CO-RE、工具链六件、运行时环境、汇总。本节逐段走读它的源码与实跑输出；脚本全文、逐段解读与完整实录见 [`00-env/README.md`](../examples/chapter-09/00-env/README.md)。本机实况全章通用，先行声明（其他机器数值不同）：内核 7.0.0-42-generic、无 root、`kernel.unprivileged_bpf_disabled=2`、tracefs 未挂载。

### 层① 依赖：内核基线与工具链六件

依赖层回答“要装什么”。脚本第一段与第三段负责盘点：

```bash
echo "== 内核 =="
printf 'kernel: %s\n' "$(uname -r)"

kver_major="$(uname -r | cut -d. -f1)"
if [ "${kver_major:-0}" -ge 6 ]; then
    echo 'OK    内核版本 >= 6.x，满足全部示例要求（Netfilter 示例需 6.6+）'
elif [ "${kver_major:-0}" -ge 5 ]; then
    echo 'OK    内核版本 5.x，满足示例 01-08（09-netfilter-go 需 6.6+）'
else
    echo 'WARN  内核版本低于 5.10，多数示例可能不受支持'
fi
```

内核段只取主版本号分三档：6.x 全部示例可用，09 的 Netfilter 程序类型另需 6.6+——`scripts/verify-build.sh` 的机检门禁按主+次版本精确判定，与本行仅按主版本的提示在 6.0–6.5 内核上会分歧，以机检为准；5.x 满足示例 01–08；更低则给 WARN。这一行是 WARN 而不是 FAIL：内核版本无法用包管理器补装，脚本只提示、不拦截。

工具链段用一个通用函数逐项检查六件。`check()` 在脚本开头定义一次，工具链段连续调用六次；下面把定义与这六次调用拼为一处展示（各自与脚本逐字一致）：

```bash
check() {
    # check <名称> <检查命令...>
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        printf 'OK    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        fail=1
    fi
}

echo "== 工具链 =="
check 'clang（编译 BPF C）'        clang --version
check 'gcc（编译用户态 loader）'   gcc --version
check 'bpftool（BTF dump / skeleton）' bpftool version
check 'make'                        make --version
check 'libbpf 头文件（libbpf-dev）' test -f /usr/include/bpf/bpf_helpers.h
check 'Go（仅 09-netfilter-go 需要）' go version
```

`check <名称> <命令...>` 丢弃命令输出、只看退出码；失败打印 FAIL 并把 `fail` 置 1，但不退出、不中断——六件一次查完，便于一眼看清缺什么。本机实跑摘录（BTF 段在层②走读，其余行见 00-env README 完整实录）：

```text
== 内核 ==
kernel: 7.0.0-42-generic
OK    内核版本 >= 6.x，满足全部示例要求（Netfilter 示例需 6.6+）
== 工具链 ==
OK    clang（编译 BPF C）
OK    gcc（编译用户态 loader）
```

六件各自缺了会卡在哪、怎么补：

| 检查项 | 缺失后果 | 补装（Ubuntu/Debian，方法论级） |
|---|---|---|
| clang | BPF 侧编译失败，`*.bpf.o` 出不来 | `sudo apt install clang llvm` |
| gcc | loader 链接失败 | `sudo apt install gcc` |
| bpftool | vmlinux.h 生成与 skeleton 生成两步都停 | `sudo apt install bpftool` |
| make | 构建无法启动 | `sudo apt install make` |
| libbpf-dev | loader 编译报找不到 `<bpf/bpf_helpers.h>` | `sudo apt install libbpf-dev` |
| Go | 仅 09 的 `go build` 失败 | `sudo apt install golang-go` |

这些依赖不是“装上就行”的清单项，其中三件值得多说两句。`clang/LLVM` 负责把受限 C 编译为 BPF 字节码：普通 C 程序编译成 x86_64 或 arm64 机器码，而 eBPF 内核态程序使用 `clang -target bpf` 编译成 BPF ELF 对象——Hello World 的 `hello.bpf.c` 会生成 `hello.bpf.o`，里面包含 BPF 指令、map 定义、license、section 名称和 BTF/重定位信息。这里说“受限 C”，是因为 eBPF C 不是完整用户态 C：不能随意调用 libc，不能 malloc，不能阻塞睡眠，栈空间有限，循环和指针访问都要让 verifier 能证明安全。

`bpftool` 是学习和排错阶段最重要的瑞士军刀：`feature probe` 查内核支持的程序类型、map 类型和 helper；`btf dump` 从内核 BTF 生成 `vmlinux.h`；`gen skeleton` 生成 libbpf 骨架头；`prog list`、`map list` 观察系统里已加载的程序和 map。示例加载失败时，先用 bpftool 确认系统能力，通常比盲目改代码更快。

`libbpf-dev` 提供两侧接口：用户态 loader API（`bpf_object__open()`、`bpf_object__load()`、`bpf_program__attach()`，或 skeleton 生成的 `xxx_bpf__open_and_load()`、`xxx_bpf__attach()`）与 BPF 侧头文件（`<bpf/bpf_helpers.h>` 的 `SEC()`、`__uint()`、`__type()` 宏）。没有它，BPF C 文件写得出来，却很难用现代方式加载和管理。

### 层② 编译：BTF、vmlinux.h 与三步构建链

编译层回答“怎么编出来”。脚本第二段检查编译链的第一块基石：

```bash
echo "== BTF / CO-RE =="
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用）'
else
    echo 'FAIL  /sys/kernel/btf/vmlinux 缺失：CO-RE 示例无法编译，参见附录 C'
    fail=1
fi
```

这一项与工具链缺项同为 FAIL 级，但成因不同：BTF 取决于内核配置 `CONFIG_DEBUG_INFO_BTF`，不是装一个包就能补齐的（自编内核路径见[附录 C](../appendix/c-distro-btf-build-guide.md)）。它之所以排在最前，是因为本章每个示例 `make` 的第一条命令就是 `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`——BTF 缺失，01–09 没有一个能编译。`BTF` 是 CO-RE 的类型信息基础：`/sys/kernel/btf/vmlinux` 存在时，工具知道当前内核里 `task_struct`、tracepoint 上下文、网络结构等类型长什么样，CO-RE 在加载时把“我要读这个字段”重定位到当前内核的真实偏移（机制与规模第 8 章已拆）。没有 BTF，很多老式示例仍可运行，但可移植性会明显下降。

有了 vmlinux.h，示例统一走 libbpf-bootstrap 式三步链（实验目录一节已述）：`clang -g -target bpf` 把 BPF C 编成 `.bpf.o`——`-g` 是命门，BTF 与 CO-RE 重定位记录靠它进入对象；`bpftool gen skeleton` 把对象、map、程序的句柄封成骨架头；`gcc` 链接用户态 loader。整条链无 root 即可完成，逐示例的编译级机检由 `scripts/verify-build.sh` 承担；第一个完整实例见第 4 节与 [`01-hello-world/`](../examples/chapter-09/01-hello-world/)。

### 层③ 部署：运行时前提与权限档

部署层回答“编出来之后，运行还差什么”。脚本第四段给出两个 WARN 级前提：

```bash
echo "== 运行时环境 =="
if [ -d /sys/kernel/tracing/events ]; then
    echo 'OK    tracefs 已挂载于 /sys/kernel/tracing'
else
    echo 'WARN  tracefs 未挂载，可执行: sudo mount -t tracefs tracefs /sys/kernel/tracing'
fi

if [ "$(id -u)" -eq 0 ]; then
    echo 'OK    当前为 root，可直接运行 loader'
else
    echo 'WARN  当前非 root：示例可编译，但运行 loader 需要 sudo 或 CAP_BPF/CAP_PERFMON 等能力'
fi
```

两个 WARN 都不进 `fail` 计数：它们不影响编译，只影响运行。本机实跑恰好两行都中：

```text
== 运行时环境 ==
WARN  tracefs 未挂载，可执行: sudo mount -t tracefs tracefs /sys/kernel/tracing
WARN  当前非 root：示例可编译，但运行 loader 需要 sudo 或 CAP_BPF/CAP_PERFMON 等能力
```

`tracefs/debugfs` 是早期学习和排错的重要输出入口：Hello World 用 `bpf_printk()` 输出的文本不进 stdout，而是进入内核 tracing buffer，经 `/sys/kernel/tracing/trace_pipe` 查看；tracefs 还暴露 tracepoint 字段格式（如 `events/syscalls/sys_enter_openat/format`）。入门阶段如果“程序好像运行了但没有输出”，tracefs 是否挂载、trace_pipe 路径是否正确，是第一批要检查的问题。

权限这一项，本机实况比脚本的 root/非 root 二分更严一层：`kernel.unprivileged_bpf_disabled=2`，非特权加载被内核彻底禁用，不存在“不 root 降级运行”的路径。tracing 类示例（01–08）的权限矩阵：root 可运行；非 root 加 `CAP_BPF`+`CAP_PERFMON` 可运行；非 root 且无能力集只能编译。09-netfilter-go 另有三项：内核 6.6+（`BPF_PROG_TYPE_NETFILTER` 的引入版本）、`CAP_NET_ADMIN`、bpffs 已挂载（通常为 `/sys/fs/bpf`）。安全警告与全书同口径：加载未经验证的 eBPF 程序会影响系统行为，只应在实验环境操作；网络路径示例还必须在隔离拓扑中验证——09 提供 `scripts/netns-lab.sh`（netns+veth 拓扑），一个错误的 drop 规则可能断开当前 SSH 连接。

### 层④ 运行：加载类动作与预期档口径

运行层回答“怎么加载、怎么停”。本章示例的运行形态只有两种。01–08 是单个 loader 前台运行：`sudo ./<loader>` 加载并挂载，Ctrl-C 退出即卸载，对象生命周期跟随进程，不 pin、无守护进程；运行期间可另开终端用 `bpftool prog list`、`bpftool map list` 观察。09 是进入隔离拓扑后运行 Go 控制面：`sudo ip netns exec nfguard bash` 后执行 `nfctl run`（加载、挂到 `NF_INET_LOCAL_OUT`、每 2 秒打印统计）、`nfctl block <port>`（下发 drop 规则）、Ctrl-C 卸载。

口径先说死：本机无 root 且 `unprivileged_bpf_disabled=2`，上述加载类动作一项都无法实跑，全章所有“运行输出”一律标注**预期输出示例 / 运行级待验证**——与章级及各示例 README 的验证状态标注同口径。读者在满足层①–③的特权机上应能复现；正文给命令与判读，不虚构实跑记录。

### 层⑤ 效果：自检汇总与各示例可见产出

效果层回答“跑起来能看到什么”。自检脚本最后一段先给环境自身的判读收口：

```bash
echo "== 汇总 =="
if [ "$fail" -eq 0 ]; then
    echo '环境满足第 9 章示例的编译要求。'
else
    echo '存在 FAIL 项：请先补齐对应依赖（Ubuntu/Debian 可参考: sudo apt install clang llvm gcc bpftool libbpf-dev make）。'
fi
exit "$fail"
```

`fail` 为 0 时输出“环境满足第 9 章示例的编译要求”——准确含义是**编译要求齐备**，不代表运行级就绪（层③两项 WARN 仍在场）；有 FAIL 项则给出与层①表格一致的补装提示。脚本以 `exit "$fail"` 退出：0=无 FAIL，1=存在 FAIL（与第 2–4 章自检脚本同一约定）。本机实跑：

```text
== 汇总 ==
环境满足第 9 章示例的编译要求。
```

自检之后，环境层的编译级效果由 `scripts/verify-build.sh` 兑现：BTF 前置探针 + 00-env 自检 + 逐示例 `make`，本机 2026-11-08 实跑 PASS=10、WARN=0、FAIL=0。每个示例的运行效果——加载后能看到什么——是后续各节的主题，这里一表总览，细节见对应小节与示例 README：

| 示例 | 可见效果（预期输出示例 / 运行级待验证） |
|---|---|
| 01-hello-world | `trace_pipe` 出现 `Hello, eBPF world!`（第 4 节） |
| 02-pid-filter | `trace_pipe` 仅出现目标 PID 的 `execve: pid=… comm=…` 行（第 5 节） |
| 03-container-security | `trace_pipe` 出现目标容器的 `container-guard: cgid=… pid=… open(…)` 行（第 6 节） |
| 04-ring-buffer | loader 按 `PID CGROUP_ID COMM` 三列打印结构化事件（第 7 节） |
| 05-open-file-kprobe | loader 按 `PID COMM FILE` 三列打印打开的文件（第 8 节） |
| 06-open-file-perf-event | 输出同 05，事件通道换成 perf buffer（第 9 节） |
| 07-open-file-return | loader 打印 `PID COMM RET FILE`，RET 为 `fd=N` 或 `ERR=N(errno 名)`（第 10 节） |
| 08-open-file-tracepoint | loader 打印 `PID COMM RET FILE`，RET 为 `fd=N` 或 `ERR=N(errno 名)`，挂载点换成 openat tracepoint（第 11 节） |
| 09-netfilter-go | `nfctl block` 下发黑名单后命中端口流量被 drop，`stats` 打印 pass/drop 计数（第 12 节） |

环境五层就绪后，下一节从第一个示例 Hello World 开始，逐层兑现它们。

**学生动手**（示例资产 [`00-env/`](../examples/chapter-09/00-env/) 与 [`scripts/verify-build.sh`](../examples/chapter-09/scripts/)；两步均编译级、无 root 可复跑）：

```bash
# ①环境自检：内核/工具链/BTF/运行时/汇总五段输出
bash book/examples/chapter-09/00-env/check-env.sh
# ②全章机检：BTF 前置探针 + 00-env 自检 + 01-08 逐示例 make + 09 内核门禁
bash book/examples/chapter-09/scripts/verify-build.sh
```

预期看到：①五段逐项 OK，仅层③两行 WARN（对照层①–⑤的走读逐段核对，FAIL 项按层①表格补装后重跑）；②末尾汇总 `PASS=10 WARN=0 FAIL=0`（本机 2026-11-08 实跑口径）。加载类动作不在机检范围内——脚本只编译不加载，运行级命令一律按层④口径“预期输出示例 / 运行级待验证”，逐示例见第 4–12 节的学生动手。

**检查点**(自测后看提示):

1. 同样是“环境不达标”，BTF 缺失记 FAIL、内核版本低于 6.x 只记 WARN，两者的处理路径差在哪？——提示：层②/层①；BTF 卡死 01–09 编译链的第一条命令（换发行版或按附录 C 自编内核可补），内核版本无法用包管理器补装，脚本只提示、不拦截。
2. 层③的两个 WARN（tracefs 未挂载、非 root）为什么都不进 fail 计数？本机 `kernel.unprivileged_bpf_disabled=2` 又比脚本的 root/非 root 二分严在哪？——提示：两者只影响运行、不影响编译；非特权加载被内核彻底禁用，不存在“不 root 降级运行”的路径，tracing 类示例只能 root 或 `CAP_BPF`+`CAP_PERFMON`。
3. verify-build.sh 的 `PASS=10` 数的是哪十项？为什么它不能替你完成运行验证？——提示：00-env 自检 + 示例 01–08 逐个 make + 09（内核 6.6+ 门禁通过时）；脚本首行的安全声明就是边界——只编译、不加载。

## 4. Hello World

**独立示例**：[`book/examples/chapter-09/01-hello-world/`](../examples/chapter-09/01-hello-world/)。

Hello World 示例的目标是让读者第一次看到 eBPF 输出。相较第 1 章的 tracepoint skeleton 示例，本节应给出更贴近 ebpf.top 入门系列的版本，并解释 `bpf_trace_printk` 与 `bpf_printk` 的关系。

内核态代码（与示例 01 的 `hello.bpf.c` 同形；完整源码含注释与 license 声明，见 [`01-hello-world/`](../examples/chapter-09/01-hello-world/)）：

```c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

SEC("tracepoint/syscalls/sys_enter_execve")
int hello(void *ctx)
{
    bpf_printk("Hello, eBPF world!\n");
    return 0;
}
```

编译与运行命令（与示例 01 的 README 一致）：

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

`trace_pipe` 适合教学，是因为它把内核 tracing buffer 以文本流形式暴露出来。`bpf_printk("Hello, eBPF world!\n")` 的结果不会出现在 loader 的 stdout，而是进入 `/sys/kernel/tracing/trace_pipe`。这能帮助读者理解 eBPF 程序运行在内核事件上下文中，输出路径和用户态 printf 不同。

生产代码不应大量使用 `bpf_printk`，原因是它做字符串格式化并写 tracing buffer，开销高且容易在高频事件下丢数据或干扰系统。真实工具通常只在开发和排错阶段保留少量 `bpf_printk`。当需要持续输出事件时，应使用 ring buffer 或 perf buffer；当只需要统计时，应使用 map 聚合，让用户态周期性读取。

**学生动手**（示例 [`01-hello-world/`](../examples/chapter-09/01-hello-world/)；第一步编译级、无 root，其余运行级）：

```bash
cd book/examples/chapter-09/01-hello-world
make                                        # 编译级：vmlinux.h -> hello.bpf.o -> skel -> hello
sudo ./hello                                # 运行级：加载并前台运行，Ctrl-C 退出
sudo cat /sys/kernel/tracing/trace_pipe     # 另一终端；触发：执行 /bin/true、ls 等任意命令
```

预期看到：loader 打印加载提示；另一终端每执行一条命令，`trace_pipe` 就出现一行 `bpf_trace_printk: Hello, eBPF world!`（预期输出示例 / 运行级待验证）。没有输出时按 README 失败表排查三件事：tracefs 是否挂载、是否触发了 `execve`、加载权限是否足够。

**检查点**(自测后看提示):

1. `bpf_printk("Hello, eBPF world!\n")` 的输出为什么不出现在 `./hello` 的 stdout？它走了哪条路径？——提示：helper 写的是内核 tracing buffer；经 `/sys/kernel/tracing/trace_pipe` 查看，这正是 eBPF 运行在内核事件上下文、与用户态 printf 输出路径不同的体现。
2. 第一个示例为什么选 `execve` tracepoint，而不是调度器之类的高频事件？——提示：容易触发、语义清楚、输出不会刷屏；执行任意命令即可把“我执行了命令”和“trace_pipe 出现一行输出”直接对应起来。
3. `bpf_printk` 为什么不适合生产？要持续输出事件、只要统计计数分别该换什么？——提示：字符串格式化并写 tracing buffer，开销高且高频下易丢数据；持续事件用 ring buffer/perf buffer，纯统计用 map 聚合（见“横向对比”表）。

## 5. 通过 PID 过滤

**独立示例**：[`book/examples/chapter-09/02-pid-filter/`](../examples/chapter-09/02-pid-filter/)。

没有过滤的 eBPF 程序会捕获系统中所有匹配事件。入门阶段很快会遇到噪声问题：你只想观察目标进程，却看到 shell、systemd、容器 runtime、监控 agent 的大量事件。PID 过滤是最小过滤能力。

map 设计（节选自示例 02 的 `pid_filter.bpf.c`）：

```c
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} target_pid SEC(".maps");
```

过滤逻辑（节选自 `pid_filter.bpf.c`，随后的 comm 读取与输出见示例源码）：

```c
__u32 key = 0;
__u32 current = bpf_get_current_pid_tgid() >> 32;
__u32 *target = bpf_map_lookup_elem(&target_pid, &key);

if (target && *target != 0 && *target != current)
    return 0;
```

用户态运行方式：

```bash
sudo ./pid_filter -p 12345
```

这里的关键是理解 PID 过滤为什么要通过 helper 和 map 组合完成。

`bpf_get_current_pid_tgid()` 返回一个 64 位值，高 32 位通常是线程组 ID，也就是用户经常理解的进程 PID；低 32 位是当前线程 ID。很多入门工具写 `bpf_get_current_pid_tgid() >> 32`，是为了按进程过滤，而不是按单个线程过滤。如果读者要跟踪多线程程序中的某个具体线程，才需要关注低 32 位。

用 array map 存目标 PID，比把 PID 写死在 BPF 程序里更灵活。假设把 `12345` 直接写进 `pid_filter.bpf.c`，每换一个进程都要重新编译、重新加载、重新挂载程序。使用 `BPF_MAP_TYPE_ARRAY` 后，内核态程序只读 key 0 对应的 value；用户态 loader 可以在启动时写入 `-p 12345`，也可以运行中更新为另一个 PID。

用户态更新 map 后，内核态程序下一次触发即可看到新值，因为 map 是内核对象，不是编译期常量。示例里的逻辑 `if (target && *target != 0 && *target != current) return 0;` 表示：如果配置了非 0 目标 PID，并且当前进程不匹配，就直接返回，不输出事件。这样过滤发生在内核事件现场，噪声不会再被传到用户态。

**学生动手**（示例 [`02-pid-filter/`](../examples/chapter-09/02-pid-filter/)；loader 只认 `-p`，不带参数则输出全部进程）：

```bash
cd book/examples/chapter-09/02-pid-filter
make                                        # 编译级
sudo ./pid_filter -p $(pgrep -x bash)       # 运行级：只观察 bash 的 execve
sudo cat /sys/kernel/tracing/trace_pipe     # 另一终端；触发：执行 /bin/true
```

预期看到：`trace_pipe` 只出现目标 PID 的 `execve: pid=… comm=bash` 行，其他进程（systemd、监控 agent 等）的 execve 不再出现（预期输出示例 / 运行级待验证）——过滤发生在内核态，这正是与“全部传到用户态再过滤”的本质区别。过滤不生效时先查 `-p` 的目标是否存在、目标命令是否为 shell 内建。

**检查点**(自测后看提示):

1. `bpf_get_current_pid_tgid() >> 32` 丢掉的是什么？什么时候才需要低 32 位？——提示：高 32 位是线程组 ID（用户通常理解的进程 PID），低 32 位是线程 ID；跟踪多线程程序中的某个具体线程时才用低 32 位。
2. 目标 PID 放在 `max_entries=1` 的 ARRAY map 里，而不是写死在 BPF 源码里，换来什么？——提示：换目标进程不用重新编译、加载、挂载；map 是内核对象而非编译期常量，用户态更新后内核态下一次触发即可见。
3. 过滤逻辑 `if (target && *target != 0 && *target != current)` 里，第一个判断去掉会发生什么？——提示：`bpf_map_lookup_elem()` 可能返回 NULL，不判空就解引用会被 verifier 直接拒绝加载。

## 6. 借助 eBPF 保护容器安全

**独立示例**：[`book/examples/chapter-09/03-container-security/`](../examples/chapter-09/03-container-security/)。

这一篇把过滤维度从 PID 扩展到容器。容器环境中，单纯 PID 不稳定：容器会重启，进程会变化，同一个宿主机上有多个 namespace。更实际的做法是结合 cgroup id、mount namespace、pid namespace 或容器 runtime 暴露的元数据。

示例目标：识别目标容器并记录其进程访问敏感路径的行为。

事件流：

```text
进程打开文件 -> eBPF 读取 comm/pid/cgroup_id/path 信息 -> 判断是否属于目标容器 -> 输出事件或拒绝
```

核心 helper：

```c
__u64 cgid = bpf_get_current_cgroup_id();
```

map 形态（示意；实际定义见示例 03 `container_guard.bpf.c` 的 `target_cgroups` HASH map）：

```c
// key: cgroup id（__u64）
// value: __u32 策略（POLICY_LOG=0 / POLICY_DENY=1；示例只启用 POLICY_LOG）
BPF_MAP_TYPE_HASH target_cgroups;
```

容器安全示例要把“识别容器”和“执行安全策略”分开讲。

cgroup id 适合作为容器级过滤线索，是因为现代容器运行时通常把容器进程放进独立 cgroup。PID 会随着容器重启变化，进程名也可能伪装，但 cgroup 层级更接近资源隔离和容器生命周期。eBPF 程序可以用 `bpf_get_current_cgroup_id()` 获取当前任务所属 cgroup id，再到 `target_cgroups` map 中查询是否需要记录或应用策略。

安全策略必须区分“观测”和“阻断”。如果程序挂在 tracepoint 或普通 kprobe 上，它通常只能记录事件，不能可靠地改变系统调用结果。比如你可以在 openat tracepoint 上看到容器进程正在打开 `/etc/shadow`，但不能因此声称这个 tracepoint 程序已经阻止了打开。要做阻断，应选择有决策语义的挂载点，例如 LSM BPF 钩子、cgroup hooks，或网络路径上的 XDP/TC/Netfilter 返回动作。

因此，本章示例要明确边界：容器安全的入门版先做“识别目标容器并输出敏感行为事件”。如果要扩展为阻断版，需要切换到合适的程序类型，并说明返回值如何影响内核决策。这样读者不会把观测型探针误用成访问控制系统。

**学生动手**（示例 [`03-container-security/`](../examples/chapter-09/03-container-security/)；README 的“简化实验”不依赖真实容器，把当前 shell 所在 cgroup 当目标）：

```bash
cd book/examples/chapter-09/03-container-security
make                                        # 编译级
CGID=$(stat -c %i "/sys/fs/cgroup$(grep -o '/.*' /proc/self/cgroup | head -1)")
sudo ./container_guard -c "$CGID"           # 运行级：注册目标 cgroup
sudo cat /sys/kernel/tracing/trace_pipe     # 另一终端；触发：cat /etc/hosts、ls
```

预期看到：`trace_pipe` 出现目标 cgroup 内进程的 `container-guard: cgid=… pid=… open(…)` 行，宿主机其他 cgroup 的进程不出现（预期输出示例 / 运行级待验证）。真实容器实验先取容器 cgroup 的 inode 号（如 `stat -c %i /sys/fs/cgroup/system.slice/docker-<容器长ID>.scope`），并确认 cgroup v2。

**检查点**(自测后看提示):

1. 为什么 cgroup id 比 PID 更适合标识容器？`bpf_get_current_cgroup_id()` 的返回值在用户态怎么对上？——提示：PID 随容器重启变化，cgroup 层级更贴近资源隔离与容器生命周期；返回值是 cgroup 的 inode 号（v2），与 `stat -c %i <路径>` 可直接对应。
2. 看到容器进程打开 `/etc/shadow` 的事件，能说“这个 tracepoint 程序阻止了这次打开”吗？要做阻断该换什么？——提示：不能；tracepoint/kprobe 只有观测语义，阻断要换有决策语义的挂载点——LSM BPF、cgroup hooks 或网络路径的 XDP/TC/Netfilter。
3. `target_cgroups` 为什么用 HASH map 而不是第 5 节的 ARRAY？——提示：ARRAY 的 key 恒为 0，只当“全局配置变量”用；这里按 64 位 cgroup id 查询、目标容器可多个，key 是稀疏的大整数空间，HASH 才合适。

## 7. 在 eBPF 中使用 ring buffer

**独立示例**：[`book/examples/chapter-09/04-ring-buffer/`](../examples/chapter-09/04-ring-buffer/)。

ring buffer 是现代 eBPF 事件输出的重要机制。相比 `bpf_printk`，它传输结构化数据；相比 perf buffer，它提供 MPSC ring buffer 语义，用户态读取更直接。

事件结构（节选自示例 04 的 `ringbuf_exec_common.h`）：

```c
struct event {
    __u32 pid;
    __u64 cgroup_id;
    char comm[16];
};
```

事件 map（节选自示例 04 的 `ringbuf_exec.bpf.c`）：

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");
```

输出逻辑（节选自 `ringbuf_exec.bpf.c`）：

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

**学生动手**（示例 [`04-ring-buffer/`](../examples/chapter-09/04-ring-buffer/)）：

```bash
cd book/examples/chapter-09/04-ring-buffer
make                                        # 编译级
sudo ./ringbuf_exec                         # 运行级：结构化事件直接打到 loader 的 stdout
/bin/true; ls; echo test                    # 另一终端触发 execve
```

预期看到：loader 的 stdout 按 `PID  CGROUP_ID  COMM` 三列打印事件（预期输出示例 / 运行级待验证）——不再经过 `trace_pipe`，与第 4 节对比就是输出通道从文本日志换成了 ring buffer。ring buffer 需要内核 5.8+。

**检查点**(自测后看提示):

1. reserve 之后、submit 之前，这条事件处于什么状态？既不 submit 也不 discard 会怎样？——提示：`bpf_ringbuf_reserve()` 预留的空间对用户态不可见；不提交也不丢弃会导致 verifier 拒绝或运行路径不完整。
2. `if (!e) return 0;` 防的是什么？生产程序通常再加什么让用户态感知丢事件？——提示：ring buffer 满时 reserve 返回 NULL，这是高频事件下的背压路径；生产工具增加 per-CPU drop 计数，让用户态知道丢了多少。
3. `ring_buffer__poll(ring, 100)` 返回 0 与返回负值分别意味着什么？为什么这个语义适合写 daemon？——提示：100ms 超时、无事件返回 0 是正常情况，负值才按错误处理；主循环可以边 poll 边响应退出信号。

## 8. 捕获进程打开文件信息

**独立示例**：[`book/examples/chapter-09/05-open-file-kprobe/`](../examples/chapter-09/05-open-file-kprobe/)。

这一篇进入真实 opensnoop 类工具。目标是捕获进程打开文件时的文件名、进程名、PID 等信息。

可选挂载点：

- kprobe：`do_sys_openat2`、`do_filp_open` 或架构相关 syscall wrapper。
- tracepoint：`syscalls:sys_enter_openat`、`syscalls:sys_enter_openat2`。

示例 05 选第一类中的 `do_sys_openat2`：它是 open/openat/openat2 三个系统调用在内核里的公共实现（fs/open.c，5.6+ 引入），原型为 `long do_sys_openat2(int dfd, const char __user *filename, struct open_how *how)`——第二个参数正是本节要抓的用户态 filename 指针。事件结构与第 7 节同为三字段固定大小形态，字段换成 pid/comm/filename（节选自 `opensnoop_kprobe_common.h`）：

```c
#define FILENAME_LEN 128

struct event {
	__u32 pid;
	char comm[16];
	char filename[FILENAME_LEN];
};
```

事件通道是 256 KB 的 ring buffer（节选自 `opensnoop_kprobe.bpf.c`）：

```c
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");
```

kprobe 版本读取 filename 的动作全部集中在入口函数里（节选自 `opensnoop_kprobe.bpf.c`）：

```c
SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(opensnoop_entry, int dfd, const char *filename,
	       struct open_how *how)
{
	struct event *e;

	e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
	if (!e)
		return 0;

	e->pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(&e->comm, sizeof(e->comm));

	/* filename 是用户态地址：读取用户内存，可能失败/截断 */
	bpf_probe_read_user_str(&e->filename, sizeof(e->filename),
				(const void *)filename);

	bpf_ringbuf_submit(e, 0);
	return 0;
}
```

对照这段代码逐段看：

- **取参**：`BPF_KPROBE` 宏（`<bpf/bpf_tracing.h>`）按位置把 `do_sys_openat2` 的形参绑定到 `dfd`、`filename`、`how`。不用宏时的等价写法是 `int opensnoop_entry(struct pt_regs *ctx)` 内取 `PT_REGS_PARM2(ctx)`——这正是 kprobe 与 tracepoint 的一个差别：kprobe 上下文是 `pt_regs`，函数参数要按调用约定自己还原，宏只是把这个机械步骤封装掉。
- **预留**：`bpf_ringbuf_reserve()` 与判空 `return 0` 同第 7 节：缓冲满时返回 NULL，先丢掉这条事件。
- **基础字段**：`e->pid` 取 `bpf_get_current_pid_tgid() >> 32`（第 5 节讲过的进程 PID 语义），`bpf_get_current_comm()` 填任务名。
- **读 filename（本节主角）**：`filename` 形参是用户态地址，必须用 `bpf_probe_read_user_str()` 从触发事件的那个进程的地址空间复制进事件内的 `filename[128]` 缓冲；读取可能失败（指针无效、缺页）或被 128 字节截断，输出里偶见的空文件名多来自这里，生产工具应检查 helper 的返回值。
- **提交**：`bpf_ringbuf_submit(e, 0)` 之后，事件才对用户态可见。

用户态 loader（节选自 `opensnoop_kprobe.c`）先 `opensnoop_kprobe_bpf__open_and_load()` 一步完成加载与校验，再挂载并创建 ring buffer reader：

```c
	if (opensnoop_kprobe_bpf__attach(skel)) {
		fprintf(stderr, "挂载 BPF 程序失败（do_sys_openat2 是否存在于当前内核？）\n");
		opensnoop_kprobe_bpf__destroy(skel);
		return 1;
	}

	ring = ring_buffer__new(bpf_map__fd(skel->maps.events),
				handle_event, NULL, NULL);
```

attach 的失败信息指向 kprobe 的固有依赖：程序绑定的是内核函数名，`do_sys_openat2` 必须出现在当前内核的符号表里（老内核可换 `do_sys_open`，或改用第 11 节的 tracepoint 版本）。reader 的回调把字节流按 `struct event` 解释并打印三列（节选自 `opensnoop_kprobe.c`）：

```c
static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct event *e = data;

	printf("%-8u %-16s %s\n", e->pid, e->comm, e->filename);
	return 0;
}
```

`main()` 的主循环就是第 7 节的 `ring_buffer__poll(ring, 100)`：100 是超时毫秒数，返回 `-EINTR` 时置位退出标志收尾。预期输出（列宽按上面的 printf 格式串逐列排布；预期输出示例 / 运行级待验证）：

```text
PID      COMM             FILE
1001     cat              /etc/hosts
1002     ls               /tmp
1003     nginx            /var/log/nginx/access.log
```

文件打开跟踪示例最容易踩指针和时序两个坑。

用户指针与内核指针不能混用。系统调用参数里的 filename 通常是用户态地址，指向调用进程地址空间中的字符串；内核结构里的字段则是内核地址。eBPF verifier 会跟踪指针类型，不允许你把任意整数当指针解引用。读取用户态 filename 时应使用 `bpf_probe_read_user_str()`，读取内核结构字符串时才考虑 `bpf_probe_read_kernel_str()`。

`bpf_probe_read_user_str()` 和 `bpf_probe_read_kernel_str()` 的区别在于来源地址空间不同。前者处理用户地址，可能遇到缺页、无效地址或字符串过长；后者处理内核地址，也同样受 verifier 和 helper 约束。示例要把这两者分开讲，否则读者在 kprobe、tracepoint、LSM 示例之间迁移代码时很容易出现 verifier 错误或运行时读取失败。

文件名通常在 enter 阶段最容易拿到，因为系统调用刚进入内核时参数仍然可见。但 open 是否成功、返回的 fd 是多少、错误码是什么，要到 exit 阶段才知道。这也是为什么后续返回值示例需要在 enter 阶段把 filename 暂存到 map，exit 阶段再按 pid 查回它并补上返回值。

**学生动手**（示例 [`05-open-file-kprobe/`](../examples/chapter-09/05-open-file-kprobe/)；先确认挂载点符号在当前内核存在，再编译运行）：

```bash
sudo cat /proc/kallsyms | grep do_sys_openat2   # 符号确认（5.6+ 内核应在场）
cd book/examples/chapter-09/05-open-file-kprobe
make                                            # 编译级
sudo ./opensnoop_kprobe                         # 运行级；另一终端触发：cat /etc/hosts、ls /tmp
```

预期看到：loader 按 `PID  COMM  FILE` 三列打印打开的文件（预期输出示例 / 运行级待验证）；`do_sys_openat2` 不在当前内核符号表里时，attach 失败并打印中文报错——这正是 kprobe 绑定内核函数名的固有依赖，老内核可换 `do_sys_open` 或改用第 11 节的 tracepoint 版。

**检查点**(自测后看提示):

1. `BPF_KPROBE` 宏替你做了什么？不用宏时的等价写法是什么？——提示：按调用约定从 `pt_regs` 还原 `do_sys_openat2` 的形参；等价写法是 `int opensnoop_entry(struct pt_regs *ctx)` 内取 `PT_REGS_PARM2(ctx)`。
2. 读 filename 用 `bpf_probe_read_user_str()`，什么时候才用 `bpf_probe_read_kernel_str()`？混用会怎样？——提示：filename 是触发进程的用户态地址；读内核结构里的字符串才用 kernel 版；两者混用是 verifier 报错的高频原因。
3. 为什么要在 enter 阶段读文件名？open 是否成功、fd 是多少要到哪一阶段才知道？——提示：系统调用刚进入内核时参数指针仍然可见；结果要到 exit 阶段，这正是第 10 节用临时 map 关联 enter/exit 的动机。

## 9. 捕获进程打开文件信息 perf_event 篇

**独立示例**：[`book/examples/chapter-09/06-open-file-perf-event/`](../examples/chapter-09/06-open-file-perf-event/)。

这一篇与 ring buffer 形成对照：同样输出结构化事件，但使用 perf event array。

事件 map（节选自示例 06 的 `opensnoop_perf.bpf.c`）：

```c
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} events SEC(".maps");
```

输出调用（与示例 06 的 `opensnoop_perf.bpf.c` 同形）：

```c
struct event event = {};
bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event));
```

perf_event 篇要解释为什么同样是输出事件，还要学习 perf buffer。

perf buffer 基于 `BPF_MAP_TYPE_PERF_EVENT_ARRAY`，通常按 CPU 建立事件通道。eBPF 程序调用 `bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event))` 时，会把事件写到当前 CPU 对应的 perf buffer。用户态 reader 需要为这些 per-CPU buffer 建立回调，并持续 poll。这样做可以减少跨 CPU 竞争，但也意味着用户态要处理每个 CPU 的缓冲和丢事件统计。

libbpf 用户态一般使用 `perf_buffer__new()` 创建 reader，传入 map fd、每 CPU 页数、事件回调和丢失回调，再用 `perf_buffer__poll()` 进入循环。事件回调负责把原始字节解释成 `struct event`；丢失回调负责记录 lost samples。完整示例要让读者看到：内核态输出结构体，用户态按同一个结构体布局解析。

与 ring buffer 相比，perf buffer API 更老，使用面更广，很多 BCC 时代工具和旧内核环境仍然依赖它。ring buffer 在现代 libbpf 项目中更简洁，但不能因此跳过 perf buffer。给同一 opensnoop 事件写两个版本，可以让读者直观看到两者差异：BPF 侧一个调用 `bpf_perf_event_output()`，另一个调用 `bpf_ringbuf_reserve/submit()`；用户态一个用 `perf_buffer__poll()`，另一个用 `ring_buffer__poll()`。

**学生动手**（示例 [`06-open-file-perf-event/`](../examples/chapter-09/06-open-file-perf-event/)）：

```bash
cd book/examples/chapter-09/06-open-file-perf-event
make                                        # 编译级
sudo ./opensnoop_perf                       # 运行级；另一终端触发：cat /etc/hosts
```

预期看到：输出三列与示例 05 同形（预期输出示例 / 运行级待验证）；高负载时可能出现“CPU N 丢失 X 个事件”的丢失警告——这正是 perf buffer 与 ring buffer 在背压行为上的可感差异。

**检查点**(自测后看提示):

1. `BPF_F_CURRENT_CPU` 让事件写到哪？用户态 reader 为什么因此要管理 per-CPU 缓冲？——提示：写到当前 CPU 对应的 perf buffer，避免跨 CPU 竞争；`perf_buffer__new()` 按 map fd 与每 CPU 页数建通道，用户态要逐 CPU poll 并各自处理丢失统计。
2. perf buffer 的丢失回调为什么是必选项而非装饰？ring buffer 满时的对应机制是什么？——提示：缓冲满时内核直接丢样并计数，不接 lost 回调，数据“悄悄变少”无从察觉；ring buffer 走的是 reserve 返回 NULL 的背压路径（第 7 节）。
3. 给同一 opensnoop 事件写 05/06 两个版本，BPF 侧与用户态侧各换了哪一对调用？——提示：BPF 侧 `bpf_ringbuf_reserve/submit()` ↔ `bpf_perf_event_output()`；用户态 `ring_buffer__poll()` ↔ `perf_buffer__poll()`，事件结构与其余逻辑保持一致。

## 10. 捕获进程打开文件信息返回值

**独立示例**：[`book/examples/chapter-09/07-open-file-return/`](../examples/chapter-09/07-open-file-return/)。

只捕获 open enter 事件还不够。很多排障问题关心的是“打开失败了吗，错误码是什么”。返回值需要在函数返回或 syscall exit 时捕获。

enter/exit 两阶段路径：

```text
enter 阶段：记录 pid -> filename
exit 阶段：读取返回值 ret，关联 pid 查 filename，输出并删除临时状态
```

临时 map 形态（示意；实际定义见示例 07 的 `openret.bpf.c`）：

```c
// key: pid_tgid（64 位，避免多线程同进程冲突）
// value: comm + filename（struct inflight_value）
BPF_MAP_TYPE_HASH inflight;
```

输出示例（列宽按 `openret.c` 中 `handle_event` 的 printf 格式串逐列排布；预期输出示例 / 运行级待验证）：

```text
PID      COMM             RET        FILE
1001     cat              fd=3    /etc/hosts
1002     cat              ERR=-2  (ENOENT) /not-exist
```

返回值示例要把“两个事件拼成一次完整操作”讲清楚。

enter 和 exit 需要通过 map 关联，是因为 openat 进入时能看到 filename，但还不知道结果；退出时能看到返回值，但 filename 参数未必还方便读取。常见做法是在 enter 阶段用 64 位 `pid_tgid` 作为 key，把 comm、filename 写入 `inflight` map；exit 阶段用同一个 key 查回临时状态，加上返回值后输出完整事件。

返回值为负数时通常表示 `-errno`。例如 `-2` 对应 `ENOENT`，表示文件不存在；非负值通常是文件描述符 fd。示例输出不能只打印数字，还应解释如何把负返回值映射到错误原因。完整工具可以在用户态把 `-ret` 转换为 errno 名称和错误字符串。

临时 map 必须在 exit 阶段删除。否则一个进程多次打开文件后，旧 filename 会残留，后续 exit 可能关联到错误数据；长时间运行的工具还会让 map 持续增长。正确路径是：enter 更新 map，exit 查找 map，输出事件后 `bpf_map_delete_elem()` 清理。即使 exit 阶段发现读取返回值异常，也应考虑清理临时项。

key 设计要谨慎。只用 tgid 可能在多线程程序中冲突，因为同一进程多个线程可能同时执行 open；通常更安全的是使用 `pid_tgid` 的完整 64 位值，或者至少用低 32 位 tid 区分线程。遇到异步、嵌套或递归场景时，还可能需要额外时间戳或栈层次信息。

**学生动手**（示例 [`07-open-file-return/`](../examples/chapter-09/07-open-file-return/)；触发要覆盖成功与失败两种返回值）：

```bash
cd book/examples/chapter-09/07-open-file-return
make                                        # 编译级
sudo ./openret                              # 运行级
# 另一终端分别触发成功与失败：
cat /etc/hosts                              # RET 为 fd=N
cat /not-exist-file                         # RET 为 ERR=-2（ENOENT）
```

预期看到：`PID  COMM  RET  FILE` 四列输出，成功行 `fd=3`、失败行 `ERR=-2(ENOENT)`（预期输出示例 / 运行级待验证）。

**检查点**(自测后看提示):

1. enter/exit 两阶段为什么必须借临时 map 关联？key 为什么用完整 64 位 pid_tgid？——提示：enter 时看不到返回值、exit 时拿不到入参（kretprobe 上下文里没有函数参数）；只取 tgid 会在多线程程序中冲突——同一进程多个线程可能同时执行 open。
2. `-2`、`-13` 这样的负返回值怎么变成人话？非负值又是什么？——提示：内核返回 `-errno`——`-2` 是 ENOENT（文件不存在）、`-13` 是 EACCES（权限不足）；非负值是文件描述符 fd；完整工具在用户态把 `-ret` 映射为 errno 名与错误字符串。
3. exit 阶段输出事件后为什么必须 `bpf_map_delete_elem()`？漏删有两个什么后果？——提示：旧 filename 残留会被后续 exit 错误关联（事件串位）；map 持续增长相当于内存泄漏；即使异常分支也应先删临时项再返回。

## 11. 捕获进程打开文件信息 tracepoint 篇

**独立示例**：[`book/examples/chapter-09/08-open-file-tracepoint/`](../examples/chapter-09/08-open-file-tracepoint/)。

tracepoint 版本强调稳定性。与 kprobe 绑定内核函数名不同，tracepoint 是内核显式提供的事件点，并通过 tracefs 暴露字段格式。

发现 tracepoint：

```bash
sudo ls /sys/kernel/tracing/events/syscalls/sys_enter_openat
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/format
sudo cat /sys/kernel/tracing/events/syscalls/sys_exit_openat/format
```

section 形态（完整实现在示例 08 的 `opensnoop_tp.bpf.c`）：

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int tp_openat_enter(struct trace_event_raw_sys_enter *ctx) { ... }

SEC("tracepoint/syscalls/sys_exit_openat")
int tp_openat_exit(struct trace_event_raw_sys_exit *ctx) { ... }
```

tracepoint 篇要强调“稳定，但仍要核对”。

tracepoint 字段来自 tracefs 的 `format` 文件。比如 `syscalls/sys_enter_openat/format` 会列出事件字段、偏移、大小和字段名。使用 tracepoint 时，读者不需要猜某个内核内部函数在当前版本叫什么、参数顺序是什么，而是按照内核暴露的事件格式读取上下文。这就是 tracepoint 比 kprobe 更适合长期维护工具的原因。

syscall tracepoint 的参数通常放在 `ctx->args[]` 中。以 openat 为例，`args[0]` 可能是 `dfd`，`args[1]` 是用户态 filename 指针，`args[2]` 是 flags，具体要以 `format` 为准。示例应展示如何先查看 `format`，再在 BPF 代码中把 `ctx->args[1]` 转成用户指针并用 `bpf_probe_read_user_str()` 读取。

tracepoint 的稳定性不等于永远不变。不同架构、不同 syscall ABI、不同内核配置可能导致字段或事件可用性差异。完整章节应要求读者先运行 `ls /sys/kernel/tracing/events/syscalls/` 和 `cat .../format`，再编译对应示例。这样做比直接复制别人机器上的上下文结构更可靠。

**学生动手**（示例 [`08-open-file-tracepoint/`](../examples/chapter-09/08-open-file-tracepoint/)；“先查 format、再写代码”是本节的方法论）：

```bash
sudo ls /sys/kernel/tracing/events/syscalls/            # 发现事件（运行级：tracefs 需挂载）
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/format
sudo cat /sys/kernel/tracing/events/syscalls/sys_exit_openat/format
cd book/examples/chapter-09/08-open-file-tracepoint
make                                                    # 编译级
sudo ./opensnoop_tp                                     # 运行级；触发同第 10 节
```

预期看到：format 文件列出事件字段、偏移、大小与字段名（`args[]` 的布局以它为准）；loader 输出与示例 07 逐行一致（预期输出示例 / 运行级待验证）——两个版本只是“取数据的入口”不同。

**检查点**(自测后看提示):

1. tracepoint 与 kprobe 的稳定性差别，在“字段从哪来”上如何体现？——提示：kprobe 绑定内核函数名与参数布局，随版本漂移；tracepoint 是内核显式提供的事件点，字段格式经 tracefs 的 `format` 文件暴露，按格式读上下文即可。
2. openat 的用户态 filename 指针在 `ctx->args[]` 的第几个？最终以什么为准？——提示：`args[1]`（`args[0]` 是 dfd、`args[2]` 是 flags）；具体以 `/sys/kernel/tracing/events/syscalls/sys_enter_openat/format` 为准，先查再写代码。
3. “tracepoint 稳定”为什么仍要求读者先 `ls`/`cat format` 再编译？——提示：不同架构、不同 syscall ABI、不同内核配置可能造成字段或事件可用性差异；先核对再用，比直接复制别人机器上的上下文结构更可靠。

## 12. 在 Netfilter 中使用 eBPF 以及 Go 经验总结

**独立示例**：[`book/examples/chapter-09/09-netfilter-go/`](../examples/chapter-09/09-netfilter-go/)（含 `scripts/netns-lab.sh` 隔离拓扑脚本）。

系列最后一篇把学习从观测推进到网络钩子和 Go 控制面。Netfilter BPF 与 XDP/TC 的侧重点不同：它接近传统防火墙路径，适合解释报文过滤、策略判断和用户态控制面的关系。

示例目标：

```text
Go 控制面加载 BPF 程序 -> 挂到 Netfilter hook -> map 下发阻断规则 -> 输出命中统计
```

Go 控制面形态（示意；完整实现在 [`09-netfilter-go`](../examples/chapter-09/09-netfilter-go/) 的 `cmd/nfctl/main.go`）：

```go
package main

// 实现选型：github.com/cilium/ebpf（v0.16，与示例 go.mod 一致）。
// 目标：加载对象、打开 map、写入规则、attach netfilter hook、打印统计。
func main() {}
```

Netfilter + Go 示例要让读者第一次看到“控制面/数据面分离”的工程形态。

Go 用户态负责控制面，C eBPF 程序负责数据面。数据面在内核网络路径上高频执行，只做解析报文、查 map、更新统计、返回动作；Go 控制面负责加载对象、挂载程序、解析命令行、把规则写入 map、读取统计、优雅退出。这样分工后，规则变更不需要重新编译 BPF 程序，控制面也不会进入高频报文处理路径。

map pinning 用来把 map 固定到 bpffs，例如 `/sys/fs/bpf/nf_guard_rules`（rules 与 stats 两个 map 都经 `LIBBPF_PIN_BY_NAME` 固定在这里）。如果不 pin，map 生命周期通常跟 loader 进程和 fd 绑定，进程退出后 map 可能被释放。pin 后，另一个控制面进程可以重新打开同一个 map，读取或更新规则。生产工具常用 pinning 支持 daemon 重启、状态保留和多进程协作，但也要做好清理，避免留下过期 map。

Netfilter、XDP、TC 的钩子位置不同。XDP 在驱动收包早期，适合极高吞吐的早期丢包；TC 位于 qdisc ingress/egress，和 skb、容器 veth、改包动作结合更自然；Netfilter 位于传统防火墙路径，适合理解 hook、协议栈阶段和策略过滤。入门示例选择 Netfilter，是为了把 eBPF 与经典防火墙概念连接起来；第 15 至第 17 章会系统比较 XDP 和 TC。

网络路径示例必须在隔离拓扑中验证。一个错误的 drop 规则可能让当前 SSH 连接断开，一个错误的 NAT 校验和可能污染宿主机网络排障。完整章节应使用 network namespace、veth pair 和专用测试地址构造实验环境，所有 attach/detach 脚本都要有清理路径。只有在隔离拓扑通过后，才考虑真实网卡实验。

**学生动手**（示例 [`09-netfilter-go/`](../examples/chapter-09/09-netfilter-go/)；网络路径，全程在隔离拓扑内，需要内核 6.6+ 与 root）：

```bash
cd book/examples/chapter-09/09-netfilter-go
make                                        # 编译级：go build + vmlinux.h -> bpf/nf_guard.bpf.o -> nfctl
# ——以下运行级（预期输出示例 / 运行级待验证）——
sudo bash scripts/netns-lab.sh up           # 创建 netns+veth 隔离拓扑
sudo ip netns exec nfguard bash             # 进入命名空间后执行：
./nfctl run                                 # 加载并挂到 NF_INET_LOCAL_OUT，每 2 秒打印统计
./nfctl block 53                            # 另一 netns 终端：拉黑 UDP 53，dig 应超时
./nfctl stats                               # 观察 pass/drop 计数变化
# 清理：./nfctl unblock 53 -> Ctrl-C -> exit -> netns-lab.sh down -> 删除 pinned map
```

预期看到：`run` 打印挂载信息与 pin 路径（`/sys/fs/bpf/nf_guard_rules`、`/sys/fs/bpf/nf_guard_stats`）；`block 53` 后 `dig +short example.com @1.1.1.1` 请求超时，`stats` 的 drop 计数增长（README 预期输出示例 / 运行级待验证）。完整实验流程与清理路径见示例 README。

**检查点**(自测后看提示):

1. “控制面/数据面分离”在本示例里各由谁承担？规则变更为什么不需要重新编译 BPF 程序？——提示：C 数据面只做解析报文、查 map、更新统计、返回动作；Go 控制面负责加载、规则下发与统计读取；规则在 map 里，改 map 即改规则，控制面也不进入高频报文路径。
2. map pinning 解决什么问题？rules 与 stats 两个 map pin 在哪、不 pin 会怎样？——提示：`/sys/fs/bpf/nf_guard_rules` 与 `/sys/fs/bpf/nf_guard_stats`（`LIBBPF_PIN_BY_NAME`）；不 pin 则 map 生命周期与 loader 进程 fd 绑定、退出即释放，`block`/`stats` 等独立子命令也无法直接打开同一个 map。
3. Netfilter、XDP、TC 的钩子位置与取舍各是什么？本示例为什么必须先建 netns？——提示：XDP 在驱动收包早期、TC 在 qdisc ingress/egress、Netfilter 在传统防火墙路径；一个错误的 drop 规则可能断开当前 SSH 连接，隔离拓扑加清理路径是网络路径示例的硬前提。

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

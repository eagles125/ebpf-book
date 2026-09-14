# 08-open-file-tracepoint：tracepoint 稳定版 opensnoop

对应 ebpf.top BPF 入门系列实践教程八（捕获进程打开文件信息 tracepoint 篇，系列第 11 篇），以及本书第 9 章第 11 节。

## 演示目标

- 理解 tracepoint 相对 kprobe 的核心优势：字段格式由内核显式提供并通过 tracefs
  暴露，不依赖内部函数名/参数布局，适合长期维护的工具。
- 学会先查 tracefs `format` 文件、再写代码的工作流（而不是复制别人机器上的结构）。
- 掌握 syscall tracepoint 上下文：enter 侧参数在 `ctx->args[]`，exit 侧返回值在 `ctx->ret`。

## 动手前先核对 format（本示例的正确打开方式）

```bash
sudo ls /sys/kernel/tracing/events/syscalls/ | grep openat
sudo cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/format
sudo cat /sys/kernel/tracing/events/syscalls/sys_exit_openat/format
```

`sys_enter_openat/format` 预期包含（预期输出为教学示例）：

```text
name: sys_enter_openat
ID: 624
format:
	field:unsigned short common_type;	offset:0;	size:2;
	field:int __syscall_nr;		offset:8;	size:4;
	field:int dfd;			offset:16;	size:4;
	field:const char __user * filename;	offset:24;	size:8;
	field:int flags;			offset:32;	size:4;
	field:umode_t mode;		offset:36;	size:2;
```

对照这张表即可确定：`args[1]` 是 filename 的用户态指针、`args[2]` 是 flags。

## 与 07（kprobe 版）的差异

| | 07（kprobe/kretprobe） | 08（tracepoint） |
|---|---|---|
| 挂载依据 | 内核函数名 + 参数位置（需读源码） | tracefs format 文件 |
| enter 侧文件名 | `BPF_KPROBE` 参数绑定 | `ctx->args[1]` |
| exit 侧返回值 | `BPF_KRETPROBE` 第一个参数 | `ctx->ret` |
| 稳定性 | 随内核内部实现变化 | 内核 UAPI 承诺，跨版本更稳 |
| 灵活性 | 任意函数（含非导出函数） | 仅限内核预埋的事件点 |

## 文件说明

| 文件 | 说明 |
|---|---|
| `opensnoop_tp_common.h` | 共享事件结构（与 07 一致） |
| `opensnoop_tp.bpf.c` | 内核态：sys_enter/sys_exit_openat 成对挂载 + inflight 关联 |
| `opensnoop_tp.c` | 用户态：输出格式与 07 一致 |

## 环境前提

同 `../01-hello-world/`；tracefs 已挂载（`/sys/kernel/tracing`）。

## 编译与运行

```bash
cd book/examples/chapter-09/08-open-file-tracepoint
make
sudo ./opensnoop_tp
# 另一终端触发：
cat /etc/hosts
cat /not-exist-file
```

## 预期输出

```text
PID       COMM             RET        FILE
1001      cat              fd=3       /etc/hosts
1002      cat              ERR=-2(ENOENT)  /not-exist-file
```

与 07 版本输出应逐行一致——两个版本只是“取数据的入口”不同。

## 代码讲解要点

- **`trace_event_raw_sys_enter` / `trace_event_raw_sys_exit`**：vmlinux.h 提供的
  syscall tracepoint 上下文结构，与 format 文件的字段一一对应。
- **enter/exit 关联**：与 07 相同的 inflight HASH（key = 64 位 pid_tgid）方案。
  tracepoint 的 exit 侧直接有 `ctx->ret`，比 kretprobe 更直接。
- **“稳定，仍要核对”**：不同架构、内核配置下事件字段可能变化
  （例如 `sys_enter_open` 与 `sys_enter_openat` 是不同事件）。
  先看 format 再写代码，是 tracepoint 开发的基本纪律。
- 本示例只挂 `openat`；要同时覆盖 `open`/`openat2`，需要为每个事件
  各写一个 section（BCC 的 opensnoop 正是这么做的）。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| attach 报未知 tracepoint | 事件名拼写错误；用 `ls /sys/kernel/tracing/events/syscalls/` 核对 |
| args 索引不对 | 直接套用了别的内核/架构的布局；重新看 format 文件 |
| 只想看 `open` 却有 `openat` 事件 | libc 的 `open()` 实际走 `openat` 系统调用；两个事件都要挂 |
| 事件丢失 | 叠加 PID 过滤（02 的模式）或增大 ring buffer |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；format 文件内容为教学示例，
  读者应以本机 `cat .../format` 输出为准。

## 参考来源

- ebpf.top: BPF 入门系列实践教程八（捕获进程打开文件信息 tracepoint 篇）。
- 内核文档：tracefs、syscall tracepoint 字段格式。
- BCC opensnoop 上游实现（open/openat/openat2 多事件覆盖参考）。

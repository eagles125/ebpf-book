# 第 9 章独立示例资产

本目录是《第 9 章 eBPF 可观测性入门：基于 ebpf.top 入门系列》的配套示例代码，
按 ebpf.top BPF 入门系列 12 篇文章的学习顺序组织，对应关系如下。

## 目录结构

| 目录 | 对应 ebpf.top 系列文章 | 演示主题 | 挂载点 | 输出机制 |
|---|---|---|---|---|
| `00-env/` | 第 3 篇：eBPF 开发环境搭建 | 环境自检 | - | - |
| `01-hello-world/` | 第 4 篇：实践教程一 Hello World | 第一个 BPF 程序 | `tracepoint/syscalls/sys_enter_execve` | `bpf_printk` |
| `02-pid-filter/` | 第 5 篇：实践教程二 通过 PID 过滤 | map + 内核态过滤 | `tracepoint/syscalls/sys_enter_execve` | `bpf_printk` |
| `03-container-security/` | 第 6 篇：实践教程三 借助 eBPF 保护容器安全 | cgroup id 过滤 + 敏感文件访问观测 | `tracepoint/syscalls/sys_enter_openat` | `bpf_printk` |
| `04-ring-buffer/` | 第 7 篇：实践教程四 在 eBPF 中使用 ring buffer | 结构化事件输出 | `tracepoint/syscalls/sys_enter_execve` | `BPF_MAP_TYPE_RINGBUF` |
| `05-open-file-kprobe/` | 第 8 篇：实践教程五 捕获进程打开文件信息 | kprobe 动态追踪 + 文件名读取 | `kprobe/do_sys_openat2` | ring buffer |
| `06-open-file-perf-event/` | 第 9 篇：实践教程六 perf_event 篇 | perf buffer 与 ring buffer 对照 | `kprobe/do_sys_openat2` | `BPF_MAP_TYPE_PERF_EVENT_ARRAY` |
| `07-open-file-return/` | 第 10 篇：实践教程七 返回值 | enter/exit 关联 + 临时 map | `kprobe/kretprobe/do_sys_openat2` | ring buffer |
| `08-open-file-tracepoint/` | 第 11 篇：实践教程八 tracepoint 篇 | 稳定事件接口 + `ctx->args[]` | `tracepoint/syscalls/sys_{enter,exit}_openat` | ring buffer |
| `09-netfilter-go/` | 第 12 篇：实践教程九 Netfilter 与 Go 经验总结 | 数据面/控制面分离 + Go 控制面 | `netfilter`（需内核 6.6+） | map（规则 + per-CPU 统计） |

## 环境前提

先运行环境自检：

```bash
bash book/examples/chapter-09/00-env/check-env.sh
```

统一要求：

- Linux kernel 5.10+（`09-netfilter-go` 需 6.6+）。
- `clang`/LLVM、`gcc`、`bpftool`、`libbpf-dev`、`make`；`09-netfilter-go` 另需 Go 1.21+ 和 `cilium/ebpf`。
- 运行 loader 需要 root，或具备 `CAP_BPF`、`CAP_PERFMON`（tracing 类）等能力。
- CO-RE 示例需要内核开启 `CONFIG_DEBUG_INFO_BTF`（存在 `/sys/kernel/btf/vmlinux`）。

## 统一构建模式

示例 01–08 采用与上游 `libbpf-bootstrap` 相同的三步构建，每个示例目录内直接 `make`：

```text
vmlinux.h            由 bpftool 从 /sys/kernel/btf/vmlinux 生成（不入库，make 自动生成）
*.bpf.c   -> *.bpf.o clang -target bpf 编译内核态程序
*.bpf.o   -> *.skel.h bpftool gen skeleton 生成 libbpf skeleton 头
*.c + *.skel.h -> 可执行 loader：gcc 链接 -lbpf -lelf -lz
```

生成的 `vmlinux.h`、`*.skel.h`、`*.o` 均为构建产物，已通过仓库 `.gitignore` 排除。

## 验证状态说明

本章示例的验证状态在各示例 `README.md` 末尾逐项标注，遵循 `book/STYLE.md` 的约定：

- **编译验证**：BPF 侧（`clang -target bpf`）与用户态侧（`gcc -lbpf`）已在编写本书的
  Linux x86_64 环境（内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）编译通过。
- **待运行验证**：加载与运行需要特权内核环境，编写环境为非 root，未执行实际加载；
  各 README 中的“预期输出”为教学说明，不是本地运行结果。
- `09-netfilter-go` 的 verifier 行为依赖内核版本，运行前请阅读其 README 的排错章节。

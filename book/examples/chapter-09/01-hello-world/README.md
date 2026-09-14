# 01-hello-world：第一个 eBPF 程序

对应 ebpf.top BPF 入门系列实践教程一（Hello World，系列第 4 篇），以及本书第 9 章第 4 节。

## 演示目标

- 完成“BPF C 源码 -> BPF 字节码 -> verifier 校验 -> JIT -> 挂载 -> 事件触发”的最小闭环。
- 理解 `bpf_printk` 的输出路径：内核 tracing buffer（`trace_pipe`），而不是 loader 的 stdout。
- 理解为什么第一个示例选择 `execve` tracepoint：容易触发、语义清楚、不会刷屏。

## 文件说明

| 文件 | 说明 |
|---|---|
| `hello.bpf.c` | 内核态 BPF 程序，挂在 `tracepoint/syscalls/sys_enter_execve` |
| `hello.c` | 用户态 loader：`hello__open_and_load()` + `hello__attach()` |
| `Makefile` | libbpf-bootstrap 式三步构建（vmlinux.h -> bpf.o -> skel.h -> loader） |

## 环境前提

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_PERFMON`。

## 编译与运行

```bash
cd book/examples/chapter-09/01-hello-world
make
sudo ./hello
```

另开一个终端：

```bash
sudo cat /sys/kernel/tracing/trace_pipe
# 触发：执行任意命令，如
/bin/true
```

## 预期输出

`trace_pipe` 中应看到类似内容（预期输出为教学示例）：

```text
            bash-12345   [003] .....  1234.5678: bpf_trace_printk: Hello, eBPF world!
            true-12348   [003] .....  1234.5701: bpf_trace_printk: Hello, eBPF world!
```

loader 自身输出：

```text
Hello World 已加载，按 Ctrl-C 退出。
在另一个终端查看输出：
  sudo cat /sys/kernel/tracing/trace_pipe
触发方式：执行任意命令（如 /bin/true、ls、echo test）
```

## 代码讲解要点

- `SEC("tracepoint/syscalls/sys_enter_execve")`：程序类型与挂载点由 section 名声明，
  libbpf 加载时可自动推断 attach 点，`hello__attach()` 无需额外参数。
- `bpf_printk` 是 `bpf_trace_printk` 的宏封装（单次调用最多 3 个参数）。
  它只适合教学与排错：字符串格式化开销大、高频事件下可能丢数据。
- `char LICENSE[] SEC("license")`：GPL 兼容声明，`bpf_trace_printk` 等 GPL-only helper 依赖它。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| `Operation not permitted` 加载失败 | 非 root 且缺少 `CAP_BPF`/`CAP_PERFMON`，用 `sudo` 运行 |
| `trace_pipe` 没有输出 | tracefs 未挂载（`sudo mount -t tracefs tracefs /sys/kernel/tracing`），或没有触发 `execve` |
| 编译找不到 `vmlinux.h` | 未先 `make` 生成，或内核缺少 BTF（参见附录 C） |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；上方“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程一（Hello World）。
- libbpf-bootstrap 上游示例 `examples/c/minimal`。
- libbpf 文档：`bpf_printk` 与 tracefs。

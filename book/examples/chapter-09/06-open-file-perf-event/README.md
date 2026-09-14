# 06-open-file-perf-event：perf buffer 输出版 opensnoop

对应 ebpf.top BPF 入门系列实践教程六（捕获进程打开文件信息 perf_event 篇，系列第 9 篇），以及本书第 9 章第 9 节。

## 演示目标

- 与 `../05-open-file-kprobe/` 逐行对照：同一挂载点、同一事件结构，只把
  输出机制从 ring buffer 换成 perf buffer，理解两种事件通道的差异。
- 掌握 `BPF_MAP_TYPE_PERF_EVENT_ARRAY` + `bpf_perf_event_output()` 的内核态用法。
- 掌握用户态 `perf_buffer__new()`（事件回调 + 丢失回调）与 `perf_buffer__poll()`。

## 与 05 的差异（速览）

| | 05（ring buffer） | 06（perf buffer） |
|---|---|---|
| map 类型 | `BPF_MAP_TYPE_RINGBUF` | `BPF_MAP_TYPE_PERF_EVENT_ARRAY` |
| 内核态输出 | `bpf_ringbuf_reserve()/submit()` | `bpf_perf_event_output(ctx, ...)` |
| 用户态消费 | `ring_buffer__new/poll` | `perf_buffer__new/poll` |
| 通道模型 | 单一 MPSC 队列 | 每 CPU 一个通道 |
| 丢事件感知 | reserve 返回 NULL（自己处理） | 丢失回调 `handle_lost_events` |
| 跨 CPU 内存拷贝 | 一次（事件生产处） | 通常一次，但管理面更复杂 |

## 文件说明

| 文件 | 说明 |
|---|---|
| `opensnoop_perf_common.h` | 共享事件结构（与 05 一致） |
| `opensnoop_perf.bpf.c` | 内核态：同挂点，perf buffer 输出 |
| `opensnoop_perf.c` | 用户态：poll + 事件/丢失双回调 |

## 环境前提

同 `../05-open-file-kprobe/`（perf buffer 自 4.x 起可用，兼容老内核）。

## 编译与运行

```bash
cd book/examples/chapter-09/06-open-file-perf-event
make
sudo ./opensnoop_perf
# 另一终端触发：
cat /etc/hosts
```

## 预期输出

```text
PID       COMM             FILE
1001      cat              /etc/hosts
1002      python3          /usr/lib/python3.12/os.py
警告：CPU 2 丢失 15 个事件（perf buffer 满）    <- 高负载时可能出现
```

## 代码讲解要点

- `BPF_F_CURRENT_CPU`：让内核用“当前 CPU id”作为 perf event array 的 key，
  事件落入当前 CPU 对应的缓冲，避免跨 CPU 写入。
- `perf_buffer__new(map_fd, 8, ...)`：第二个参数是每 CPU 缓冲的页数（页大小 4KB）。
  高频事件下出现丢失警告，就调大该值。
- **丢失回调是必选项而非装饰**：perf buffer 满时内核直接丢样并计数，
  用户态只有通过 lost 计数才能感知，否则数据“悄悄变少”。
- 事件回调签名 `(ctx, cpu, data, size)`：多 CPU 下回调可能并发执行，
  回调里直接 `printf` 教学够用；生产代码应只做入队，交给单线程消费。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 事件丢失警告刷屏 | 缓冲不足：调大页数；或降低事件频率（过滤） |
| 偶发乱码 | 两侧 `struct event` 不一致；或回调并发写共享缓冲 |
| 与 05 行为不一致 | 检查是否只改了输出机制；本示例刻意保持其余部分相同 |

## 什么时候选 perf buffer 而不是 ring buffer

- 新项目、内核 5.8+：优先 ring buffer（API 简洁、单一队列、内存效率高）。
- 老内核（<5.8）、BCC 时代工具链、或需要与既有 perf 生态对接：perf buffer。
- 本书在第 9 章保留两者对照，是为了读者在两种代码库间迁移时不迷路。

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程六（捕获进程打开文件信息 perf_event 篇）。
- libbpf 文档：`bpf_perf_event_output`、`perf_buffer__new/poll`。
- 内核文档：perf buffer 与 ring buffer 的设计差异。

# 04-ring-buffer：用 ring buffer 输出结构化事件

对应 ebpf.top BPF 入门系列实践教程四（在 eBPF 中使用 ring buffer，系列第 7 篇），以及本书第 9 章第 7 节。

## 演示目标

- 从“文本日志”（`bpf_printk`）升级到“结构化事件”：用户态按 `struct event` 精确解析。
- 掌握 ring buffer 事件生命周期：`bpf_ringbuf_reserve()` -> 填充 -> `bpf_ringbuf_submit()`
  （或 `bpf_ringbuf_discard()`），理解 reserve 失败就是缓冲区满时的背压路径。
- 掌握用户态消费模式：`ring_buffer__new()` 绑定回调，`ring_buffer__poll()` 循环消费。

## 文件说明

| 文件 | 说明 |
|---|---|
| `ringbuf_exec_common.h` | 内核态/用户态共享的事件结构定义 |
| `ringbuf_exec.bpf.c` | 内核态：`execve` 事件写入 ring buffer |
| `ringbuf_exec.c` | 用户态：poll 消费并按字段打印 |

## 环境前提

- 同 `../01-hello-world/`；ring buffer 需要内核 5.8+。

## 编译与运行

```bash
cd book/examples/chapter-09/04-ring-buffer
make
sudo ./ringbuf_exec
# 另一终端触发：
/bin/true; ls; echo test
```

## 预期输出

loader 的 stdout（不再是 trace_pipe）：

```text
PID       CGROUP_ID            COMM
12345     34501                bash
12348     34501                true
12349     34501                ls
```

## 代码讲解要点

- **事件结构固定大小**（`comm[16]` 对应内核任务名长度）：让 verifier 和用户态
  reader 都能明确事件边界。变长内容（如长文件名）用定长数组 + 截断处理。
- **reserve/submit/discard 三段式**：reserve 返回 NULL 表示 ring buffer 已满——
  高频事件下必须考虑丢弃路径；入门示例直接静默丢弃，生产工具应增加
  per-CPU drop 计数让用户态感知丢事件。
- **共享头文件纪律**：`struct event` 在内核态与用户态必须是同一份定义。
  本示例用 `*_common.h` 同时被两侧 include，杜绝“两侧结构体不一致导致输出乱码”。
- `ring_buffer__poll(ring, 100)`：100ms 超时返回 0 是正常“无事件”，负值才按错误处理。
  这个语义适合写 daemon：主循环可以边 poll 事件边响应信号。

## 与 bpf_printk 的对比

| | `bpf_printk` | ring buffer |
|---|---|---|
| 数据形态 | 文本 | 二进制结构体 |
| 输出路径 | trace_pipe | 用户态回调 |
| 开销 | 高（字符串格式化 + 全局 buffer） | 低（预留-提交，MPSC） |
| 生产适用性 | 仅排错 | 推荐的事件通道 |

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| reserve 后忘记 submit/discard | verifier 拒绝（引用未释放），检查每条路径 |
| 用户态解析乱码 | 两侧 `struct event` 布局不一致，统一走 `*_common.h` |
| 高频事件丢数据 | ring buffer 满；增大 `max_entries` 或在用户态更快消费 |
| `ring_buffer__poll` 返回 -EINTR | 被 SIGPROF 等信号打断；如非预期可重试或退出 |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程四（在 eBPF 中使用 ring buffer）。
- Andrii Nakryiko: BPF ring buffer 设计文章（kernel v5.8 引入）。
- libbpf 文档：`bpf_ringbuf_reserve/submit/discard`、`ring_buffer__poll`。

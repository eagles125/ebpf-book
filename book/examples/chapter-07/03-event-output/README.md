# 03-event-output:事件输出双变体——ring buffer vs perf buffer(编译级已验证)

同一个事件源(sys_enter_execve tracepoint)、同一种事件({pid, comm}),两条输出通道各一套完整实现:BPF 侧 `bpf_ringbuf_reserve`+`submit` 对 `bpf_perf_event_output`;用户态 `ring_buffer__poll` 对 `perf_buffer__poll`。这是正文四节 perf vs ringbuf 五点对比的实证面。

- **验证状态:编译级已验证**——`make` 全链零告警产出两个可执行;**运行级待验证**(需 root):加载/attach/事件收发均为预期输出示例。
- **安全声明:构建期零内核交互;tracepoint 挂载不涉及网卡。**

## 构建与运行

```bash
make                        # 编译级:产出 event_ringbuf 与 event_perf
sudo ./event_ringbuf        # 变体一(运行级待验证)
sudo ./event_perf           # 变体二(运行级待验证)
# 产生事件:另开终端执行任意命令即可(每次 exec 触发一条)
```

**预期输出示例(运行级待验证,root 档)**:

```text
[ringbuf] pid=12345  comm=bash
[ringbuf] pid=12346  comm=uname
```
```text
[perf]    pid=12345  comm=bash (cpu2)
[perf]    pid=12346  comm=uname (cpu0)
```

## 两通道对照(正文四节五点对比的速查版)

| 维度 | ring buffer | perf buffer |
|---|---|---|
| 缓冲结构 | 单一共享环形缓冲(meta 页 + 双数据区) | 每 CPU 一个 perf 环(PERF_EVENT_ARRAY 按 CPU 号索引) |
| BPF 侧写法 | reserve → 填 → submit(两段式,可中途 discard) | 一次性拷贝(`bpf_perf_event_output`) |
| 跨 CPU 顺序 | 提交顺序即全局顺序(单 prod_pos 定序) | 各 CPU 环独立,无全局顺序 |
| 丢失语义 | 满时 `reserve` 返回 NULL,BPF 侧静默丢弃 | 环满走 `on_lost` 回调,丢失计数可观测 |
| 用户态接口 | `ring_buffer__new` + `ring_buffer__poll` | `perf_buffer__new` + `perf_buffer__poll`(依赖 perf_event_open) |

选型结论(正文四节展开):需要跨 CPU 时序与大缓冲吞吐选 ringbuf;需要与 perf 生态(perf_event_open 档位、采样语义)对齐或逐 CPU 隔离时选 perf buffer。

## 正文对应

- ringbuf 内核侧:`kernel/bpf/ringbuf.c`(`__bpf_ringbuf_reserve` :408、`bpf_ringbuf_commit` :489、`bpf_ringbuf_notify` :150、mmap :261)——正文四节逐段引用;切片器扩展见 [`../exercises/02-extend-ringbuf/`](../exercises/02-extend-ringbuf/)。
- perf 内核侧:`kernel/trace/bpf_trace.c`(`bpf_perf_event_output` :660、`__bpf_perf_event_output` :618)与 `kernel/bpf/arraymap.c`(`perf_event_array_map_ops` :1245)。

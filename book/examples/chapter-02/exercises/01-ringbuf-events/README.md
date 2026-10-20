# 练习 1：把轮询改为 ring buffer 事件流

第 2 章《eBPF 架构与核心概念》课后扩展任务 1（扩展维度：事件通道）：以 01-trace-stat（`../../01-trace-stat/`）为基线，把「loader 定时轮询映射读回」改为「数据面有事件就经 ring buffer 推送」。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在 01-trace-stat 的基础上完成事件通道换形：

1. 内核态：不再往 `BPF_MAP_TYPE_PERCPU_ARRAY` 累计计数，改为定义一个调度事件结构（时间戳、`prev_pid`、`next_pid`、CPU 号），每次 `sched_switch` 触发就把一条事件推入 `BPF_MAP_TYPE_RINGBUF` 映射。
2. 用户态：loader 不再每秒 `bpf_map_lookup_elem` 读回求和，改为 `ring_buffer__new` 挂回调 + `ring_buffer__poll` 事件循环，逐事件打印。
3. 保持与基线相同的挂载方式（SEC 名自动推断）与 Ctrl-C 退出即卸载的生命周期。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] 加载后能看到带 CPU 号、时间戳与 pid 对的事件流：每行形如 `cpu<N> <时间戳> ns: prev=<pid> next=<pid>`，且行内容随系统调度持续增长。
- [ ] 能说明它与轮询的取舍：延迟、开销、丢失语义、实现复杂度四个维度（与正文「常见问题与排错」节第四组对照表同口径）。

## 提示

只给「会用到什么」，不给写法：

- 映射类型涉及 `BPF_MAP_TYPE_RINGBUF`：对照 3.6 想一想它和 01-trace-stat 的 per-CPU 数组各自适合什么数据形态。
- 内核态推事件用 `bpf_ringbuf_reserve` / `bpf_ringbuf_submit` 两段式（3.6 的 reserve/submit）；事件结构有 **8 字节对齐**的硬性要求，想想 24 字节的结构里哪个字段是为此存在的。
- 用户态消费用 `ring_buffer__new`（绑定映射 fd 与回调）与 `ring_buffer__poll`（带超时轮询）——超时的存在理由与 Ctrl-C 的及时性有关。
- 满表丢弃语义：缓冲打满时 `bpf_ringbuf_reserve` 返回 NULL，观测程序此刻应该做什么（提示：观测不得反压调度路径）。
- 上下文字段仍从 `struct trace_event_raw_sched_switch` 的 `ctx->prev_pid` / `ctx->next_pid` 读取（vmlinux.h CO-RE）；时间戳与 CPU 号各有对应辅助函数（1.3/5.2 已见过其中之一）。
- skeleton 不会导出自定义 map value/事件结构类型——loader 侧要自己重新定义同构结构（01-trace-stat 已有先例）。

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `bash ../../00-env/check-env.sh` 自检）。
- 运行需要 root 或 `CAP_BPF`（+ `CAP_PERFMON`/`CAP_SYS_ADMIN`）；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。
- 安全警告：tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

```bash
cd book/examples/chapter-02/exercises/01-ringbuf-events
make
sudo ./trace_events          # 打印满 50 条后自动退出；Ctrl-C 可提前退出
sudo ./trace_events 20       # 自定义打印条数上限
```

触发：无需专门负载——sched_switch 系统空闲时也持续触发，运行后立刻能看到事件流。

## 预期输出示例

（预期输出示例 / 运行级待验证）

```text
cpu0  123456789 ns: prev=1823 next=910
cpu2  123457891 ns: prev=910  next=1823
cpu1  123458790 ns: prev=3456 next=789
cpu0  123459788 ns: prev=789  next=1823
```

- 时间戳来自内核单调时钟（`bpf_ktime_get_ns`，非墙钟），单调递增；同一 CPU 号可以反复出现。
- `sched/sched_switch` 事件速率极高：若消费侧打印不及时，16 MB 缓冲打满后新事件被静默丢弃，属预期行为（取舍讲解见下文分界线之后的思路节）。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`trace_events.bpf.c`/`trace_events.c`），建议自行实现后再对照：

1. **事件结构（`struct sched_event`）**：`__u64 ts_ns` + 4 个 `__u32`，共 24 字节——是 8 的倍数，满足 ringbuf 对记录 **8 字节对齐**的硬性要求；`pad` 字段存在的唯一理由就是补齐对齐，写入时清零，避免把栈上垃圾数据提交进缓冲。时间戳用 `bpf_ktime_get_ns()`（内核单调时钟），CPU 号用 `bpf_get_smp_processor_id()`。
2. **推送三步（`trace_events` 程序体）**：`bpf_ringbuf_reserve` 租一块记录 → 填字段 → `bpf_ringbuf_submit` 提交。关键在第一步失败的分支：**满表时 reserve 返回 NULL，直接 `return 0` 静默丢弃**——观测程序绝不能在调度路径上自旋重试或阻塞（反压内核路径），这是事件流形态必须接受的丢失语义（对照排错节第四组「丢失语义」一行）。
3. **loader 事件循环（`trace_events.c`）**：`ring_buffer__new(maps.events 的 fd, on_event, ...)` 把映射与回调绑定，`ring_buffer__poll(rb, 100)` 以 100 ms 超时轮询——**带超时**是刻意的：暂无事件时线程也能醒来检查 `exiting` 标志，保证 Ctrl-C 及时生效；回调里只做格式化打印，达到条数上限后丢弃积压事件，主循环条件随即退出。loader 侧的 `struct sched_event` 与 BPF 侧逐字段一致（skeleton 不导出自定义类型，同 01-trace-stat 的 `struct cpu_stat` 先例）。
4. **与 01-trace-stat 的差异点对照**：

| 维度 | 01-trace-stat（轮询 map） | 本练习（ring buffer 事件流） |
|---|---|---|
| 延迟 | 新鲜度 = 轮询周期（1 秒），中间变化不可见 | 每事件可见，poll 唤醒、延迟低 |
| 开销 | 数据面约等于零，代价集中在读回侧，与事件速率无关 | 每事件一次 reserve/submit，随事件速率线性增长 |
| 丢失语义 | 无「丢失」概念，只有「最新状态快照」 | 缓冲满则 reserve 返回 NULL、新事件静默丢弃 |
| 实现复杂度 | 最低：lookup + 求和打印 | 中等：8 字节对齐、回调函数、poll 超时与 Ctrl-C 协作 |

   选型口诀（1.7）：要「最新状态」选轮询，要「每一件事」选事件流；选了事件流，要管理的就是丢失——加大缓冲、在 BPF 侧对丢弃计数。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- Linux kernel documentation：BPF ring buffer（`Documentation/bpf/ringbuf.rst`，reserve/submit 两段式与 8 字节对齐要求）。
- Linux kernel documentation：BPF 映射与 tracepoints（`Documentation/bpf/maps.rst`、`Documentation/trace/tracepoints.rst`）。
- libbpf 文档：`ring_buffer__new`/`ring_buffer__poll` 事件循环 API 与 skeleton 加载模式。
- 本章正文 3.6（ring buffer card）、排错节第四组（轮询与事件流的取舍）与本目录 README 的对照表。
- 基线示例 `book/examples/chapter-02/01-trace-stat/`：本练习的改造对象。

# 练习 3：ring buffer → perf buffer 迁移对照

第 9 章《eBPF 可观测性入门：基于 ebpf.top 入门系列》课后扩展任务 3（综合类）：以 04-ring-buffer（`../../04-ring-buffer/`）为基线（机制讲解参照 §7/§9 与 06-open-file-perf-event），把 execve 事件流从 ring buffer 迁移到 perf buffer——同一事件结构、同一挂载点、同一输出列，只换事件通道。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在 04-ring-buffer 的基础上完成输出机制迁移：

1. 内核态：`BPF_MAP_TYPE_RINGBUF` + `bpf_ringbuf_reserve/submit` 换成 `BPF_MAP_TYPE_PERF_EVENT_ARRAY` + `bpf_perf_event_output`；事件结构（pid/cgroup_id/comm）、挂载点（`tracepoint/syscalls/sys_enter_execve`）与输出语义一律不动。
2. 用户态：`ring_buffer__new`/`ring_buffer__poll` 换成 `perf_buffer__new`/`perf_buffer__poll`；事件行打印格式保持 04 同形（`PID CGROUP_ID COMM` 三列），并补上 perf buffer 侧必须显式处理的丢失回调。
3. 自查方式：与基线目录逐行 diff——除了事件通道那一对调用，不应出现任何其他语义差异。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] 加载后输出与 04 同形的三列事件流；高负载下可能出现「CPU N 丢失 X 个事件」的丢失警告，而 04 的 ring buffer 版本对应场景是 reserve 返回 NULL 的静默丢弃。
- [ ] 能回答正文 §9 检查点 2 的对照问题：两种机制在「缓冲满」时各走什么路径、用户态各靠什么感知丢失。

## 提示

只给「会用到什么」，不给写法：

- perf event array 的 map 定义只要 `key_size`/`value_size` 各 4 字节，不需要 `max_entries`——通道数由系统 CPU 数决定（对照 06 的 map 定义）。
- 内核态输出一次调用完成：`BPF_F_CURRENT_CPU` 会把事件写到哪个缓冲？没有 reserve/submit 两段式，也就没有「预留失败」分支——丢失发生在哪里、由谁计数？
- 用户态 `perf_buffer__new` 要多给什么参数（每 CPU 页数、丢失回调）；事件回调的签名与 ring buffer 版有何不同（多了哪个参数）？
- 迁移不等于复述 06：06 的基座是 opensnoop（kprobe + filename），本练习基座是 04 的 execve 事件——把「同一事件换通道」做干净，diff 才有教学价值。

## 环境前提与构建

同 `../../04-ring-buffer/`：Linux kernel 5.10+（ring buffer 需 5.8+，本练习迁移后不再依赖），开启 `CONFIG_DEBUG_INFO_BTF`；`clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `bash ../../00-env/check-env.sh` 自检）。运行 loader 需要 root 或 `CAP_BPF`/`CAP_PERFMON`；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。

```bash
cd book/examples/chapter-09/exercises/03-ringbuf-to-perf
make
sudo ./perf_exec
# 另一终端触发：
/bin/true; ls; echo test
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

loader 的 stdout（与 04 同形的三列事件流；预期输出为教学示例）：

```text
PID      CGROUP_ID            COMM
12345    34501                bash
12348    34501                true
12349    34501                ls
```

列宽按 `perf_exec.c` 中 `handle_event` 的 `%-8u %-20llu %s` 格式串逐列排布，与 04 的输出完全一致——迁移不应改变任何一列。高负载下 stderr 可能出现丢失警告（预期输出为教学示例）：

```text
警告：CPU 2 丢失 128 个事件（perf buffer 满）
```

---

## 参考实现思路

以下思路对应本目录内的参考实现（`perf_exec_common.h`/`perf_exec.bpf.c`/`perf_exec.c`），建议自行实现后再对照：

1. **共享头（`perf_exec_common.h`）**：与 04 的 `ringbuf_exec_common.h` 逐字段一致（`pid`/`cgroup_id`/`comm[16]`），注释里写明迁移纪律——「只换通道、不换事件」。共享头两侧同一份定义的纪律不变，这正是 diff 自查能成立的前提。
2. **map 定义（`perf_exec.bpf.c`）**：`BPF_MAP_TYPE_PERF_EVENT_ARRAY`，`key_size`/`value_size` 各 `sizeof(__u32)`，不需要 `max_entries`——perf event array 的每 CPU 通道由内核按 CPU 数管理，与 06 的 map 定义同形。ring buffer 版的 `max_entries 256 * 1024` 在这里没有对应物：容量语义变成了「每 CPU 页数」，由用户态 `perf_buffer__new` 的第二参指定（本实现给 8 页/每 CPU，与 06 一致）。
3. **内核态输出调用**：`bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e))` 一次完成；`BPF_F_CURRENT_CPU` 让事件写进当前 CPU 的通道，避免跨 CPU 竞争。与 reserve/submit 的对照要点：没有预留失败分支可写——per-CPU 缓冲满时事件在内核侧被丢弃并计入 lost 计数，观测程序不会反压事件源。源码注释按此口径写，避免读者误以为还有「满时 NULL」可判。
4. **用户态消费（`perf_exec.c`）**：`perf_buffer__new(map_fd, 8, handle_event, handle_lost_events, NULL, NULL)` + `perf_buffer__poll(pb, 100)` 循环（100ms 超时与 Ctrl-C 协作，同 04/06 的节奏）。事件回调签名多了 `cpu` 与 `data_sz` 参数；多 CPU 下回调可能并发触发。丢失回调非装饰：没有它，缓冲满导致的丢样「悄悄变少」无从察觉——这正是正文 §9 检查点 2 的答案：ring buffer 走 reserve 返回 NULL 的背压路径（内核不丢、由程序自己决定丢弃策略），perf buffer 走内核丢样 + lost 计数上报（程序不接回调就看不见丢失）。
5. **diff 自查清单**：与 04 逐行 diff，差异应恰为四处——map 定义、内核输出调用、用户态 reader 创建与 poll、丢失回调（新增）；事件结构、挂载点、printf 格式串、生命周期（Ctrl-C 卸载）零差异。若 diff 出了第五处差异，多半是把「迁移」做成了「重写」。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- ebpf.top：BPF 入门系列实践教程四/六（ring buffer 篇与 perf_event 篇）。
- 本章正文 §7/§9：两种事件通道的机制讲解与对比口径；§9 检查点 1–3（per-CPU 通道、丢失回调、BPF 侧与用户态侧各换的一对调用）。
- libbpf 文档：`perf_buffer__new`/`perf_buffer__poll` 与 lost samples 回调签名。
- 基线示例 `book/examples/chapter-09/04-ring-buffer/`（迁移起点）与 `book/examples/chapter-09/06-open-file-perf-event/`（perf buffer 消费模式参照）。

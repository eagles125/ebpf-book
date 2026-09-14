# 02-pid-filter：通过 map 做 PID 过滤

对应 ebpf.top BPF 入门系列实践教程二（通过 PID 过滤，系列第 5 篇），以及本书第 9 章第 5 节。

## 演示目标

- 理解 `bpf_get_current_pid_tgid()` 的布局：高 32 位是进程 PID（tgid），低 32 位是线程 ID。
- 理解为什么用 ARRAY map 保存目标 PID，而不是把 PID 写死在 BPF 代码里：
  换目标进程不需要重新编译，过滤条件在内核事件现场生效，噪声不会传到用户态。
- 体会 map 是“内核对象而非编译期常量”：用户态更新后，内核态下一次触发即可见。

## 文件说明

| 文件 | 说明 |
|---|---|
| `pid_filter.bpf.c` | 内核态：读 `target_pid` map 过滤 `execve` 事件 |
| `pid_filter.c` | 用户态 loader：`-p <pid>` 参数写入 map |

## 环境前提

同 `../01-hello-world/`（内核 5.10+、BTF、clang/bpftool/libbpf-dev、root 运行）。

## 编译与运行

```bash
cd book/examples/chapter-09/02-pid-filter
make

# 终端 1：只观察 bash 的 execve
sudo ./pid_filter -p $(pgrep -x bash)

# 终端 2：查看输出并触发
sudo cat /sys/kernel/tracing/trace_pipe
/bin/true   # 若 /bin/true 由 bash 直接 exec，触发的是 bash 的事件
```

## 预期输出

指定 `-p <bash pid>` 后，`trace_pipe` 只出现该 PID 的事件（预期输出为教学示例）：

```text
            bash-12345   [002] .....  2345.6789: bpf_trace_printk: execve: pid=12345 comm=bash
```

其他进程（systemd、监控 agent 等）的 `execve` 不会再出现——过滤发生在内核态，
这正是与“把全部事件传到用户态再过滤”的本质区别。

## 代码讲解要点

- `bpf_get_current_pid_tgid() >> 32`：按进程过滤；多线程程序所有线程共享同一个 tgid。
  若要跟踪单个线程，才需要低 32 位。
- `BPF_MAP_TYPE_ARRAY` + `max_entries 1`：key 恒为 0，充当“全局配置变量”。
- `if (target && *target != 0 && *target != current)`：`bpf_map_lookup_elem()`
  可能返回 NULL，必须先判空再解引用，否则 verifier 直接拒绝加载。
- 用户态 `bpf_map_update_elem()` 使用 `BPF_ANY`：创建或更新均可。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| verifier 报 invalid mem access（读 map 返回值） | 漏了 NULL 判断，对照上面讲解要点检查 |
| 过滤不生效 | `-p` 传了不存在的 PID；或目标命令是 shell 内建（如 `echo` 可能由 bash fork 而非 exec） |
| 想运行中改目标 PID | 当前示例只在启动时写一次；可扩展为信号处理循环中重复调用 `bpf_map_update_elem()` |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程二（通过 PID 过滤）。
- libbpf 文档：ARRAY map 与 `bpf_map_lookup_elem`/`bpf_map_update_elem`。
- 内核文档：`bpf_get_current_pid_tgid` helper 语义。

# 03-container-security：观测容器内进程的敏感文件访问

对应 ebpf.top BPF 入门系列实践教程三（借助 eBPF 保护容器安全，系列第 6 篇），以及本书第 9 章第 6 节。

## 演示目标

- 把过滤维度从 PID 扩展到容器：理解为什么 cgroup id 比 PID 更适合标识容器
  （PID 随容器重启变化，cgroup 层级更贴近容器生命周期）。
- 学会用 `bpf_get_current_cgroup_id()` + HASH map 实现容器级策略下发。
- 明确“观测”与“阻断”的边界：本示例只记录，不改变系统调用结果。

## 事件流

```text
进程打开文件（openat）
        |
        v
eBPF 读取 cgroup_id / pid / comm / filename
        |
        v
target_cgroups 命中？（HASH map 查询）
        | 是
        v
bpf_printk 输出敏感访问事件 -> trace_pipe
```

## 文件说明

| 文件 | 说明 |
|---|---|
| `container_guard.bpf.c` | 内核态：cgroup 过滤 + `openat` 文件名读取 + 事件输出 |
| `container_guard.c` | 用户态 loader：`-c <cgroup-id>` 注册目标容器 |

## 环境前提

- 同 `../01-hello-world/`，且使用 cgroup v2（`stat -f -c %T /sys/fs/cgroup` 应为 `cgroup2fs`）。
- 需要 root 运行。`bpf_get_current_cgroup_id()` 在 cgroup v1 环境下语义不同，本示例以 v2 为准。

## 编译与运行

```bash
cd book/examples/chapter-09/03-container-security
make

# 1. 获取目标容器的 cgroup id（cgroup v2 的 inode号）
stat -c %i /sys/fs/cgroup/system.slice/docker-<容器长ID>.scope
# 或使用本机 shell 所在 cgroup 做最小实验（见下方“简化实验”）

# 2. 启动观测
sudo ./container_guard -c <cgroup-id>

# 3. 另一终端查看输出
sudo cat /sys/kernel/tracing/trace_pipe
```

简化实验（不依赖真实容器）：把当前 shell 所在 cgroup 当作“目标容器”：

```bash
CGID=$(stat -c %i "/sys/fs/cgroup$(grep -o '/.*' /proc/self/cgroup | head -1)")
sudo ./container_guard -c "$CGID"
# 然后在该 shell 中执行 cat /etc/hosts、ls 等命令，观察 trace_pipe 输出
```

## 预期输出

`trace_pipe` 中应看到目标 cgroup 内进程的文件访问（预期输出为教学示例）：

```text
   container-guard: cgid=4321 pid=1001 comm=cat open(/etc/hosts)
   container-guard: cgid=4321 pid=1002 comm=sh open(/etc/shadow)
```

非目标 cgroup 的进程（宿主机其他进程）不会出现——过滤发生在内核态。

## 代码讲解要点

- `bpf_get_current_cgroup_id()`：返回当前任务所属 cgroup 的 inode 号（cgroup v2），
  与用户态 `stat -c %i <cgroup 路径>` 的值可直接对应。
- HASH map `target_cgroups`：key 是 cgroup id，value 是策略标志。本示例策略固定为
  `POLICY_LOG`；`POLICY_DENY` 是占位，提示读者阻断需要更换挂载点。
- `ctx->args[1]`：syscall tracepoint 上下文中的调用参数数组，`args[1]` 是 openat 的
  filename 指针（用户态地址），须用 `bpf_probe_read_user_str()` 读取。
  字段布局以 `/sys/kernel/tracing/events/syscalls/sys_enter_openat/format` 为准，
  详见 `../08-open-file-tracepoint/`。

## 观测与阻断的边界（重要）

tracepoint/kprobe 类挂载点只能**记录**事件，不能改变系统调用结果。
“看到容器进程打开 /etc/shadow”不等于“阻止了这次打开”。要做阻断，应选择具备
返回决策语义的挂载点：

- LSM BPF（如 `lsm/file_open`，返回 `-EPERM` 可拒绝操作，需内核 `CONFIG_BPF_LSM`）；
- 网络路径的 XDP / TC / Netfilter（返回 drop/accept 类动作）。

把观测型探针误用成访问控制系统，是入门阶段常见的安全错觉。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 没有任何输出 | cgroup id 不匹配（v1/v2 混用，或路径选错）；目标进程没有触发 openat |
| verifier 拒绝读 `ctx->args[1]` | 先确认 format 文件中 args 字段存在；不要把 args 当内核指针直接解引用 |
| 容器里跑的程序在宿主机视角 cgroup 不对 | 用 `cat /proc/<pid>/cgroup` 确认目标进程真实 cgroup 路径 |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。
- `POLICY_DENY` 阻断路径未实现，属有意为之（见“观测与阻断的边界”）。

## 参考来源

- ebpf.top: BPF 入门系列实践教程三（借助 eBPF 保护容器安全）。
- 内核文档：`bpf_get_current_cgroup_id` helper、cgroup v2 接口。
- BPF LSM 文档（阻断方向的延伸阅读）。

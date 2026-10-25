# 01-kretprobe：在目标函数返回处读返回值——do_sys_openat2 成败计数（类型画廊①）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.8 节（kretprobe card）
的配套最小程序。kprobe 看入口参数，kretprobe 看「办成了没有」：把程序挂到
`do_sys_openat2`（openat2 系统调用的公共实现）的**返回处**，按返回值正负分桶计数——
`ret>=0` 是成功（返回 fd）、负值是失败（负错误码），一行读数就是这条系统调用的成败分布。

## 任务一句话

用 `BPF_KRETPROBE` 宏写一个 kretprobe，挂 `do_sys_openat2` 返回处，把每次系统调用的
返回值按「成功（fd）/ 失败（负错误码）」两个 key 计数进一个 ARRAY map。

## 代码走读

- **返回值读取靠宏**：`int BPF_KRETPROBE(krp, int ret)`——宏由 `<bpf/bpf_tracing.h>` 提供，
  按架构展开取返回值寄存器（x86 由 `PT_REGS_RC`——以所用架构为准），把「手工从
  pt_regs 捞返回值」变成一个有类型的形参 `ret`。与 kprobe card 的 `PT_REGS_RC` 手工取参
  相对照：kretprobe 的典型写法就是宏一包。
- **ctx 形状**：宏内部仍是 `struct pt_regs *`——kretprobe 与 kprobe 同属 KPROBE 程序类型，
  差别只在挂载时刻（入口 vs 返回处）与「要不要读返回值」。
- **map 选型**：`BPF_MAP_TYPE_ARRAY`、`max_entries=2`——成功/失败两个 key 固定计数；
  0 号 key 收 `ret>=0`（fd），1 号 key 收负错误码；计数用 `__sync_fetch_and_add` 原子递增。
- **失败分支必检**：`bpf_map_lookup_elem` 返回 NULL 即退出——第 3 章画廊 card ①
  （未检查的映射查找）的正面写法，此处仅回指不重复展开。
- **SEC 命名**：`SEC("kretprobe/do_sys_openat2")`——前缀决定程序类型与挂载点解析，
  斜杠后是目标函数名，加载时按内核符号解析。

## 挂载方式与前提

- **预期加载前提**：root/特权档（root 或 `CAP_BPF`/`CAP_PERFMON`）；本机
  `unprivileged_bpf_disabled=2` 时非特权用户 load 即失败（预期失败，非缺陷）。
- **挂载形态**：kretprobe 属 tracefs 事件类挂载，`/sys/kernel/tracing/events/kprobes/`
  下生成对应 `<事件>_ret` 事件；libbpf 侧 `bpf_program__attach_kprobe(…, retprobe=true)`，
  或 skeleton 自动 attach（`SEC` 前缀 `kretprobe/` 即按 retprobe 解析）。
- **目标函数**：`do_sys_openat2` 的签名已按编写环境 BTF 核实（内核 7.0）；换内核版本时
  签名可能漂移，加载前用 `bpftool btf dump` 复核（章级 README「常见失败点」同款口径）。

## 预期输出示例

（预期输出示例 / 运行级待验证）本目录 compile-only、无 loader；读数需几行
`bpf_map_lookup_elem` 或直接 `bpftool map dump`：

```text
$ sudo bpftool map dump name openat2_verdict
key: 00 00 00 00  value: 0f 00 00 00 00 00 00 00   # 0=成功（fd 计数）
key: 01 00 00 00  value: 02 00 00 00 00 00 00 00   # 1=失败（负错误码计数）
Found 2 elements
```

随便跑几条 `ls`/`cat` 后 key 0 持续增长；构造失败调用（如 `cat /不存在/路径`）则
key 1 增长。具体数值随负载，以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `kretprobe.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/trace/kprobes.rst`（kretprobe 语义与限制）。
- `<bpf/bpf_tracing.h>`：`BPF_KRETPROBE` 宏定义（`PT_REGS_RC` 按架构展开）。
- 内核源码 `fs/open.c`：`do_sys_openat2` 实现。
- 第 3 章失败画廊 card ①：`book/examples/chapter-03/02-fail-gallery/01-map-null/`
  （本程序 `if (!v)` 分支的出处）。

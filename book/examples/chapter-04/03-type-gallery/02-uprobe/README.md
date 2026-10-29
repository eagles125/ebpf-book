# 02-uprobe：挂进用户态函数——SEC 串只是标记，路径与偏移挂载时给（类型画廊②）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.9 节（uprobe card）
的配套最小程序。uprobe 把探针从内核函数搬到用户态二进制：任意 ELF 可执行文件/共享库的
任意符号偏移都可以成为挂点。本目录程序只做一件事——被触发即计数，教学焦点全在
「SEC 怎么写、路径与偏移从哪来」。

## 任务一句话

写一个 `SEC("uprobe")` 的用户态探针，命中即向 ARRAY map 的 0 号 key 原子 +1；
二进制路径与偏移由挂载方（loader/bpftool）提供，编译期不写死。

## 代码走读

- **ctx 形状**：`int up(struct pt_regs *ctx)`——libbpf 解析后 uprobe 转成 KPROBE 类型的
  uprobe 变体，ctx 仍是 pt_regs（被打断处的用户态现场），与 kprobe 同族。
- **SEC 命名规则**：完整形态是 `uprobe/<二进制路径>:<偏移>`——但路径与偏移是**挂载期**
  信息，本程序只写 `SEC("uprobe")` 留空，挂载时由 loader/bpftool 提供；编译期 SEC 串
  只是标记。路径/偏移写错时编译照过、attach 才报错——「类型检查在加载期」的又一实证。
- **map 选型**：单 key ARRAY（`uprobe_hits`）——命中计数一个就够；`__sync_fetch_and_add`
  原子递增。
- **失败模式回指**：路径/偏移错→attach 失败；用户内存读必须走 probe 系 helper——
  界限讲解见第 3 章画廊 card ⑦（ctx 标量字段强转指针），此处回指不重复展开。

## 挂载方式与前提

- **预期加载前提**：root + 指定二进制路径与偏移（uprobe 挂载要向目标二进制注入断点，
  需特权；偏移取目标 ELF 内的文件偏移（PIE/共享库下与运行时虚拟地址不同）——`nm -D`/`readelf -s` 可查，或用
  `bpf_program__attach_uprobe_opts` 传 `func_name` 交由 libbpf 查符号表）。
- **挂载形态**：libbpf `bpf_program__attach_uprobe(…, pid, binary_path, func_offset)`；
  挂上后 `/sys/kernel/tracing/events/uprobes/` 出现对应事件。教学挂法：挑一个常用
  共享库（如 libc）的符号挂点，触发后用 `bpftool map dump name uprobe_hits` 读数。
- **与 kretprobe 的对称**：`uretprobe` 是用户态的「办成了没有」——SEC 前缀
  `uretprobe/`，读返回值同样可用 `BPF_KRETPROBE` 系宏，本画廊不单列目录。

## 预期输出示例

（预期输出示例 / 运行级待验证）假设挂上某二进制的某符号后，反复触发该函数：

```text
$ sudo bpftool map dump name uprobe_hits
key: 00 00 00 00  value: 2a 00 00 00 00 00 00 00   # 命中 42 次
Found 1 element
```

计数只随「挂点函数被调用」增长；挂到不同二进制/偏移则各挂各的计数。具体数值随负载，
以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `uprobe.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/trace/uprobetracer.rst`（uprobe 语义）。
- libbpf 文档：`bpf_program__attach_uprobe()` / `bpf_program__attach_uprobe_opts()`
  （`binary_path`/`func_offset`/`func_name` 参数语义）。
- 内核源码 `kernel/trace/bpf_trace.c`（uprobe 变体的 KPROBE 类型归类）。
- 第 3 章失败画廊 card ⑦：`book/examples/chapter-03/02-fail-gallery/07-kernel-ptr/`
  （用户内存读界限的失败形态）。

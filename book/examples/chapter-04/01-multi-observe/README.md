# 01-multi-observe：同一事件五副眼镜——openat2 经 5 类追踪程序观察（追踪类集成锚点）

《第 4 章 eBPF 程序类型与钩子点深入》「四、实战：为任务选对类型并挂上钩子」的追踪类集成锚点，
同时是「三、类型画廊：十四类逐类走读」的类型化程度谱系对照实物：
同一个内核事件 `do_sys_openat2`（openat/openat2 系统调用的公共实现，签名在本机 BTF 实证、
上游 5.10+ 稳定）同时经 tracepoint、raw tracepoint（raw_tp）、kprobe、fentry、fexit
五类追踪程序观察——程序类型（program type）决定什么，在这一个目录里逐镜 diff。

## 演示目标

- **一个对象、五个程序、一个映射（map）、一个 loader**：`multi_observe.bpf.c` 把 5 个程序编进
  同一个 `.bpf.o`，共享一个 ARRAY 分镜 map（`lenses`，5 槽，key=镜编号）；`multi_observe.c`
  用 `bpf_program__attach()` 逐程序挂载，每秒打印 5 行分镜读数。
- **类型化程度的谱系**：typed 事件上下文（tracepoint）→ 无类型 args 强转（raw_tp）→
  pt_regs 手工取参（kprobe）→ BTF 直接类型化（fentry）→ 返回值语义（fexit 才看得到
  「这次调用办成了没有」）。
- **观察即教学**：本目录程序只观测不干预（全部返回 0，不改变系统调用行为）；
  加载与运行需要特权（见「环境前提」与「常见失败点」）。

## 文件说明

| 文件 | 说明 |
|---|---|
| `multi_observe.bpf.c` | 内核态 BPF 程序：5 个 SEC 各一镜（tracepoint/raw_tp/kprobe/fentry/fexit），共享分镜 map `lenses` |
| `multi_observe.c` | 用户态 loader：skeleton open/load → `bpf_program__attach()` 逐镜挂载 → 每秒读 map 打印 5 行分镜 → Ctrl-C 退出即卸载 |
| `Makefile` | 全链构建（vmlinux.h → `multi_observe.bpf.o` → `multi_observe.skel.h` → 可执行 `multi_observe`） |

## 环境前提

- 先运行 `bash ../00-env/check-env.sh` 自检：无 root 也可运行，按「编译级 / 加载级 / 运行级」
  三档给出结论。编译级通过即可完成本目录 `make`；加载需要 root 或 `CAP_BPF`；
  本机若 `unprivileged_bpf_disabled=2`，非 root 时 load 失败属预期。
- Linux kernel 5.10+（`do_sys_openat2` 独立函数与该签名自此稳定），开启
  `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成，BTF 缺失则无法编译）。
- tracefs（`/sys/kernel/tracing`）可读：tracepoint/raw_tp 的挂载点在 tracefs 事件目录下，需 root。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`。
- 安全说明：本目录程序均为只读观测，不干预系统调用行为；`Makefile` 本身只编译不加载。

## 编译与运行

```bash
cd book/examples/chapter-04/01-multi-observe
make
sudo ./multi_observe    # Ctrl-C 退出即卸载
```

触发：另开终端随便跑几条命令（`ls`、`cat` 等，凡是打开文件的调用都会走 `openat2`），
五镜计数同步增长；对比「同一秒、同一事件、五个计数一致」即本锚点要钉住的事实。

## 预期输出

（预期输出示例 / 运行级待验证）

```text
五副眼镜已挂上 openat2，随便跑几条命令（如 ls、cat）观察计数……
  [tracepoint] count=42     last_ret=0    name=/etc/ld.so.cache
  [raw_tp   ] count=42     last_ret=437  name=(本镜不读文件名)
  [kprobe   ] count=42     last_ret=0    name=/usr/bin/ls
  [fentry   ] count=42     last_ret=0    name=/etc/ld.so.cache
  [fexit    ] count=42     last_ret=3    name=(本镜不读文件名)
----
```

- 首行为挂载成功提示；之后每秒打印一次 5 行分镜（镜名/计数/最近返回值/最近文件名）+ 分隔线。
- 五行 count 应一致（同一事件）；`last_ret` 同字段不同语义——tracepoint/kprobe/fentry 三镜不写（恒 0），
  raw_tp 镜存的是 `ctx[1]` 原始值（syscall 类为系统调用号，openat2=437，供对照），只有 fexit 镜
  存真正的返回值（成功为 fd 如 3，失败为负错误码）；`name` 列只有读文件名的三镜有值。

## 代码讲解要点

五镜对照（同表口径见正文「三、类型画廊」各 card）：

| 镜（key） | SEC | ctx 形状 | 能拿到什么 |
|---|---|---|---|
| tracepoint（0） | `tracepoint/syscalls/sys_enter_openat2` | `struct trace_event_raw_sys_enter *`（typed 事件布局） | 类型化 `args[]` 数组，`args[1]` 即 filename；事件字段有名字、有类型 |
| raw_tp（1） | `raw_tp/sys_enter_openat2` | `__u64 *`（原始参数数组，无类型） | 只有参数槽位：syscall 类形状为 `(regs, id)`（以内核源码 `kernel/trace/trace_syscalls.c` 为准）；本镜不拆字段，只计数并存 `ctx[1]` 对照 |
| kprobe（2） | `kprobe/do_sys_openat2` | `pt_regs *`（宏内部），`BPF_KPROBE` 按 C 原型解包 | 任意内核函数入口参数（寄存器解包，x86 展开、以所用架构为准）；不依赖事件在 tracefs 注册 |
| fentry（3） | `fentry/do_sys_openat2` | BTF 直接类型化参数（无 pt_regs 中转） | 原型即函数原型，参数名与类型都来自 BTF；类型精确但**不豁免 probe 界限**——filename 仍是 `__user` 指针 |
| fexit（4） | `fexit/do_sys_openat2` | 同 fentry 参数 + 尾随 `int ret` | 五镜中唯一能直接读到返回值（fd 或负错误码）的一镜 |

- **`bpf_prog_type` 与 `bpf_attach_type` 的关系**：SEC 前缀同时决定两者——libbpf 按 SEC 推导
  程序类型（`TRACEPOINT`/`RAW_TRACEPOINT`/`KPROBE`/`TRACING`）与挂载类型，`bpf_attach_type`
  是「同一程序类型下按挂载点细分」的枚举（如 `TRACING` 之下 fentry/fexit/LSM 各有 attach 类型）；
  本 loader 用通用 `bpf_program__attach()`，逐程序的挂载路径差异全部交给 libbpf 按 SEC 解析，
  正面演示「类型决定挂载方式」。
- **共享分镜 map 选型**：`BPF_MAP_TYPE_ARRAY`——key 恒定 5 个、无删除、O(1) 定位，
  教学聚焦类型差异而非 map 选型；并发计数用 `__sync_fetch_and_add` 原子递增。
- **helper（辅助函数）选型**：`bpf_probe_read_user_str`——filename 是 `__user` 指针，
  任何镜下都不得直接解引用（第 3 章失败画廊 card ⑦ 的界限）；长度截断安全，读入 map 值。
- **失败分支必检**：`bpf_map_lookup_elem` 返回 NULL 即退出（第 3 章 card ①）；
  raw 镜不读文件名、fexit 镜不读文件名——各镜只取自己类型下「拿得到」的信息，本身就是教学点。
- **生命周期**：Ctrl-C → `bpf_link__destroy` 逐 link 卸载 → skeleton destroy（第 2 章生命周期回指）。

## 常见失败点

| 现象 | 原因 | 处理 |
|---|---|---|
| `load 失败（无特权环境的预期失败：Operation not permitted）` | 非 root 且 `unprivileged_bpf_disabled=2`，open/load 阶段即报 EPERM | 加载级实验用 `sudo`；无特权环境只做编译级验证（`make` 通过即本目录验收口径） |
| `attach fentry 失败`（libbpf 日志报 ENOENT/EINVAL） | fentry/fexit 需目标函数存在于本机 BTF：ENOENT=函数不存在（版本内联），EINVAL=签名不匹配 | 用 `bpftool btf dump file /sys/kernel/btf/vmlinux format c \| grep do_sys_openat2` 核对本机签名；签名漂移本身是教学点（正文 2.4） |
| `attach kprobe 失败` | 目标函数不存在（内核过老或被内联） | 同上核对 BTF；kprobe 挂点选择随内核版本演进 |
| `attach tracepoint 失败` | `syscalls/sys_enter_openat2` 事件不存在（内核无 openat2）或 tracefs 不可读 | 确认内核 5.10+ 且 root；`ls /sys/kernel/tracing/events/syscalls/` 核对事件目录 |
| `make` 报 `vmlinux.h` 生成失败 | 内核缺 BTF（`CONFIG_DEBUG_INFO_BTF` 未启用） | 参见附录 C 发行版 BTF 构建指南 |

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，
  全链生成（`multi_observe.bpf.o` → `multi_observe.skel.h` → 可执行 `multi_observe`）。
  编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在
  clang 18 下的已知现象），与示例源码无关。
- **本章补充（C6 口径）**：全链锚点验证状态为「编译级（含 gcc 链接）已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：tracepoint 与 tracefs（`Documentation/trace/`）、
  `Documentation/bpf/` 下 tracing 类程序文档。
- 内核源码 `kernel/trace/trace_syscalls.c`：syscall 类 raw_tp 原始参数形状 `(regs, id)` 的依据。
- libbpf 文档：`bpf_program__attach()` 通用挂载、SEC 命名与自动 attach 规则；
  `<bpf/bpf_tracing.h>`：`BPF_PROG`/`BPF_KPROBE` 宏。
- `bpftool` 手册页（BTF dump 与 skeleton 生成）。

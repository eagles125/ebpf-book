# 04-stack-uninit：栈变量未初始化使用（读未初始化的栈槽）

失败画廊 card ④——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.4 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示栈初始化跟踪类拒绝：`verdict` 仅在 `if (v)` 分支内赋值，`v == NULL` 的路径上
`if (verdict > 10)` 读到的是从未写过的栈槽——加载期预期被拒；声明处补一个
`= 0` 即修复。

## bad 与 fixed 差异

仅一处语义差异：fixed 在声明处补了 `= 0`，其余（stats 映射（map）定义、SEC、license、
赋值与判定的全部语句）完全一致；函数名按画廊约定固定为 `probe_bad` / `probe_fixed`。

bad 声明行上的 `volatile` **不是修复、也不是语义错误的一部分**：它是编译级可观测性
装置——未初始化读在 C 里是未定义行为，clang `-O2` 会把「分支外使用未初始化值」按
undef 折叠（实测：折叠后产物里没有那条未初始化读、整个程序反而变成验证器可放行的
形态）；`volatile` 让每次读写都落到真实栈槽，offending 指令（未初始化栈槽读取）才
得以保留。fixed 侧同样带 `volatile`，使 diff 只剩 `= 0` 这一处（与画廊 03 的
防折叠处理同一思路）。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：只差一个 = 0
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `volatile __u64 verdict;` 仅在 `if (v)` 分支赋值，分支外 `if (verdict > 10)` 直接读 | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | `volatile __u64 verdict = 0;`——所有路径上都有确定值 | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
invalid read from stack R1 off -16+0 size 8
```

关键行指认「从栈上读了一个未初始化的槽」——寄存器号与偏移随产物代码生成与内核
版本而异（本对 bad 产物中该读的目标寄存器为 R1、栈槽偏移 -16，详细日志等价形式
作 `R1 !read_ok`；教学基准亦常写作 `R2 !read_ok`），以「invalid read from
stack / !read_ok」这一稳定短语为准。

## 为什么拒绝

验证器（verifier）对 BPF 程序的 512 字节栈逐槽跟踪初始化状态：每个槽只允许在
「先写入过」的前提下读取（栈初始化跟踪，见正文 2.6）。本例中 verdict 的栈槽只在
`if (v)` 分支内被写入；`v` 为 NULL 的那条路径直接落到 `if (verdict > 10)`——
这条路径上槽从未被写，读取即被拒绝。

拒绝的理由是安全而非洁癖：内核栈在使用之间不清零，未初始化的槽里可能残留
**前一次系统调用留下的进程敏感数据**（地址、凭据片段等）。BPF 程序读出后若回传
用户态，就是一次内核信息泄露。验证器按所有可能路径的最坏情况证明——「NULL 大概率
不出现」不是放行理由，与画廊 01-map-null 的判空逻辑同理：只不过那里跟踪的是
寄存器的联合类型（map_value_or_null），这里跟踪的是栈槽的写入记录。

## 修复原则

- **声明即初始化**：`__u64 verdict = 0;`——所有路径上都有确定值，初始化值按业务
  语义选（这里 0 表示「无计数信号」）。
- 或把使用点包进赋值分支：只有 `v` 非空才消费 `verdict`（`if (v) { ... }` 内
  使用）——验证器要求的是「读之前这条路径上写过」，两条路径都满足即可。
- C 层面这是未定义行为（UB），编译器有权按 undef 折叠：**不要指望「读了也白读」
  的侥幸**——本对 bad 若不加 volatile，产物里连这条读都没有（见「bad 与 fixed
  差异」的可观测性说明）。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）；bad 产物
  经 `llvm-objdump -S` 核验，未初始化栈槽读取指令真实存在（`r1 = *(u64 *)(r10 - 0x10)`，
  该槽仅在 `if (v)` 分支内被写入）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（栈初始化跟踪与
  `!read_ok` / `invalid read from stack` 相关措辞）。
- `bpf_helpers.h`（libbpf）：`bpf_map_lookup_elem` 声明与映射定义宏。

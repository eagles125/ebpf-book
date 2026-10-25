# 07-kernel-ptr：ctx 标量字段强转指针直接解引用（invalid mem access 'scalar'）

失败画廊 card ⑦——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.7 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示寄存器类型层面的拒绝：tracepoint 上下文里 `ctx->args[1]` 是「指针的数值」
（标量），C 强转成 `char *` 就直接解引用 `fn[0]`——加载期预期被拒；改用
`bpf_probe_read_user_str()` 把数据受控读进栈缓冲再判读即修复。

## bad 与 fixed 差异

仅一处语义差异：bad 把 `ctx->args[1]` 强转 `char *` 后直接 `fn[0]` 解引用，
fixed 换成 `bpf_probe_read_user_str()` 受控读取到 64 字节栈缓冲、在「读取成功」
分支内判读；其余（SEC、返回值 1/0、license）完全一致；函数名按画廊约定固定为
`probe_bad` / `probe_fixed`。本对为 tracepoint 程序类型（program type，syscalls
跟踪点，无 XDP 挂载风险），骨架按画廊公共骨架约定不带 uapi 常量块与映射。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：只差「强转解引用 → helper 受控读取」
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `char *fn = (char *)ctx->args[1];` 后直接 `fn[0]` | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | `bpf_probe_read_user_str(fn, sizeof(fn), ...)` 读入栈缓冲后再判读 | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
R1 invalid mem access 'scalar'
```

关键行指认「对一个标量（scalar）类型的寄存器做了内存访问」——寄存器号随产物
代码生成（本对 bad 产物中解引用的目标寄存器为 R1，等价形态亦可作无前缀的
`invalid mem access 'scalar'`）；指认点就是 `fn[0]` 那条字节读取指令（其地址
寄存器由 `ctx->args[1]` 的加载而来，`llvm-objdump -S` 可核验）；日志前文会
给出该寄存器当前的类型与取值域（格式随版本略有差异）。

## 为什么拒绝

`struct trace_event_raw_sys_enter` 的 `args[]` 是 `unsigned long` 数组：从
tracepoint 元数据看，`args[1]`（openat 的 filename 参数）**在数值上**是用户态
地址，但在验证器（verifier）的类型系统里，从 ctx 加载出来的寄存器只是
标量（SCALAR_VALUE）——「一个数」，不是任何可解引用的指针类型。C 的强制
转换能骗过编译器，骗不过验证器：类型随数据流走，寄存器没被登记为指针，
解引用它就没有任何「访问安全」的证明可言。

放行意味着什么：BPF 程序可以把**任意 64 位数**当成地址去读——内核空间任意
位置、用户空间任意位置——一次 `fn[0]` 就是一次不受界限检查的内核内存读。
这正是第 2 章预告⑦「ctx 申报访问 vs 任意内核指针」的正式展开：ctx 自身按
申报布局访问（`ctx->args[1]` 这一步合法），而 ctx 字段里存着的地址属于
「任意地址」，必须走 probe 系 helper——`bpf_probe_read_user_str()` 在 helper
内部把源地址限定在用户空间、按缓冲区大小受控拷贝，越界/缺页返回错误值而非
崩溃（正文 2.3 寄存器类型表、2.7 helper 约束的落点）。

## 修复原则

- **任意内核/用户地址一律走 probe 系辅助函数（helper）**：`bpf_probe_read_user_str` /
  `bpf_probe_read_user`（用户空间）、`bpf_probe_read_kernel*`（内核空间），
  目标是验证器逐槽跟踪的栈缓冲；绝不对标量强转出的指针解引用。
- **判读在「读取成功」分支内**：helper 返回值 > 0 才消费缓冲区内容（本例
  `n > 0 && fn[0] == '/'`）——失败路径上的缓冲区内容不可信。
- **版本演进点保守对待**：`bpf_probe_read_user_str` 为 5.5+（probe 系按访问
  空间拆分）；更旧内核的 `bpf_probe_read_str` 读取空间随架构配置而异，以
  所用内核文档为准。
- 同型错误：把 map value 里存的地址、报文里读出的指针值拿来直接解引用——
  凡「来源不是验证器登记的指针类型」的解引用，全部是这一类拒绝。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）；bad 产物
  经 `llvm-objdump -S` 核验，对强转指针的字节读取指令真实存在
  （`r1 = *(u64 *)(r1 + 0x18)` 取 args[1] 后 `r1 = *(u8 *)(r1 + 0x0)` 解引用）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（寄存器类型与
  `invalid mem access 'scalar'` 措辞）。
- `include/trace/events/syscalls.h`：`trace_event_raw_sys_enter` 布局
  （`args[]` 为 `unsigned long[6]`）。
- `bpf_helpers.h`（libbpf）：`bpf_probe_read_user_str` 声明（5.5+，保守注明）。

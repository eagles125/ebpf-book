# 06-helper-misuse：helper 白名单越界（XDP 调用 tracing 类专用 helper）

失败画廊 card ⑥——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.6 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示辅助函数（helper）权限类拒绝：XDP 程序调用 `bpf_get_current_pid_tgid()`——这个
辅助函数只对 tracing/kprobe 等进程上下文里的程序类型（program type）开放，不在
XDP 白名单内——加载期预期被拒；改用 XDP 白名单内的等价能力
`bpf_get_smp_processor_id()` 即修复。

## bad 与 fixed 差异

仅一处语义差异：bad 调用 `bpf_get_current_pid_tgid()`（helper id 14，tracing 类
专用），fixed 换成 `bpf_get_smp_processor_id()`（helper id 8，XDP 白名单内），
对应变量名 `pid` → `cpu`；其余（判定结构、返回动作、SEC、license）完全一致；
函数名按画廊约定固定为 `probe_bad` / `probe_fixed`。本对不使用映射，也不读报文，
`ctx` 参数按骨架保留。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：只差一个 helper 换名
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `bpf_get_current_pid_tgid()`（id 14）出现在 XDP 程序里 | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | `bpf_get_smp_processor_id()`（id 8）——XDP 白名单内 | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
unknown func bpf_get_current_pid_tgid#14
```

关键行指认「id 14 这个辅助函数不允许用于本程序类型」——指认点就是那条
`call 0xe` 指令（`llvm-objdump -S` 可核验）。5.10+ 内核源码中，白名单 miss 的
唯一输出形态就是 `unknown func <名字>#<id>`：验证器按程序类型查不到该辅助函数
的函数原型，即按「未知函数」报错。**初学者最容易在这里误判为「helper 不存在」**
——`bpf_get_current_pid_tgid` 当然存在，它只是不在 XDP 的白名单里；「unknown」
修饰的是「对本程序类型不可用」，不是「内核没有这个函数」。排查这类拒绝时先查
辅助函数的程序类型适用面（`include/uapi/linux/bpf.h` 注释或
`bpftool feature probe`），不要先怀疑内核版本。早期教学材料流传的
`program of this type cannot use helper` / `func ... not allowed for program
type ...` 措辞已不存在于内核源码，本 README 不采用。

## 为什么拒绝

helper 不是全局可用的万花筒：内核按程序类型（program type）登记各自的
helper 白名单（每种类型一套 verifier_ops，见正文 2.7）。白名单不是随意的
官僚关卡，而是跟着**执行上下文**走的：

- `bpf_get_current_pid_tgid()` 读的是「当前进程」的 pid/tgid。tracing/kprobe
  类程序挂在进程上下文的事件上，「当前进程」有意义；XDP 程序跑在网卡收包
  软中断里，此刻可能在任意进程的地址空间切换间隙执行——「当前进程」要么
  无意义、要么指向毫无关系的进程。放行它，程序拿到的 pid 与报文毫无因果，
  还等于把系统内任意进程的 pid 泄露给网卡路径上的程序（信息泄露面）。
- 白名单在加载期静态核对：验证器扫到 `call` 指令，查出 helper id，比对
  本程序类型的可用清单，不在即拒绝——不需要等运行时才发现「数据没意义」。

这回收第 2 章预告②：「helper 白名单加载时拒绝」——第 3 章把它落到具体
指令与日志上。

## 修复原则

- **先查适用面再选 helper**：`include/uapi/linux/bpf.h` 中每个 helper 的注释
  标注适用程序类型，或用 `bpftool feature probe` 查看本机清单；拿不准时
  换同程序类型白名单内的等价能力（本例：按 pid 聚合换不成，就按 CPU 聚合）。
- 「等价能力」是语义替换，不是绕过：验证器没有「豁免机制」，任何想通过
  直接改 helper id 绕白名单的做法都会在原型检查（参数类型不符）处再次被拒。
- 同型错误：在 XDP/TC 里调用 `bpf_probe_read_kernel`（tracing 专用，除非
  程序声明对应能力）、在没有锁能力的映射类型上用自旋锁辅助函数等——
  都是「程序类型 × helper 适用面」不匹配的同一类拒绝。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）；bad 产物
  经 `llvm-objdump -S` 核验，对 helper id 14 的 call 指令真实存在（`call 0xe`）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- `include/uapi/linux/bpf.h`：各 helper 的适用程序类型注释与 id 清单。
- Linux kernel documentation：`Documentation/bpf/verifier.rst`（helper 白名单
  与拒绝措辞）。
- `bpftool feature probe`：本机各程序类型的 helper 可用面实测途径。

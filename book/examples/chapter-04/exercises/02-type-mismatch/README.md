# 02-type-mismatch：类型错位修复——从「invalid bpf_context access」归位

《第 4 章 eBPF 程序类型与钩子点深入》「课后扩展任务」练习 2 的任务目录。
`broken.bpf.c` 是任务体（先读它，不是剧透）：一段报文解析逻辑被放进 kprobe
程序、ctx 却声明为 `xdp_md`——**编译通过但加载预期被拒**（1.4 教师视角：
三道类型关卡都不在编译期）。你的任务是诊断错位、把程序类型三元组归位。
`fixed.bpf.c` 是参考实现（剧透——先做后看）；本目录为 compile-only：`make`
产出 `broken.bpf.o` 与 `fixed.bpf.o` 两个对象、不加载——broken 的「被拒」
发生在加载期（验证器），编译通过是它设计的一部分。

## 任务

1. 编译 `broken.bpf.c`（本目录 `make` 即可，两个对象一起产出），确认
   **编译通过**；
2. 在满足权限的内核上加载 `broken.bpf.o`（运行级待验证——本目录编写环境未
   执行加载），把真实日志与本 README「预期输出示例」对照；
3. **诊断**：从日志第一处 ctx 访问拒绝入手，按「错位诊断三步」定位根因
   （三步是什么见分界线后；提示节先给信号面）；
4. **修复**：把程序类型三元组归位——自己写出修复版，再对照 `fixed.bpf.c`；
5. 复核：修复版预期加载成功，计数随 lo 上的流量增长（运行级待验证）。

验收标准：

- broken 编译通过、预期加载被拒（运行级待验证），诊断过程成文；
- 修复版编译通过，且**只动「程序类型三元组」**——解析与计数逻辑与 broken
  保持一致（逐行可 diff 的归位，不是重写）；
- 能说清「为什么 broken 编译能过、加载必拒」（1.4 教师视角三道关卡）。

## 提示

- 信号面：加载期日志的第一处拒绝是 **ctx 访问被拒**——关键行「invalid
  bpf_context access」形态（预期日志 / 运行级待验证，随内核版本略有差异）。
  这行在说：对 ctx 的某次字段访问没有通过**按程序类型登记的白名单**。先回
  第 3 章排错节把「验证器拒绝日志」的读法补上，再回本章排错节的判别流程
  （先问权限、再问挂载目标、最后读类型规则）——本题已是第三问。
- 日志拒的是「访问」这个动作——代码里对应哪几行？`ctx->data`/
  `ctx->data_end` 是什么形状的 ctx 才有的字段？
- SEC 前缀决定程序类型（2.3）：`kprobe/` 前缀把程序定成了哪一类？那一类的
  ctx 实际是什么形状（1.3 表 kprobe 行）？形参声明成 `struct xdp_md *` 在
  编译期意味着什么、在验证器眼里又是什么（1.4 教师视角第三道关卡）？
- 修复别忘了三元组第三项：返回值语义跟着类型走（1.2）——broken 的
  `return 0;` 在原类型里「被忽略」，归位后 0 是什么意思？（这道思考题的
  答案直接决定修复版的每个 return 写什么。）

## 环境前提与构建

- 依赖：clang（含 BPF 后端）、bpftool（`make` 自动由 `/sys/kernel/btf/vmlinux`
  生成 `vmlinux.h`）。环境自检与依赖版本见
  [`../../00-env/`](../../00-env/)（无 root 可跑）。
- 构建（本目录内直接执行）：

```bash
make            # 产出 broken.bpf.o 与 fixed.bpf.o——broken 编译通过是预期，不是意外
```

- `make` 只编译不加载（无 XDP 挂载风险）。**别急着打开 `fixed.bpf.c`**——
  它是分界线之后的参考实现；先诊断、先自己写修复版，卡住再对照。
- 加载级前提（届时需要）：root 或 CAP_BPF；修复版为 XDP 类型，实验只应挂
  veth 或 lo（章级 [`../../README.md`](../../README.md) 安全警告：切勿在不
  了解影响前挂到生产物理网卡）。

## 预期输出示例

（预期日志 / 运行级待验证；随内核版本略有差异）

broken 加载预期被拒——完整预期拒绝日志的教学示例：

```text
$ sudo bpftool prog load broken.bpf.o /sys/fs/bpf/ch04_ex2_broken
; void *data = (void *)(long)ctx->data;       /* 预期被拒：kprobe ctx 无 data 字段 */
invalid bpf_context access off=0 size=4
```

口径注（读这份示例前先看这里）：

- 教学口径把第一处拒绝定位于**第一处 ctx 访问**（`ctx->data`）——「invalid
  bpf_context access」是说这次访问没通过 kprobe 程序类型的 ctx 访问白名单
  （只放行 pt_regs 形状的字段）。
- `off=`/`size=` 的具体数值、`;` 源码回显行与外围报错行随所用内核与 bpftool
  版本而异；个别内核版本的 kprobe ctx 白名单若放行这对对齐窄读，第一处拒绝
  会落到其后对报文指针的解引用行（「invalid mem access」形态）——认的是
  「ctx 申报类型与程序类型错位」这一信号族与错位诊断三步，不认某一行具体
  措辞。
- 修复版预期加载成功；按锚点②的挂载形态挂 lo 并造流量后读映射：

```text
$ sudo bpftool prog load fixed.bpf.o /sys/fs/bpf/ch04_ex2_fixed
$ sudo bpftool net attach xdpgeneric pinned /sys/fs/bpf/ch04_ex2_fixed dev lo
$ sudo bpftool map dump name mismatch_hits
key: 00 00 00 00  value: 2a 00 00 00 00 00 00 00   # 解析计数随 lo 流量增长
Found 1 element
```

（数值随负载，以实测为准；挂载与清理（`bpftool net detach`、
`rm /sys/fs/bpf/ch04_ex2_*`）的完整命令链见锚点②
[`../../02-xdp-tc/`](../../02-xdp-tc/) 与 bpftool 手册页。）

---

## 参考实现思路

**错位诊断三步**（本章排错节判别流程第三问的展开）：

1. **看 SEC**：`SEC("kprobe/do_sys_openat2")`——前缀 `kprobe/` 把程序定型为
   KPROBE（2.3 前缀表）；可这段代码在做报文解析（`data`/`data_end` 两端
   指针＋以太网头边界检查），是网络任务的形状——「任务性质」与「程序类型」
   先对不上（1.4 决策树里报文解析落在「网络」枝）。
2. **看 ctx 类型**：形参声明 `struct xdp_md *`——XDP 的 ctx 布局。但类型
   三元组跟着**程序类型**走、不跟形参声明走（1.2/1.3 表 kprobe 行）：KPROBE
   的 ctx 实际是 `pt_regs *`。编译期「对某个结构体的正常访问」clang 无从
   比对（1.4 教师视角第三道关卡），错位因此一路瞒到加载期。
3. **看 is_valid_access 白名单**：加载期验证器按 KPROBE 的
   `is_valid_access` 白名单校验每一笔 ctx 访问——只放行 pt_regs 形状的字段；
   `xdp_md` 形状的 `data`/`data_end` 访问撞墙，即预期日志的「invalid
   bpf_context access」（第 3 章排错节「信号 → 根因」映射的同族信号）。

**归位**（`fixed.bpf.c`）——三元组三项一起换对：

| 三元组项 | broken（错位） | fixed（归位） |
|---|---|---|
| 程序类型/SEC | `SEC("kprobe/...")`（KPROBE） | `SEC("xdp")`（XDP）——报文解析回到 1.4「网络」枝第一叶 |
| ctx 布局 | `struct xdp_md *` 挂在 KPROBE 下（字段访问不在白名单内） | 同为 `struct xdp_md *`，落在 XDP 下——字段访问从此在白名单内 |
| 返回值语义 | `return 0;`（KPROBE：被忽略） | `return XDP_PASS;`（XDP：放行）——**不能**把 0 原样搬过来：`enum xdp_action` 里 0 是 `XDP_ABORTED`，每包中止 |

- 解析与计数逻辑（取两端指针、边界检查、原子递增）一字未动——这正是「类型
  错位」的教学点：错的不是逻辑，是「这段逻辑被安进了哪个类型」；归位是可
  diff 的（对比两个文件，差异只有 SEC 串与 return）。
- `XDP_PASS` 来自 `enum xdp_action`（在 BTF，1.3 表 XDP 行）——返回值语义
  跟着类型走（1.2 第三项）的最短实证：同一个 `return 0;`，KPROBE 里无害、
  XDP 里每包中止。修复只换三元组、不重写逻辑——「选对类型」本身是低成本的，
  前提是诊断路径熟。
- 同构参照：修复版与锚点②的
  [`../../02-xdp-tc/xdp_count.bpf.c`](../../02-xdp-tc/) 同款（`SEC("xdp")` +
  `xdp_md` 两端指针＋边界检查＋恒 `XDP_PASS`）——报文解析逻辑在 XDP 下的
  标准形状。

## 验证状态

- **编译验证**：broken 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、
  bpftool 7.7、libbpf 1.3）`make` 通过，产出 `broken.bpf.o` 与
  `fixed.bpf.o`；broken 编译通过是预期如此——它的被拒发生在加载期，不在
  编译期。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行
  加载与运行；「预期输出」为教学示例（口径注见「预期输出示例」）。broken
  『预期被拒』与 fixed『预期加载成功』均待特权环境验证。

## 参考来源

- Linux kernel documentation：BPF 验证器（`Documentation/bpf/verifier.rst`，
  ctx 访问校验措辞的上游基准）。
- 内核源码：`kernel/trace/trace_kprobe.c`（kprobe 的 ctx 访问白名单实现，
  以所用内核为准）、`include/uapi/linux/bpf.h`（`xdp_md` 布局与
  `enum xdp_action`）。
- 正文对位：1.2 三元组与返回值语义、1.4 决策树与教师视角「三道类型关卡都不
  在编译期」、2.3 SEC 前缀解析、本章排错节判别流程与速查表；第 3 章排错节
  （验证器日志精读）。
- 画廊/锚点对照：[`../../02-xdp-tc/`](../../02-xdp-tc/)（XDP 侧同构解析）、
  [`../../03-type-gallery/01-kretprobe/`](../../03-type-gallery/01-kretprobe/)
  （kprobe 家族的正确用法对照）。

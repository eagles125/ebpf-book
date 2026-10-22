# 第 3 章「失败画廊」总览：七类典型拒绝的 bad/fixed 对照

本目录是《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」一节的配套
示例组：七个子目录，每对一个 `bad.bpf.c`（预期被拒）与一个 `fixed.bpf.c`
（最小修复版），bad 与 fixed **仅差一处语义错误**，函数名固定为
`probe_bad` / `probe_fixed`。

**安全警告：本画廊只编译不加载**——所有目录为 compile-only 构建，`make` 只产出
`bad.bpf.o` / `fixed.bpf.o`，不生成 skeleton、不挂载；bad 程序的「拒绝」发生在
加载期（verifier），编译脚本头部已注明「本脚本只编译不加载（无 XDP 挂载风险）」。

## 学习路径

建议按 01→07 顺序逐对走读，每对三步：

1. `diff bad.bpf.c fixed.bpf.c`——亲眼确认只差一处；
2. 读该对 README 的「预期 verifier 日志关键行」与「为什么拒绝」——把日志行、
   语义错误、安全原理三者对上号；
3. 读「修复原则」——把该类的修复模式记成一句话。

七对覆盖验证器（verifier）拒绝的七个典型面：映射（map）值判空（①）→ 报文边界（②）→
指针类型（③）→ 栈初始化（④）→ 循环可终止性（⑤）→ 辅助函数（helper）白名单（⑥）→
任意地址访问（⑦）。建议先完成正面锚点 [`../01-pkt-parse/`](../01-pkt-parse/)
与 [`../00-env/`](../00-env/) 的 `check-env.sh` 环境自检，再进入画廊。

## 七对总览

| 目录 | 失败信号 | 预期日志关键行 | 修复原则一句话 |
|---|---|---|---|
| [01-map-null](./01-map-null/) | `bpf_map_lookup_elem` 返回值未检查即 `*v += 1`（map_value_or_null 直接解引用） | `invalid mem access 'map_value_or_null'` | lookup 之后、解引用之前先判空 `if (!v) return XDP_PASS;` |
| [02-pkt-bounds](./02-pkt-bounds/) | eth/ip 指针推进后无任何 `data_end` 检查就读字段（首个越界读在 `eth->h_proto`） | `R2 offset is outside of the packet`（或 `invalid packet access`） | 每层解引用前先 `(void *)(指针 + 1) > data_end` 界检查 |
| [03-ptr-math](./03-ptr-math/) | 对 pkt_end 指针做算术且结果可观测（防 clang 死代码消除）：`data_end += 4` 后参与 `data > data_end` 比较 | `R2 pointer arithmetic on pkt_end PROHIBITED` | 去掉对尾界的算术，偏移放在 packet 指针上并立刻界检查收口 |
| [04-stack-uninit](./04-stack-uninit/) | `volatile __u64 verdict;` 仅在 `if (v)` 分支赋值，分支外条件使用（volatile 使未初始化栈槽读取落到真实指令——裸形式会被 clang -O2 UB 折叠消除，实测教训） | `R2 !read_ok`（或 `invalid read from stack off -16+0 size 8`；特权 CAP_PERFMON 加载放行——豁免说明见该对 README） | 声明即初始化 `volatile __u64 verdict = 0;`（两侧同带 volatile，diff 仅剩 `= 0` 一处） |
| [05-loop](./05-loop/) | 循环上界 `n` 直接取自报文且无掩码（不可证明终止）；累加用 `sum ^= i`（线性 `sum += i` 会被 -indvars 折叠成闭式、循环整条消除，实测教训） | `infinite loop detected at insn ...`（特权路径主形态；`back-edge from insn ... to ...` 为非特权路径形态） | `n & 0xF` 掩码定界（上界变常数 16）；两侧同用 `sum ^= i`，diff 仅剩掩码一处 |
| [06-helper-misuse](./06-helper-misuse/) | XDP 程序调用 `bpf_get_current_pid_tgid()`（tracing 类专用，不在 XDP 白名单） | `unknown func bpf_get_current_pid_tgid#14`（白名单 miss 的真实形态） | 改用 XDP 白名单内的等价能力 `bpf_get_smp_processor_id()` |
| [07-kernel-ptr](./07-kernel-ptr/) | tracepoint 程序把 ctx 标量字段强转指针直接解引用：`char *fn = (char *)ctx->args[1]; fn[0]` | `invalid mem access 'scalar'` | `bpf_probe_read_user_str()` 读入栈缓冲后再判读（5.5+） |

预期日志关键行以 5.10+ 稳定措辞为基准，**全部为「预期输出示例 / 运行级待验证」**
——日志行随内核版本略有差异（有界循环 5.3、`bpf_probe_read_user_str` 5.5 等
演进点已在各对 README 保守注明），以特权环境实测为准。注意验证器严格度随
特权档位变化：04-stack-uninit 的拒绝仅在允许非特权 BPF 的配置成立（特权
CAP_PERFMON 加载放行未初始化栈读），详见该对 README 的特权豁免说明。

## 验证口径：bad 也必须编译通过

画廊七对不是语法错误示例，是**语义被拒**示例：bad 与 fixed 在 clang 层面
**全部编译通过**，差异只在加载期被验证器看到——「编译成功 ≠ 能加载」正是
本章的教学主线。因此本画廊的验收口径是：

- 编译级（硬性）：每个目录 `make` 必须通过，产出两个 `.bpf.o`；bad 的验收
  就是**编译成功**——若 bad 连编译都过不了，说明写成了语法错误，不是画廊要
  展示的语义拒绝。
- 加载级（待验证）：编写环境为非 root（`unprivileged_bpf_disabled=2`），
  未执行加载与运行；『预期被拒 / 预期放行』与上方日志关键行均为教学示例，
  待特权环境验证（各对 README「验证状态」段的 C6 双状态约定）。

统一编译命令（仓库根执行）：

```bash
for d in book/examples/chapter-03/02-fail-gallery/0*/; do make -C "$d"; done
```

构建依赖与工具链版本见章级 [`../README.md`](../README.md) 与
[`../00-env/`](../00-env/)。

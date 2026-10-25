# 01-fix-challenge：修复挑战——三处组合错误的独立坏程序

《第 3 章 验证器与安全保障》「课后扩展任务」练习 1 的任务目录。`broken.bpf.c`
是给学生的任务体：一个含**三处组合错误**的 XDP 程序（不重复失败画廊任何单一
case，需综合运用本章知识定位）；`fixed.bpf.c` 是参考实现（剧透——先做后看）。
本目录为 compile-only：`make` 只产出 `broken.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——broken 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务

`broken.bpf.c` 的 `challenge` 程序在 clang 层面完全合法（编译通过），但加载期
预期被验证器拒绝——它埋了三处语义错误：报文边界缺失、未检查 map（映射）查找
（lookup）、未初始化栈变量。你的任务：

1. **只读本 README 分界线之前的内容**，自行修复 `broken.bpf.c`（建议复制一份
   改名练习，保留原文件做对照）；
2. 每修一处就重新编译并（在满足权限的环境）尝试加载，**读每一次的 verifier
   日志**，确认被拒信号随修复推进而变化；
3. 把三处错误各自的「日志信号 → 根因 → 修复」用一句话说清楚（信号认法见
   排错节第①组速查表）。

验收标准：

- 修复后的程序 `make` 编译通过；
- 能说明三处错误各自的 verifier 拒绝理由（对应哪条机制、日志关键行长什么样）；
- 满足权限（root / `CAP_BPF` 一类档位，XDP 挂载另需 `CAP_NET_ADMIN`）的内核上，
  修复后的程序 `bpftool prog load` 加载成功（运行级待验证——本目录编写环境未
  执行加载，见「验证状态」）。

## 提示

- 三处错误分属三个机制面，信号族各不相同：**报文边界**（比较建立关系，正文
  2.5）、**map_value_or_null**（查找返回值的两态合一，正文 2.3）、**栈初始化
  跟踪**（正文 2.6）。修复前先回画廊走读对应三对——3.2／3.1／3.4——把每对的
  「预期 verifier 日志关键行」与「为什么拒绝」读进去，再回来定位。
- 方法：**从日志第一处报错入手，修一处再看下一处**。verifier 按指令顺序探索，
  只报第一处「不可证明」——一次把三处全改完再加载，你只看得见最后剩下的那个
  信号（如果还有）；逐个修才能把三个信号都亲眼看一遍。
- 注意其中一处的拒绝有**特权档位前提**（严格度随特权档位变化，见画廊 3.4 的
  特权豁免说明）：特权加载下你看到的信号形态可能与非特权配置不同。
- 日志关键行的查法：《第 3 章》「常见问题与排错」第①组速查表按「信号 → 根因
  类别」组织，不背文本。

## 环境前提与构建

- 依赖：clang（含 BPF 后端）、bpftool（`make` 自动由 `/sys/kernel/btf/vmlinux`
  生成 `vmlinux.h`）。环境自检与依赖版本见
  [`../../00-env/`](../../00-env/)（无 root 可跑）。
- 构建（本目录内直接执行）：

```bash
make            # 产出 broken.bpf.o 与 fixed.bpf.o——broken 也必须编译通过
```

- `make` 只编译不加载（无 XDP 挂载风险）。**别急着打开 `fixed.bpf.c`**——它是
  分界线之后的参考实现；做完（或确实卡住）再看。

## 预期输出示例

（预期输出示例 / 运行级待验证；以下为修复完成后在满足权限的内核上加载参考实现
的预期形态，字段随 bpftool／内核版本略有差异）

```text
$ bpftool prog load fixed.bpf.o /sys/fs/bpf/ch03_challenge
$ bpftool prog show pinned /sys/fs/bpf/ch03_challenge
<id>: xdp  name challenge  tag <tag>  gpl
	loaded_at <时间>  uid 0
	xlated <N> bytes  jited <M> bytes  memlock <K> bytes
```

- 加载成功后 `bpftool prog show` 应能看到一行 `xdp` 类型的程序、名字
  `challenge`、license `gpl`——验证「通过」路径的实物。
- 修复**过程中**的各次加载失败属练习本体（每一步的拒绝日志就是练习材料），
  本节只给终态（加载成功）的预期输出。
- 清理：`rm /sys/fs/bpf/ch03_challenge`（解除 pin 后程序随引用释放）。

---

## 参考实现思路

三处错误与修复、画廊对照、原理小节的对位：

| 错误 | 根因 | 修复 | 画廊对照 | 原理小节 |
|---|---|---|---|---|
| (1) 报文边界缺失 | `eth`／`ip` 指针推进后没有与 `data_end` 的比较就解引用 | 两层各补一行 `if ((void *)(指针 + 1) > data_end) return XDP_PASS;` | 3.2（02-pkt-bounds） | 2.5 报文指针比较建立关系 |
| (2) 未检查 lookup | `bpf_map_lookup_elem` 返回值可能为 NULL（map_value_or_null 两态合一） | `if (!v) return XDP_PASS;` 判空分支 | 3.1（01-map-null） | 2.3 寄存器类型族 |
| (3) 未初始化栈变量 | `verdict` 仅在 `if (v)` 分支赋值，v == NULL 路径上分支外读取 | 声明即初始化（参考实现：判空后 `__u64 verdict = *v;`，每条路径都有确定值） | 3.4（04-stack-uninit） | 2.6 栈槽初始化跟踪 |

**连带危险（本题与画廊单一 case 的关键差别）**：`*v = verdict + 1` 写在
`if (v)` 判空**之外**。v == NULL 的路径上，这一行既触发错误 (3)（读未初始化的
verdict），又是对 `map_value_or_null` 的**写**——验证器对读与写一视同仁，
`invalid mem access 'map_value_or_null'` 同样拦截。所以只给 `verdict` 补初值、
不把 `*v` 的使用收进判空之后，程序仍会被拒；参考实现把 `*v` 的全部使用
（读出 `verdict`、写回计数）都放在 `if (!v)` 分支之后，一处判空同时收窄两条
语句的类型。

**修复顺序与日志推进**（与方法提示互证）：按指令顺序，verifier 先撞上 (1)
——报文边界信号（`R1 offset is outside of the packet` 一类，寄存器号随产物
而异）；修掉后撞上 (3)——栈初始化信号（`invalid read from stack off ...`
一类）；修掉后才是 (2) 的连带面——`invalid mem access 'map_value_or_null'`。
三步日志正好把三类保证（内存安全、初始化跟踪、判空收窄）各过一遍。

**档位敏感提示**：错误 (3) 的拒绝仅在允许非特权 BPF 的配置成立（特权
CAP_PERFMON 档加载放行未初始化栈读，见画廊 3.4 README 特权豁免说明）。特权
环境下修复中段可能看不到栈初始化信号、直接落到 (2) 的 map_value_or_null——
这不是修复错了，是验证器严格度随特权档位变化；教学与代码审计仍按「必须
初始化」对待。

**可观测性装置说明**：任务体 `broken.bpf.c` 的 `verdict` 声明带 `volatile`——
不是错误、也不是修复的一部分，而是编译级装置（画廊 03／04 同一先例）：未初始
化读在 C 里是未定义行为，clang `-O2` 会按 undef 折叠——实测裸形式下 verdict
被寄存器分配、未初始化栈槽读整条消失，产物里只剩寄存器形态的信号；`volatile`
让每次读写都落到真实栈槽，offending 指令（未初始化栈槽读取）得以保留。参考
实现 `fixed.bpf.c` 的 `verdict` 每条路径都有确定值，无需 `volatile`。

## 验证状态

- 编译级：broken 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、
  bpftool 7.7、libbpf 1.3）`make` 通过。broken 按 C6 本章补充条为双状态：
  **编译级已通过（预期如此——拒绝发生在加载期）；加载级『预期被拒』待特权
  环境验证**。broken 产物经 `llvm-objdump -S` 核验，三处 offending 指令真实
  存在：无界报文读 `r1 = *(u8 *)(r1 + 0xe)`（错误 1）；未初始化栈槽读
  `r1 = *(u64 *)(r10 - 0x10)`（错误 3——该槽仅在 `if (v)` 分支内被写入）；
  对 map_value_or_null 的写 `*(u64 *)(r0 + 0x0) = r1`（错误 2 连带面）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与
  运行。broken『预期被拒』（首处报文边界拒绝与档位无关；特权档下未初始化栈读
  被豁免，信号形态见「参考实现思路」档位敏感提示）、fixed『预期加载成功』
  均待特权环境验证，上方预期输出为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（verifier 拒绝
  信息与日志措辞的上游基准）。
- 本画廊对照对：[`../../02-fail-gallery/01-map-null/`](../../02-fail-gallery/01-map-null/)、
  [`../../02-fail-gallery/02-pkt-bounds/`](../../02-fail-gallery/02-pkt-bounds/)、
  [`../../02-fail-gallery/04-stack-uninit/`](../../02-fail-gallery/04-stack-uninit/)。
- `bpf_helpers.h`（libbpf）：`bpf_map_lookup_elem` 声明与映射定义宏。
- `bpftool` 手册页（`prog load` / `prog show`）。

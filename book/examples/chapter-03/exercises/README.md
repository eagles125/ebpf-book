# 第 3 章「课后扩展任务」练习总览：修复挑战与日志诊断

本目录是《第 3 章 验证器与安全保障》「课后扩展任务」一节的配套练习组：两个
子目录各含一项完整参考实现与防剧透 README——练习 1 给你一个含三处组合错误的
坏程序（修复挑战），练习 2 给你一条预期 verifier 拒绝日志（日志诊断，从日志
反推程序）。任务表如下（与正文「课后扩展任务」一节逐字一致）：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 综合修复 | 修复挑战：`broken.bpf.c` 含报文边界缺失、未检查 lookup、未初始化栈变量三处组合错误——修到编译通过，并在满足权限的内核上能加载成功 | `book/examples/chapter-03/exercises/01-fix-challenge/` |
| 2 | 日志诊断 | 按预期 verifier 日志片段「R2 min value is negative, either use unsigned or 'var &= const'」反推根因，写程序复现并逐行解读日志 | `book/examples/chapter-03/exercises/02-log-diagnosis/` |

**安全警告：本目录只编译不加载**——两个练习目录均为 compile-only 构建，`make`
只产出 `.bpf.o`，不生成 skeleton、不挂载；预期被拒程序（broken／reproduce）的
「拒绝」发生在加载期（verifier），编译脚本头部已注明「本脚本只编译不加载
（无 XDP 挂载风险）」。加载与挂载示例属练习完成后的自主实验（「XDP 程序直接
作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前
挂到生产物理网卡。」——见章级 [`../README.md`](../README.md) 安全警告）。

## 防剧透阅读法

两个练习的 README 都按「任务在前、参考实现思路独立成节放在后部」的结构组织，
**分界线（`---`）在「预期输出示例」节之后**——分界线之前是任务描述、提示、
环境前提与预期输出，分界线之后才是参考实现思路。读法：

1. 先只读分界线之前的各节，按「提示」回正文对应小节与画廊补课；
2. 自行实现、（在满足权限的环境）加载并观察现象；
3. 卡住，或做完想对照时，才翻到分界线之后的「参考实现思路」。

三个「别急着打开」：练习 1 的 `fixed.bpf.c`、练习 2 的 `reproduce.bpf.c` 与
两个 README 的分界线后内容都是参考实现——先做后看，剧透一次即作废。

## 构建命令

与画廊同构（compile-only，各目录独立 `make`；依赖与自检见
[`../00-env/`](../00-env/)）：

```bash
make -C book/examples/chapter-03/exercises/01-fix-challenge    # 产出 broken.bpf.o 与 fixed.bpf.o
make -C book/examples/chapter-03/exercises/02-log-diagnosis    # 产出 reproduce.bpf.o
```

或一次全量（仓库根执行）：

```bash
for d in book/examples/chapter-03/exercises/0*/; do make -C "$d"; done
```

口径与画廊一致：**预期被拒程序（broken／reproduce）也必须编译通过**——它们
不是语法错误示例，是语义被拒示例；编译成功 ≠ 能加载，正是本章教学主线。

## 验证状态汇总

按章级『验证状态』约定（见 [`../README.md`](../README.md)，含本章补充条；
详细核验记录见各练习 README「验证状态」节）：

| 练习 | 编译级 | 加载级（运行级待验证） |
|---|---|---|
| 01-fix-challenge | broken 与 fixed 均已 `make` 通过；broken 为双状态——编译级已通过（预期如此——拒绝发生在加载期） | broken『预期被拒』（首处报文边界拒绝与档位无关；特权档下未初始化栈读被豁免，形态见该练习 README）；fixed『预期加载成功』——均待特权环境验证 |
| 02-log-diagnosis | reproduce 已 `make` 通过；按本章补充条同口径——编译级已通过（预期如此——拒绝发生在加载期） | reproduce『预期被拒』；学生修复版『预期加载成功』——均待特权环境验证 |

- 编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；各
  README 中「预期输出」均为教学示例。
- 编译级已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过；broken 与 reproduce 产物另经 `llvm-objdump -S` 核验，三处
  offending 指令与负偏移算术真实存在（摘录见各练习 README）。
- 全量编译结果由 `scripts/verify-build.sh` 生成，摘要见章级
  [`../README.md`](../README.md)。

## 参考来源

- Linux kernel documentation：BPF 验证器（`Documentation/bpf/verifier.rst`）。
- 正文对位：练习 1 ← 画廊 3.1／3.2／3.4 与 2.3／2.5／2.6；练习 2 ← 2.5
  「有符号/无符号与掩码修正」段与排错节第①组速查表末行。
- [`../02-fail-gallery/`](../02-fail-gallery/)：七类典型拒绝的 bad/fixed 对照
  （两练习的机制底座）。

# 第 4 章「课后扩展任务」练习总览：选型决策与类型错位修复

本目录是《第 4 章 eBPF 程序类型与钩子点深入》「课后扩展任务」一节的配套
练习组：两个子目录各含一项完整参考实现与防剧透 README——练习 1 三个场景
先选型后论证（参考实现按正确选型合并为单个可编译对象），练习 2 给你一个
类型错位的坏程序（编译通过但加载预期被拒，诊断并把三元组归位）。任务表
如下（与正文「课后扩展任务」一节逐字一致）：

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 选型决策 | 三个场景（容器内文件打开审计、TCP 连接建立观测、内核函数入口参数抓取）各选程序类型与挂载点并给出选型论证；参考实现按正确选型合并为单个可编译对象 | `book/examples/chapter-04/exercises/01-type-selection/` |
| 2 | 类型错位修复 | 一段报文解析逻辑被放进 kprobe 程序且 ctx 声明为 `xdp_md`——编译通过但加载预期被拒；诊断并把类型与 SEC 归位 | `book/examples/chapter-04/exercises/02-type-mismatch/` |

**安全警告：本目录只编译不加载**——两个练习目录均为 compile-only 构建，
`make` 只产出 `.bpf.o`，不生成 skeleton、不挂载；预期被拒程序（broken）的
「拒绝」发生在加载期（验证器），编译脚本头部已注明「本脚本只编译不加载
（无挂载风险）」。加载与挂载示例属练习完成后的自主实验（「XDP 程序直接
作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响
前挂到生产物理网卡。」——见章级 [`../README.md`](../README.md) 安全警告）。

## 防剧透阅读法

两个练习的 README 都按「任务在前、参考实现思路独立成节放在后部」的结构
组织，**分界线（`---`）在「预期输出示例」节之后**——分界线之前是任务描述、
提示、环境前提与预期输出，分界线之后才是参考实现思路。读法：

1. 先只读分界线之前的各节，按「提示」回正文对应小节与画廊补课；
2. 自行完成选型论证与实现（练习 1），或诊断与归位（练习 2），（在满足权限
   的环境）加载并观察现象；
3. 卡住，或做完想对照时，才翻到分界线之后的「参考实现思路」。

三个「别急着打开」：练习 1 的 `solution.bpf.c`、练习 2 的 `fixed.bpf.c` 与
两个 README 的分界线后内容都是参考实现——先做后看，剧透一次即作废（练习 2
的 `broken.bpf.c` 是任务体本身，先读它）。

## 构建命令

与画廊同构（compile-only，各目录独立 `make`；依赖与自检见
[`../00-env/`](../00-env/)）：

```bash
make -C book/examples/chapter-04/exercises/01-type-selection    # 产出 solution.bpf.o
make -C book/examples/chapter-04/exercises/02-type-mismatch    # 产出 broken.bpf.o 与 fixed.bpf.o
```

或一次全量（仓库根执行）：

```bash
for d in book/examples/chapter-04/exercises/0*/; do make -C "$d"; done
```

口径与画廊一致：**预期被拒程序（broken）也必须编译通过**——它不是语法错误
示例，是语义被拒示例；编译成功 ≠ 能加载，正是本章教学主线（1.4 教师视角
「三道类型关卡都不在编译期」）。

## 验证状态汇总

按章级『验证状态』约定（见 [`../README.md`](../README.md)，含本章补充条；
详细核验记录见各练习 README「验证状态」节）：

| 练习 | 编译级 | 加载级（运行级待验证） |
|---|---|---|
| 01-type-selection | solution 已 `make` 通过 | 三程序『预期加载成功』——场景A 另有三前提（root＋`CONFIG_BPF_LSM=y`＋`lsm=bpf`，本机启动参数未启用）、场景B 需 root＋cgroup v2；均待特权环境验证 |
| 02-type-mismatch | broken 与 fixed 均已 `make` 通过；broken 为双状态——编译级已通过（预期如此——拒绝发生在加载期） | broken『预期被拒』（「invalid bpf_context access」形态，随内核版本略有差异）；fixed『预期加载成功』——均待特权环境验证 |

- 编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；各
  README 中「预期输出」均为教学示例。
- 编译级已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过。
- 全量编译汇总由 `scripts/verify-build.sh` 生成（计划内资产，随后续批次
  落地），摘要见章级 [`../README.md`](../README.md)。

## 参考来源

- Linux kernel documentation：BPF 验证器（`Documentation/bpf/verifier.rst`）
  与 BPF 程序类型相关篇目（`Documentation/bpf/`）。
- 正文对位：练习 1 ← 1.4 决策树／1.2 三元组／1.3 总表／2.3／3.3/3.4/3.12/
  3.14 card／5.2 三步法；练习 2 ← 1.2 返回值语义／1.4 教师视角／2.3／本章
  排错节判别流程与第 3 章排错节。
- [`../03-type-gallery/`](../03-type-gallery/) 与
  [`../02-xdp-tc/`](../02-xdp-tc/)：两练习的机制底座（画廊⑤⑦正面形态与
  锚点②XDP 同构解析）。

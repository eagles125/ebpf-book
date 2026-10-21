# 第 3 章独立示例资产

本目录是《第 3 章 验证器与安全保障》的配套示例代码，对应正文「三、失败画廊：七类典型拒绝」与「四、实战：读懂你的第一条 verifier 日志」两节；
全部资产围绕验证器的正反两条路径组织——能通过验证的 XDP 报文解析锚点（01-pkt-parse）与七类典型拒绝的 bad/fixed 失败画廊（02-fail-gallery），外加 2 项课后扩展任务。

## 目录结构

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | 内核/BTF/工具链/权限分级诊断（无 root 可跑） |
| `01-pkt-parse/` | 正面锚点 | 能过验证的最小 XDP 报文解析：验证「通过」路径的实物 |
| `02-fail-gallery/` | 失败画廊 | 七类典型拒绝的 bad/fixed 对：验证「拒绝」路径的实物 |
| `scripts/` | 编译级验证 | `verify-build.sh` 汇总本目录全部 make |
| `exercises/` | 课后扩展任务 | 2 项完整参考实现 + 防剧透 README |

## 演示目标

本章示例把「验证器放行什么、拒绝什么」钉到两组实物上，正反两条路径各自落到一个目录：

| 路径 | 落点 | 实物 |
|---|---|---|
| 验证「通过」 | `01-pkt-parse/` | 能过验证的最小 XDP 报文解析：eth→ipv4→udp 三层边界检查 + 映射（map）计数最小闭环 |
| 验证「拒绝」 | `02-fail-gallery/` | 七类典型拒绝的 bad/fixed 对：bad 程序在 clang 层面全部合法（拒绝发生在加载期，编译成功≠能加载） |

## 学习路径

1. `bash 00-env/check-env.sh` 自检：无 root 也可运行，按「编译级/加载级/运行级」三档给出结论。
2. 进入 `01-pkt-parse/`：先 `make` 编译「通过路径」锚点，再按其 README 体验报文解析与映射计数
   （加载与运行需要 root/CAP_BPF，挂载 XDP 还需 CAP_NET_ADMIN；编译级验证在无特权机器即可完成）。
3. 进入 `02-fail-gallery/`：按 01→07 顺序逐对 diff bad/fixed，对照每对 README 的预期 verifier 日志关键行
   （bad 也必须编译通过——拒绝发生在加载期，不在编译期）。
4. 进入 `exercises/`：完成 2 项课后扩展任务（先自行实现，再看防剧透 README 后部的参考实现思路）。

## 安全警告

- XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。
- 失败画廊与课后练习只编译不加载（拒绝发生在加载期）；`00-env/check-env.sh` 为只读检查脚本，
  `scripts/verify-build.sh` 只编译不加载，均无上述风险。

## 统一构建模式

`01-pkt-parse/` 锚点采用 libbpf-bootstrap 全链三步（目录内直接 `make`）：

vmlinux.h（bpftool 由 /sys/kernel/btf/vmlinux 生成） -> pkt_parse.bpf.o（clang -target bpf）
    -> pkt_parse.skel.h（bpftool gen skeleton） -> 可执行 pkt_parse（gcc -lbpf -lelf -lz）

画廊七对与 exercises 练习采用 compile-only 构建（目录内直接 `make`）：只产出 `.bpf.o`，不生成 skeleton 与 loader。
原因：bad/broken 程序不是语法错误，而是语义被拒——它们在 clang 层面必须全部编译通过，拒绝发生在加载期；
加载级行为本机不可实测，verify-build 对这些程序的验收口径就是「编译成功」。

`00-env/` 为纯脚本目录（无 Makefile）；`scripts/verify-build.sh` 汇总本目录全部 `make`。

## 验证状态说明

遵循 `book/STYLE.md` 约定：

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  各 README 中「预期输出」均为教学示例。
- **本章补充**：bad 程序与 broken 练习的验证状态为「编译级已通过（预期如此——拒绝发生在加载期）；
  加载级『预期被拒』待特权环境验证」。
- 全量编译结果由 `scripts/verify-build.sh` 生成，摘要见 Task 10 回填。

## 参考来源

- Linux kernel documentation：BPF 验证器（`Documentation/bpf/verifier.rst`）与 XDP 相关文档。
- libbpf 与 libbpf-bootstrap 项目文档。
- `bpftool` 手册页（BTF dump 与 skeleton 生成）。

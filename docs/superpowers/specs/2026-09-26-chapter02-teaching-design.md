---
comet_change: generate-ebpf-book
role: technical-design
canonical_spec: openspec
---

# 第 2 章完整教学章节技术设计（eBPF 架构与核心概念，概念章 + 完整实战化）

- 日期：2026-09-26
- 上游事实源：`openspec/changes/generate-ebpf-book/`（proposal / design / specs / tasks）
- 设计范围：tasks.md「Chapter 2: eBPF Architecture and Core Concepts」全部 9 条任务——第 2 章正文从零写为完整教学章节，落地独立示例资产与课后扩展任务参考实现。
- 模式先例：沿用第 17 章已验证的工作模式（设计文档 → 实施计划 → subagent 逐任务构建 + thorough 逐任务审查 → 终审修复波），教学规范遵循 `book/STYLE.md`（完整章节模板 + 实战章节教学模板）。

## 1. 背景与目标

`book/chapters/` 现有 01/09/17 三章：第 1 章为 14.8k 字符的概念章（含最小 tracepoint 集成示例），第 17 章为 69.4k 字符的完整实战章。第 2 章《eBPF 架构与核心概念》是全书概念基石——第 1 章导读已预告的「指令集、verifier、map、helper、程序类型、挂载点」机制，在本章展开为系统化讲解；后续第 3 章（verifier）等以本章为地基。

用户已确认（2026-09-26）：**完整实战化**——含独立示例资产（`book/examples/chapter-02/`）、紧凑五层环境阐述与 2 项课后练习（含完整可编译参考实现），正文按完整章节模板约 35–45k 字符，符合 STYLE.md 实战章节模板与 tasks.md §7 审计口径，不留回填债。

### 已确认的决策

| 决策 | 内容 |
|------|------|
| 章节定位 | 概念章（完整章节模板）+ 完整实战化（独立示例资产触发 STYLE.md 实战章节教学模板四条规范） |
| 验证策略 | 沿用第 17 章：编译级真实验证（本机 clang 18.1.3 / bpftool 7.7.0 / libbpf-dev 1.3.0 / Go 不涉及）+ 运行效果写「预期输出示例」标注「运行级待验证」（本机无 root，`unprivileged_bpf_disabled=2`） |
| 示例组织 | 单一集成示例目录（01-trace-stat）+ 2 项课后练习，不做多阶段渐进（概念章无渐进叙事需求；四段式仅约束渐进阶段章节） |
| 练习规模 | 2 项（事件通道与 LRU 主题），完整可编译参考实现 + 防剧透 README |
| 内核兼容 | 5.10+ 保守选择；tracepoint 上下文经 vmlinux.h CO-RE 读取，版本相关细节保守措辞并注明（本机内核 7.0.0-31） |

## 2. 示例资产设计

### 2.1 目录结构

```text
book/examples/chapter-02/
├── README.md                # 章级导读：示例说明、构建模式、验证状态约定、安全警告
├── 00-env/
│   └── check-env.sh         # 环境自检：内核/BTF/clang/bpftool/libbpf/权限分级（无特权可跑）
├── 01-trace-stat/           # 集成示例：sched_switch tracepoint + per-CPU 统计
│   ├── trace_stat.bpf.c     # ~60 行：SEC("tracepoint/sched/sched_switch")、上下文对象读取、
│   │                        #   bpf_get_smp_processor_id() 选 CPU 下标、per-CPU array 计数
│   ├── trace_stat.c         # C loader（libbpf-bootstrap 风格：open_and_load + attach(tp) +
│   │                        #   每秒聚合打印各 CPU 调度计数 + Ctrl-C 退出即卸载）
│   ├── Makefile             #   vmlinux.h -> .bpf.o -> skel.h -> loader（C7 同构模板）
│   └── README.md            #   十段结构（C8 口径）：任务/文件/环境/编译运行/预期输出/
│                            #   代码讲解/常见失败点/验证状态/参考来源
├── scripts/
│   └── verify-build.sh      # 单章编译级验证：00-env + 01-trace-stat + exercises 逐目录汇总
└── exercises/               # 课后扩展任务 + 完整可编译参考实现
    ├── README.md            #   任务总览 + 防剧透结构说明
    ├── 01-ringbuf-events/   # 练习1：stats 轮询改 ring buffer 事件流
    │   ├── trace_events.bpf.c    #   BPF_MAP_TYPE_RINGBUF + bpf_ringbuf_reserve/output
    │   ├── trace_events.c        #   用户态 ring_buffer__poll 事件循环
    │   ├── Makefile
    │   └── README.md             #   防剧透七段结构
    └── 02-lru-top-proc/     # 练习2：LRU hash 记录每进程调度次数
        ├── top_proc.bpf.c        #   BPF_MAP_TYPE_LRU_HASH（pid → 次数）+ LRU 容量语义
        ├── top_proc.c            #   用户态遍历/排序 top-N 打印
        ├── Makefile
        └── README.md
```

### 2.2 设计原则

- **独立性**：示例资产自包含（自带 00-env 与 verify-build），不依赖第 9/17 章资产；Makefile 与第 17 章 C7 模板同构（`<名>=trace_stat`），读者可只读第 1–2 章即完成首个可运行闭环。
- **教学映射**：01-trace-stat 一个示例贯穿 tasks.md §Chapter 2 的概念要素——程序（tracepoint 程序类型）、映射（per-CPU array）、辅助函数（`bpf_get_smp_processor_id()`）、程序类型与挂载点（`SEC("tracepoint/...")` 隐式推断）、上下文对象（tracepoint ctx 结构经 vmlinux.h CO-RE 读取）、事件通道（用户态轮询——练习 1 对比 ring buffer 推送）、生命周期（加载→验证→JIT→挂载→触发→卸载）。
- **代码注释规范**（STYLE.md 实战模板规则 2）：中文注释覆盖数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。
- **bpf() 内核实现分析为正文内容**（tasks.md 明确项）：`kernel/bpf/syscall.c` 的对象创建、fd 所有权、引用计数与 pin 生命周期——纯讲解 + 内核路径引用，不要求内核源码级调试环境。
- **产物不入库**：`.gitignore` 追加 `book/examples/chapter-02/01-trace-stat/trace_stat`、exercises 两个可执行物与 `vmlinux.h`/`*.skel.h`/`*.o`（后者已被全局规则覆盖，可执行物需按任务追加）。

## 3. 章节正文设计

### 3.1 章节结构（完整章节模板）

```text
# 第 2 章 eBPF 架构与核心概念
学习目标
本章导读（承接第 1 章的机制预告；本章的学习路线图——先全景、再深入两个支柱[指令集/映射]、
  再看内核如何管理对象，最后动手）
一、eBPF 架构总览（八要素：程序/映射/辅助函数/程序类型/挂载点/上下文对象/事件通道/生命周期；
  各要素一节，配「第 1 章最小 tracepoint 示例」的回指定位——把概念钉在读者已见过的代码上）
二、指令集基础（64 位 RISC 模型、10 个寄存器与调用约定、8 字节指令编码 bpf_insn、
  常见指令类别与 bpftool insn dump 观察法；概念讲解配少量编码示例，不展开完整 ISA 表——指向附录 A）
三、映射类型全景（hash/array/LRU hash/per-CPU array/perf event array/ring buffer/LPM trie/sockmap：
  每类一 card——语义、典型场景、容量与并发特征、选型理由；配选型决策表与 bpftool map 观察）
四、bpf() 系统调用与内核实现分析（`kernel/bpf/syscall.c`：BPF_PROG_LOAD/BPF_MAP_CREATE 路径、
  fd 所有权与引用计数、程序生命周期钩子、pin 与 bpffs；`include/uapi/linux/bpf.h` 命令集概览）
五、实战：第一个追踪统计示例（5.1 五层环境[紧凑]：依赖/编译/部署/运行/效果与验证状态约定；
  5.2 代码走读[教师视角引入 + 核心代码原理讲解 + 学生动手 + 检查点自测]——贴 01-trace-stat 源码逐段讲解）
可视化：eBPF 程序生命周期与数据通路（加载-验证-JIT-挂载-触发-卸载全链路文本图 + 对象/fd/pin 关系图）
常见问题与排错（load 失败权限表、map 选型误用、tracepoint 上下文字段版本差异、轮询 vs 事件流取舍）
课后扩展任务（2 项，指向 exercises/，表格：编号/扩展维度/任务一句话/对应目录/验证状态）
总结
本章参考来源
```

### 3.2 教学规范落点

- **五层环境阐述**（实战模板规则 1）：以「五、实战」5.1 小节承载（紧凑版，约 3–4k 字符），口径与第 17 章 1.1–1.5 一致但面向单机 tracepoint 场景（无 veth/netns 拓扑；安全警告改为「加载未经验证的程序影响系统行为，实验环境操作；勿在生产主机随意加载」+ 权限矩阵）。
- **四段式**（规则 3）：不约束本章（非渐进阶段章节）；「五、实战」5.2 内以教师视角引入/学生动手/检查点落实双视角。
- **双视角**：总览、映射类型、实战三节落「**教师视角**」「**学生动手**」引导段与自测检查点。
- **练习约定**（规则 4）：防剧透七段 README、`exercises/` 目录、验证状态标注，口径与第 17 章一致。

### 3.3 篇幅预算

目标 35–45k 字符（`wc -m`）：总览 5–6k、指令集 5–6k、映射类型 6–8k、bpf() 内核分析 5–6k、实战（含五层）9–11k、可视化 1.5–2.5k、排错 3–4k、课后任务与总结/参考 3–4k。

## 4. 课后练习设计

| 练习 | 扩展维度 | 参考实现要点 |
|------|---------|-------------|
| 01-ringbuf-events | 事件通道 | `BPF_MAP_TYPE_RINGBUF` + `bpf_ringbuf_reserve`/`bpf_ringbuf_output`；用户态 `ring_buffer__poll`；与轮询的取舍讲解 |
| 02-lru-top-proc | 映射类型 | `BPF_MAP_TYPE_LRU_HASH`（pid→计数）+ LRU 容量语义；用户态遍历排序 top-N；LRU 满表静默淘汰口径（与第 17 章一致） |

- 每个练习：完整可编译源码 + 防剧透 README（任务与提示在前，参考实现思路独立成节放后部）。
- 验证状态：编译级「已验证（本机）」+ 运行级一律「待验证」。

## 5. 验证策略（编译级）

`scripts/verify-build.sh`（单章版，模式同第 17 章）：

1. BTF 前置检查（`/sys/kernel/btf/vmlinux`）；
2. `01-trace-stat` 与 exercises 两目录逐个 `make`（clang `-target bpf` + `bpftool gen skeleton` + C loader）；
3. 汇总 `PASS/WARN/FAIL`，退出码 0=全绿；结果写入章级 README 验证状态说明区。

本机全部可执行（无网络依赖）；运行级（实际挂载 tracepoint）按 C6 标注「预期输出示例 / 运行级待验证」。

## 6. 技术风险与边界

| 风险 | 缓解 |
|------|------|
| tracepoint 上下文结构字段随内核版本变化 | 经 vmlinux.h CO-RE 读取 + 保守措辞注明（本机 7.0.0-31 实测 BTF 可用） |
| sched_switch 的 prev_comm 数组读取边界 | `bpf_probe_read_str` 语义讲解 + verifier 定界注释 |
| 概念章篇幅膨胀（8 要素全景易写散） | §3.3 分节预算 + 「每个概念钉回读者已见过的示例」的写作纪律 |
| sockmap/LPM 等高级映射讲深了抢后续章节 | 映射类型全景只讲「语义/场景/选型」，机制细节留给专属章节（LPM→第 17 章、sockmap→第 19 章，交叉引用） |
| 挂载 tracepoint 需特权 | 五层环境权限矩阵 + 运行级待验证标注 |

## 7. tasks.md 回写清单

- 完成后统一勾选 tasks.md「Chapter 2: eBPF Architecture and Core Concepts」9 条（由 plan 的收口任务执行，同第 17 章 Task 15 模式）；
- delta spec 无需变更（无 Spec Patch）；
- Chapter 1 的两条遗留任务（终稿深度 review / 五层与双视角回填）不在本设计范围，维持原样留给全书统稿批次。

## 8. 测试策略汇总

- **编译级（本机执行）**：verify-build.sh 全绿为 plan 完成条件之一（record-check 记录）。
- **结构级**：完整章节模板九节齐备；五层环境、双视角、注释五类要点、可视化、排错、总结、参考、课后任务全部在场；正文代码块与示例文件一一对应。
- **运行级**：预期输出 + 待验证标注；由特权环境/读者复验。

## ADDED Requirements

### Requirement: Manuscript scaffold

The repository SHALL contain a `book/` manuscript scaffold for the Chinese eBPF self-study book, with a top-level README, a style guide, chapter files, appendix files, and example directories as needed.

#### Scenario: First-batch scaffold exists

- **WHEN** the first batch is implemented
- **THEN** `book/README.md`, `book/STYLE.md`, `book/chapters/`, and `book/appendix/` SHALL exist

### Requirement: Complete table of contents

The manuscript README SHALL list the full 25-chapter table of contents and appendices from the user-provided requirements.

#### Scenario: Reader opens the book entry point

- **WHEN** a reader opens `book/README.md`
- **THEN** they SHALL see the book title, target audience, prerequisites, complete chapter list, appendix list, and staged generation plan

### Requirement: Chapter structure contract

Each complete chapter SHALL include learning objectives, principle explanation, implementation analysis where relevant, reproducible examples where relevant, at least one visualization, summary, and chapter references.

#### Scenario: Complete chapter review

- **WHEN** a complete chapter is reviewed
- **THEN** the reviewer SHALL be able to identify learning objectives, visualization content, summary, and references in that chapter

### Requirement: Narrative depth and integrated examples

Complete chapters SHALL be written as substantive book chapters, not outline-style notes, and example code SHALL be integrated with explanatory prose.

#### Scenario: Example-driven chapter review

- **WHEN** a chapter includes an eBPF example
- **THEN** the chapter SHALL include the code or a complete planned code block, compile/run commands, expected output, and prose explaining the key lines, data structures, helper calls, attachment point, and output path

### Requirement: Standalone example assets

Examples referenced by practical chapters SHALL be materialized as standalone files under `book/examples/<chapter>/` when the corresponding chapter is completed, rather than existing only as inline prose.

#### Scenario: Practical chapter completion

- **WHEN** a practical chapter is marked complete
- **THEN** every runnable example promised by that chapter SHALL have standalone source files, a Makefile or build script, a run script where appropriate, and a README explaining prerequisites and verification status

#### Scenario: Skeleton chapter with planned examples

- **WHEN** a chapter is still a skeleton
- **THEN** its `book/examples/<chapter>/` directory MAY be empty, but the OpenSpec task list SHALL include explicit pending tasks to generate those standalone example assets

### Requirement: Chapter 1 complete sample

The first batch SHALL include a complete Chapter 1 sample in Chinese that covers eBPF history, major use cases, comparison with kernel modules, kernel implementation orientation, an operational orientation example, visualization, summary, and references.

#### Scenario: Style calibration

- **WHEN** future chapters are generated
- **THEN** Chapter 1 SHALL serve as the baseline for voice, structure, depth, and reference style

### Requirement: Chapter 9 deep skeleton

The first batch SHALL include a detailed Chapter 9 skeleton that explicitly reserves coverage for the ebpf.top BPF introductory path: Hello World, kprobe, tracepoint, map, and perf event.

#### Scenario: Chapter 9 requirement audit

- **WHEN** Chapter 9 is audited against the user requirements
- **THEN** the skeleton SHALL mention Hello World, kprobe, tracepoint, map, perf event, compile commands, loader scripts, expected output, and verification notes

### Requirement: Chapter 9 ebpf.top full introductory series coverage

Chapter 9 SHALL cover all 12 articles in the ebpf.top BPF introductory series, including technical introduction, learning path, environment setup, Hello World, PID filtering, container security, ring buffer, file-open tracing, perf_event file-open tracing, return value tracing, tracepoint file-open tracing, and Netfilter with Go experience.

#### Scenario: ebpf.top series audit

- **WHEN** Chapter 9 is audited against the ebpf.top BPF introductory series
- **THEN** the chapter SHALL contain one identifiable section for each of the 12 articles and SHALL map each article to concrete example code, commands, expected output, or verification requirements

### Requirement: Chapter 17 deep skeleton

The first batch SHALL include a detailed Chapter 17 skeleton for a high-performance eBPF firewall prototype, covering XDP/TC selection, conntrack, ACL, NAT, checksum updates, userspace control plane, and benchmarking.

#### Scenario: Chapter 17 requirement audit

- **WHEN** Chapter 17 is audited against the user requirements
- **THEN** the skeleton SHALL mention datapath/control-plane separation, XDP versus TC, conntrack map/state machine, ACL matching, SNAT/DNAT, checksum update, rule management, and pktgen or iperf benchmarking

### Requirement: 实战章节五层环境阐述

含可运行示例的完整实战章节必须（MUST）按依赖环境、编译环境、部署环境、运行环境、运行效果五层依次阐述：依赖环境说明内核版本、BTF、权限（root 或 `CAP_BPF`/`CAP_NET_ADMIN`/`CAP_PERFMON`）与前置工具；编译环境说明 clang/LLVM、libbpf、Go 等工具链的版本要求与安装验证方式；部署环境说明实验拓扑与脚本化搭建/销毁步骤；运行环境说明挂载、启动、卸载方式；运行效果给出每个关键步骤的预期输出与可观测指标。

#### Scenario: 读者从零复现实战章节示例

- **WHEN** 读者按完整实战章节从环境准备开始逐步操作
- **THEN** 章节应按五层环境顺序给出完整说明与预期输出，读者无需在章节之外寻找缺失步骤或自行猜测环境版本

### Requirement: 核心代码原理讲解与注释

实战章节的每个核心代码段必须（MUST）配套原理讲解，说明设计原因、数据结构选型、关键 helper 调用、verifier 边界约束与失败模式；对应的独立示例源文件必须携带中文注释解释关键行为，使读者仅通过章节正文与源码注释即可理解实现原理。

#### Scenario: 核心代码段审阅

- **WHEN** 审阅者阅读实战章节中的任意核心代码段
- **THEN** 应能在紧邻代码的位置找到原理讲解，并在对应独立源文件中找到解释关键行为的中文注释

### Requirement: 教师-学生双视角写作

完整章节必须（MUST）体现教师与学生双重视角：教师视角回答"怎么讲读者才学得快"，包括讲解顺序设计、类比、可视化与常见误区提醒；学生视角回答"怎么学才学得透"，包括动手实践清单、阶段性检查点与自测问题。

#### Scenario: 双视角写作审阅

- **WHEN** 审阅者检查完整章节的正文组织
- **THEN** 应能识别出教师视角的讲解路径设计与学生视角的实践、检查点或自测环节

### Requirement: 课后扩展任务与参考实现

含可运行示例的完整章节必须（MUST）在章末布置基于本章示例的扩展应用与功能开发任务，并为每项任务提供参考实现（源码与实现思路说明），存放于 `book/examples/<chapter>/exercises/` 或章节明确声明的对应目录，并标注验证状态。

#### Scenario: 读者核对课后任务实现

- **WHEN** 读者完成章末课后扩展任务后希望核对思路
- **THEN** 应能在书稿配套示例目录中找到该任务的参考实现与实现思路说明，并了解其验证状态

### Requirement: 第 17 章完整教学章节

第 17 章必须（MUST）从深度骨架升级为完整教学章节：正文按五层环境阐述组织，XDP/TC 数据面、conntrack、ACL、NAT、校验和、Go 控制面与压测的每个核心代码段都有原理讲解与源码注释，正文体现教师-学生双视角，章末提供课后扩展任务与参考实现，且 `book/examples/chapter-17/` 下的独立示例资产完整落地。

#### Scenario: 第 17 章教学完整性审计

- **WHEN** 第 17 章按教学要求审计
- **THEN** 章节应包含五层环境阐述、逐段核心代码原理讲解、教师-学生双视角叙事、课后扩展任务，以及 `book/examples/chapter-17/` 下可构建的独立示例与参考实现

### Requirement: Source and verification policy

The manuscript SHALL prioritize the user's authoritative reference list and SHALL avoid claiming local execution of eBPF examples unless verification was actually performed in a suitable kernel environment.

#### Scenario: Example cannot be run locally

- **WHEN** an eBPF example requires privileges or kernel features unavailable in the current environment
- **THEN** the manuscript SHALL mark the environment assumptions and provide expected output as illustrative rather than locally verified

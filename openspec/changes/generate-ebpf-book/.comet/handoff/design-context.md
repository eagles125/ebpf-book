# Comet Design Handoff

- Change: generate-ebpf-book
- Phase: design
- Mode: compact
- Context hash: 9ac39682406ad23e8a43af072741f49c6bf2a53f01a77caa1f73be301c47a3d4

Generated-by: comet-handoff.sh

OpenSpec remains the canonical capability spec. This handoff is a deterministic, source-traceable context pack, not an agent-authored summary.

## openspec/changes/generate-ebpf-book/proposal.md

- Source: openspec/changes/generate-ebpf-book/proposal.md
- Lines: 1-32
- SHA256: e2f3514d3ebe960e548bb93f4e94f7e20bccd91a1dba10714a9a3e612bcafc53

```md
## Why

用户需要一本体系化的中文 eBPF 自学书籍，内容覆盖基础原理、Linux 内核实现、可观测性、网络优化、CO-RE 可移植开发以及 Go/Rust 互操作。该书目标总量大、章节多、代码示例多，必须先建立可持续的 Markdown 书稿结构和质量约束，避免一次性生成难以审阅和维护的超大文档。

## What Changes

- 新增 `book/` 目录作为书稿根目录，按章节和附录拆分 Markdown 文件。
- 新增全书入口 `book/README.md`，包含书名、目标读者、前置知识、完整 25 章目录、附录和分批生成计划。
- 新增 `book/STYLE.md`，约束中文术语、章节结构、代码块、预期输出、参考来源和未验证示例标注方式。
- 生成第 1 章完整样章，用于校准后续章节的中文风格、技术深度、引用格式和可视化表达。
- 生成第 9 章深度骨架，锁定 ebpf.top BPF 入门系列必须覆盖的 Hello World、kprobe、tracepoint、map、perf event 路径。
- 生成第 17 章深度骨架，锁定高性能 eBPF 防火墙原型的 XDP/TC、conntrack、ACL、NAT、校验和、控制面和压测内容。
- 将第 17 章从深度骨架升级为完整教学章节：按依赖、编译、部署、运行、效果五层阐述环境，核心代码段配原理讲解与中文注释，采用教师-学生双视角叙事，章末布置扩展任务并提供参考实现，落地 `book/examples/chapter-17/` 全部独立示例资产。
- 建立全书教学式写作规范（五层环境阐述、核心代码原理注释、教师-学生双视角、课后扩展任务与参考实现），写入 `book/STYLE.md` 并约束后续批次章节。
- 新增附录 stub，作为后续完善指令集、工具命令、BTF 配置和参考链接的承载位置。

## Capabilities

### New Capabilities

- `ebpf-book-manuscript`: 生成和维护《eBPF 从入门到精通：原理、内核实现与实战可移植开发》的分章 Markdown 书稿、写作规范、首批样章与关键章节骨架。

### Modified Capabilities

无。

## Impact

- 新增书稿文件：`book/README.md`、`book/STYLE.md`、`book/chapters/*.md`、`book/appendix/*.md`。
- 新增 OpenSpec 规划文件：`openspec/changes/generate-ebpf-book/`。
- 不引入应用运行时依赖，不修改已有代码接口。
- 首批输出不承诺完整 25 万字终稿；后续按批次继续生成和验证。

```

## openspec/changes/generate-ebpf-book/design.md

- Source: openspec/changes/generate-ebpf-book/design.md
- Lines: 1-64
- SHA256: 8baa42c5876b82ab910137a5e80785feb697bab6bf86e4aab498291867b8e7ae

```md
## Context

当前仓库是一个轻量 OpenSpec skeleton，没有既有书稿结构。用户要求的最终书籍不少于 25 万汉字，包含 25 章和附录，并且要求每章具备学习目标、原理解析、内核实现分析、可运行示例、可视化内容、总结和参考来源。该规模不适合在单次聊天回复中生成，也不适合放在单个 Markdown 文件中维护。

首批工作采用“OpenSpec 管理计划 + `book/` 分章 Markdown 落地”的方式推进。OpenSpec 工件记录目标、范围、验收标准和后续批次；`book/` 目录承载实际中文书稿。

## Goals / Non-Goals

**Goals:**

- 建立稳定的书稿目录结构。
- 写出完整全书目录和分批生成计划。
- 生成可作为风格基准的第 1 章完整样章。
- 为第 9 章和第 17 章建立足够详细的骨架，确保后续不会遗漏用户特别强调的内容。
- 明确引用、示例验证和未验证环境假设的处理规则。

**Non-Goals:**

- 首批不生成完整 25 章终稿。
- 首批不保证达到 25 万汉字总量。
- 首批不在当前环境运行 eBPF 程序；eBPF 示例通常需要 root 权限、内核 BTF、clang/LLVM、bpftool、`CAP_BPF`、`CAP_PERFMON` 或 `CAP_NET_ADMIN`。
- 首批不引入 mdBook、MkDocs、Pandoc 等构建系统，保持 Markdown 文件可直接审阅。

## Decisions

1. **使用分章 Markdown，而不是单文件书稿。**
   - 选择原因：25 万字规模需要分章审阅、局部重写和后续批次续写。
   - 替代方案：单文件 `ebpf-book.md`。缺点是冲突大、检索和审阅成本高。

2. **首批先生成第 1 章完整样章。**
   - 选择原因：第 1 章能校准语言、节奏、参考格式和“原理 + 实现 + 实战 + 可视化”的章结构。
   - 替代方案：直接批量生成多章。风险是风格和深度偏差会被放大。

3. **第 9 章和第 17 章先做深度骨架。**
   - 选择原因：第 9 章绑定 ebpf.top BPF 入门系列，第 17 章绑定复杂网络防火墙原型，是全书风险最高的两章。
   - 替代方案：等写到对应章节时再规划。风险是代码资产、验证环境和性能评估口径准备不足。

4. **示例采用“可复现说明优先，运行声明保守”的策略。**
   - 选择原因：当前环境不能代表读者的 eBPF 内核环境，不能声称未运行的程序已验证。
   - 替代方案：直接给出“预期可运行”结论。风险是误导读者并掩盖内核版本差异。

5. **实战章节采用教师-学生双视角的教学式写作。**
   - 选择原因：用户明确要求读者"跟着书就能学会"。教师视角负责讲解顺序、类比与误区提醒（怎么讲才学得快），学生视角负责实践清单与自测（怎么学才学得透），两者结合同时覆盖"迅速学明白"与"学透彻"。
   - 替代方案：纯参考手册式写作。缺点是读者知道结论但不知道推理过程，遇到环境差异时无法自主排查。

6. **实战章节按五层环境阐述组织：依赖、编译、部署、运行、效果。**
   - 选择原因：eBPF 示例失败大多出在环境而非代码。把环境从依赖到运行效果分层讲透，读者才能从零复现，出错时也能定位属于哪一层的问题。
   - 替代方案：只在附录给统一环境说明。缺点是章节内断链，读者需要来回跳转拼凑步骤。

7. **课后扩展任务与参考实现是含示例章节的标配。**
   - 选择原因：用户要求基于示例布置扩展开发任务并提供参考实现。任务驱动巩固学习最有效，参考实现让读者可以自查思路，形成学习闭环。
   - 替代方案：只留开放性思考题。缺点是读者无从验证自己的方案。

8. **第 17 章从深度骨架直接升级为完整教学章节，不等批次 5。**
   - 选择原因：第 17 章是全书技术风险最高的章节，用户反馈其内容过于空泛且示例目录为空。提前完整化可以尽早落地教学规范，为后续实战章节提供样板。
   - 替代方案：按原批次计划到批次 5 再完整化。缺点是教学规范落地太晚，此前批次生成的章节可能需要返工。

## Risks / Trade-offs

- [技术内容过时] → 后续生成涉及内核、libbpf、Cilium、Go/Rust 库细节时，优先核对官方文档和上游项目。
- [示例无法在当前环境验证] → 在章节中标注环境要求、预期输出和待验证项，不伪造本地运行结果。
- [章节风格不一致] → 通过 `book/STYLE.md` 固定术语、结构和引用规则。
- [第 17 章复杂度过高] → 先固化架构与代码清单，再在后续批次补齐并单独验证。
- [教学组件篇幅膨胀] → 通过 `book/STYLE.md` 统一五层环境与课后任务的写法模板，避免每章重复发明结构。

```

## openspec/changes/generate-ebpf-book/tasks.md

- Source: openspec/changes/generate-ebpf-book/tasks.md
- Lines: 1-431
- SHA256: 55b29b04a1543621bc2914adc6ae141c2690a585e6c37bf5a35807e55f7fdfcd

[TRUNCATED]

```md
# Tasks: Generate Chinese eBPF Self-Study Book

## 1. Planning Artifacts

- [x] Create OpenSpec proposal for the book-generation change.
- [x] Create OpenSpec design for the hybrid OpenSpec + chapter Markdown approach.
- [x] Create capability spec for the manuscript scaffold and first-batch content.
- [x] Create task list for the first implementation batch.

## 2. Manuscript Scaffold

- [x] Create `book/README.md` with title, audience, prerequisites, complete table of contents, reading path, source policy, and generation plan.
- [x] Create `book/STYLE.md` with terminology, chapter template, code/example policy, reference format, and validation notes.
- [x] Create `book/chapters/`, `book/appendix/`, and example directories.
- [x] Create appendix stubs:
  - [x] `book/appendix/a-ebpf-instruction-set.md`
  - [x] `book/appendix/b-tool-command-index.md`
  - [x] `book/appendix/c-distro-btf-build-guide.md`
  - [x] `book/appendix/d-reference-links.md`

## 3. Chapter 1 Complete Sample

- [x] Write `book/chapters/01-ebpf-overview-and-history.md`.
- [x] Include learning objectives.
- [x] Explain classic BPF to eBPF history.
- [x] Explain observability, networking, and security use cases.
- [x] Compare eBPF with kernel modules.
- [x] Add kernel implementation orientation with key subsystem paths.
- [x] Add a reproducible environment/orientation example.
- [x] Add at least one text visualization.
- [x] Add summary and chapter references.

## 4. Chapter 9 Deep Skeleton

- [x] Write `book/chapters/09-observability-getting-started-ebpf-top.md`.
- [x] Include the full required structure for ebpf.top BPF introductory-series coverage.
- [x] Reserve sections for Hello World, kprobe, tracepoint, map, and perf event examples.
- [x] List planned code files, compile commands, loader scripts, and expected outputs.
- [x] Add notes for verification against ebpf.top before final chapter completion.
- [x] Expand Chapter 9 to cover all 12 ebpf.top BPF introductory-series articles.

## 4.0 Chapter 9 Standalone Example Assets

- [x] Generate `book/examples/chapter-09/00-env/check-env.sh`.
- [x] Generate `book/examples/chapter-09/01-hello-world/` with BPF C source, userspace loader, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/02-pid-filter/` with map-based PID filtering example, userspace PID configuration, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/03-container-security/` with cgroup/container filtering example, policy map, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/04-ring-buffer/` with ring buffer event output example, userspace polling code, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/05-open-file-kprobe/` with file-open kprobe tracing example, filename read logic, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/06-open-file-perf-event/` with perf event output example, userspace perf buffer reader, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/07-open-file-return/` with enter/exit return-value correlation example, temporary map cleanup, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/08-open-file-tracepoint/` with tracepoint-based file-open tracing example, tracefs format notes, Makefile, run instructions, and README.
- [x] Generate `book/examples/chapter-09/09-netfilter-go/` with Netfilter BPF program, Go control plane, build/run scripts, isolated topology notes, and README.
- [x] Cross-link Chapter 9 inline code sections to the standalone `book/examples/chapter-09/` files.
- [x] Verify Chapter 9 standalone examples compile in a suitable privileged Linux eBPF environment, or mark each example with explicit verification status and reason.

## 4.1 Review Feedback Follow-up

- [x] Expand Chapter 1 beyond outline-style prose.
- [x] Integrate example code into Chapter 1 and explain the example.
- [x] Update writing style rules to require substantive prose and code explanations.

## 5. Chapter 17 Deep Skeleton

- [x] Write `book/chapters/17-high-performance-ebpf-firewall.md`.
- [x] Include architecture for datapath/control-plane separation.
- [x] Include XDP versus TC selection analysis.
- [x] Define conntrack map and state-machine plan.
- [x] Define ACL map and matching plan.
- [x] Define SNAT/DNAT and checksum-update plan.
- [x] Define userspace control-plane plan.
- [x] Define benchmark and result-interpretation plan.

## 5.1 Chapter 17 Complete Teaching Chapter and Standalone Example Assets

按教学式写作要求将第 17 章从深度骨架升级为完整教学章节，并落地全部独立示例资产、课后扩展任务与参考实现。

### 5.1.0 Teaching Spec in STYLE.md

- [ ] Update `book/STYLE.md` with the teaching template: five-layer environment narrative (dependency, build, deploy, run, effects), core-code principle-annotation rules, teacher-student dual-perspective sections, and the post-chapter exercise convention (`book/examples/<chapter>/exercises/` with reference implementations and verification status).

```

Full source: openspec/changes/generate-ebpf-book/tasks.md

## openspec/changes/generate-ebpf-book/specs/ebpf-book-manuscript/spec.md

- Source: openspec/changes/generate-ebpf-book/specs/ebpf-book-manuscript/spec.md
- Lines: 1-141
- SHA256: 01c701fe20f2ede93a2952b8bec7f491119d9c8686b9073d637ae9f2454c5ea3

[TRUNCATED]

```md
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

```

Full source: openspec/changes/generate-ebpf-book/specs/ebpf-book-manuscript/spec.md

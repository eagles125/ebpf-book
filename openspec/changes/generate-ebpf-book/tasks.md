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

- [x] Update `book/STYLE.md` with the teaching template: five-layer environment narrative (dependency, build, deploy, run, effects), core-code principle-annotation rules, teacher-student dual-perspective sections, and the post-chapter exercise convention (`book/examples/<chapter>/exercises/` with reference implementations and verification status).

### 5.1.1 Five-Layer Environment Narrative

- [x] 阐述依赖环境：内核版本与 BTF 要求、root/`CAP_BPF`/`CAP_NET_ADMIN` 权限、veth 与网络命名空间前置条件、依赖工具清单与检查方法。
- [x] 阐述编译环境：clang/LLVM、libbpf 头文件、Go 工具链与 cilium/ebpf 库的版本要求、安装与验证命令。
- [x] 阐述部署环境：ns-client/ns-fw/ns-server veth 实验拓扑设计、脚本化搭建与销毁步骤、安全警告（避免在物理网卡误挂 XDP）。
- [x] 阐述运行环境：XDP/TC 挂载与卸载方式、fwctl 控制面启动流程、规则与 NAT 下发步骤。
- [x] 阐述运行效果：每个关键阶段的预期输出、tcpdump 抓包对比、stats 与 conntrack 查询结果示例。

### 5.1.2 Standalone Example Assets with Annotated Code (Progressive Stages)

- [x] Generate chapter-level `book/examples/chapter-17/README.md` with the stage table (stage, datapath focus, learning outcome, attach point, output mechanism), learning path, safety warnings, and verification-status conventions.
- [x] Generate `book/examples/chapter-17/00-env/check-env.sh` with graded environment diagnostics (kernel/BTF, toolchain, privileges) that runs and reports meaningfully without root.
- [x] Generate `book/examples/chapter-17/01-xdp-drop/` with `xdp_drop.bpf.c`, C loader, Makefile, and README, with Chinese comments explaining XDP attach point, action return values, and the minimal load-verify-JIT-attach loop.
- [x] Generate `book/examples/chapter-17/02-parse-acl/` with staged `fw_common.h`, `acl.bpf.c` (Ethernet/IPv4/TCP/UDP parsing + hash-map exact rules + LPM-trie CIDR blacklist/whitelist), rule-provisioning loader, Makefile, and README, with Chinese comments explaining parser bounds checks, five-tuple normalization, and map design.
- [x] Generate `book/examples/chapter-17/03-conntrack/` with extended `fw_common.h`, `conntrack.bpf.c` (forward/reverse keys, state machine, LRU cap), dump-capable loader, Makefile, and README, with Chinese comments explaining state transitions and map capacity pitfalls.
- [x] Generate `book/examples/chapter-17/04-nat-tc/` with extended `fw_common.h`, `nat.bpf.c` (TC clsact, SNAT/DNAT rewrite, incremental checksum), NAT-provisioning loader, Makefile, and README, with Chinese comments explaining rewrite order and checksum algorithms.
- [x] Generate `book/examples/chapter-17/05-firewall/` complete system: `bpf/fw_common.h` (full union of stages 2-4), `bpf/fw_xdp.bpf.c`, `bpf/fw_tc.bpf.c`, `user/` Go control plane (go.mod with pinned cilium/ebpf, main/rules/maps/stats), combined Makefile, and README, with Chinese comments throughout.
- [x] Generate `book/examples/chapter-17/scripts/` with `setup-veth.sh` (topology setup/teardown + safety warnings), `bench-iperf.sh`, `bench-pktgen.sh`, and `verify-build.sh` (top-level compile-level verification across all stages and exercises).

### 5.1.3 Upgrade Chapter Prose into a Complete Teaching Chapter

- [x] 重写第 17 章正文：按五层环境阐述组织开篇，按五个渐进阶段推进叙事（每阶段四段式：教师视角引入→核心代码原理讲解→学生动手→检查点自测），将骨架式"计划/建议"表述替换为基于已落地实现的陈述式讲解。
- [x] 为报文解析与 verifier 边界检查写逐段原理讲解：为什么每次访问前必须 `data + offset <= data_end`，verifier 如何推理路径安全。
- [x] 为 ACL 匹配（阶段 2：hash 精确五元组 + LPM CIDR 黑白名单）写原理讲解与查找路径可视化。
- [x] 为 conntrack 状态机与 LRU 回收（阶段 3）写原理讲解：正反向 key、状态迁移、超时策略、数据面/控制面分工。
- [x] 为 SNAT/DNAT 与校验和增量更新（阶段 4）写原理讲解：NAT 映射表设计、改写顺序、checksum 增量算法。
- [x] 为 Go 控制面（阶段 5）写原理讲解：cilium/ebpf 加载、map pin、原子更新、CLI 命令实现。
- [x] 为压测写结果解读讲解：iperf/pktgen 命令、指标口径、常见失败模式与定位方法。

### 5.1.4 Teacher-Student Dual Perspective and Post-Chapter Exercises

- [x] 每个主要小节增加教师视角内容：讲解顺序设计、类比、常见误区与"读者容易在哪里卡住"的提醒。
- [x] 每个主要小节增加学生视角内容：动手实践清单、阶段性检查点、自测问题与答案提示。
- [x] 设计章末课后扩展任务（已定 4 项：IPv6 ACL 支持、规则持久化与重启恢复、Prometheus 指标导出、按端口限速）。
- [x] 为每项课后任务提供完整可编译的参考实现与实现思路说明，存放于 `book/examples/chapter-17/exercises/<nn>-<name>/`，练习 README 采用防剧透结构（任务与提示在前、参考实现思路独立成节），并标注验证状态。

### 5.1.5 Cross-Linking, Verification, and Review

- [x] Cross-link Chapter 17 prose to the standalone `book/examples/chapter-17/` stage directories and exercise assets after they are generated.
- [x] Run `book/examples/chapter-17/scripts/verify-build.sh` compile-level verification locally (all stages + exercises); fix any compile failures. Verify load/runtime behavior in a privileged Linux eBPF environment or mark each component with explicit verification status and reason.
- [x] Main-agent review: audit Chapter 17 against the teaching completeness requirement (five-layer environments, core-code annotations, dual perspective, exercises with reference implementations).

## 6. First-Batch Verification

- [x] Verify all expected Markdown files exist.
- [x] Verify generated files use Chinese prose and Markdown headings.
- [x] Verify Chapter 1 contains learning objectives, visualization, summary, and references.
- [x] Verify Chapter 9 skeleton covers Hello World, kprobe, tracepoint, map, and perf event.
- [x] Verify Chapter 17 skeleton covers XDP/TC, conntrack, ACL, NAT, checksum, control plane, and benchmarking.
- [x] Record any commands that could not be run because of sandbox or privilege limitations.

## 7. Future Batches

- [ ] Batch 2: Complete Chapters 2-4.
- [ ] Batch 3: Complete Chapters 5-8.
- [ ] Batch 4: Complete Chapters 9-14, including verified observability examples where environment allows, and the Chapter 9 teaching-standard retrofit tasks recorded in the Chapter 9 matrix.
- [ ] Batch 5: Complete Chapters 15-19, with special depth for Chapter 17.
- [ ] Batch 6: Complete Chapters 20-23.
- [ ] Batch 7: Complete Chapters 24-25 and appendices.
- [ ] Batch 8: Full-manuscript consistency pass, references pass, example verification pass, and final word-count audit.

## 8. Chapter Execution Workflow

- [ ] Treat each chapter as an independent delivery unit that may be assigned to a dedicated subagent.
- [ ] For each subagent-written chapter, require the subagent to edit only its assigned chapter file and matching `book/examples/<chapter>/` assets.
- [ ] For each subagent-written chapter, require a short report listing changed files, reference sources used, verification performed, and known limitations.
- [ ] Main agent reviews every completed chapter against `book/STYLE.md`, this OpenSpec spec, and the chapter-specific task list before marking it complete.
- [ ] Main agent verifies each completed chapter contains substantive Chinese prose, integrated examples, visualization, troubleshooting, summary, and references.
- [ ] Main agent verifies each practical chapter has standalone example assets or explicit pending tasks with verification status.
- [ ] Main agent verifies each complete practical chapter satisfies the teaching requirements: five-layer environment narrative, core-code principle annotations, teacher-student dual perspective, and post-chapter exercises with reference implementations.
- [ ] Main agent rejects outline-like chapters that only list bullets without explanation, examples, and output interpretation.

## 9. Full Chapter Task Matrix

### Chapter 1: eBPF Overview and History

- [x] Write full Chinese chapter prose covering classic BPF to eBPF, design goals, major use cases, and book navigation.
- [x] Compare eBPF with kernel modules and user-space-only observability.
- [x] Explain key kernel implementation paths including `kernel/bpf/`, `include/uapi/linux/bpf.h`, and `net/core/filter.c`.
- [x] Include an integrated minimal tracepoint example with BPF C code, userspace loader, Makefile, commands, expected output, and line-by-line explanation.
- [x] Include text visualization for load, verify, JIT, attach, and trigger flow.
- [x] Include troubleshooting, summary, and references.
- [ ] Main-agent review pass for final Chapter 1 prose depth after later chapters establish final style.
- [ ] During the final style pass, retrofit Chapter 1 with the teaching elements required by the updated spec: five-layer environment narrative for its orientation example, teacher-student dual perspective, and post-chapter exercises with reference implementations.

### Chapter 2: eBPF Architecture and Core Concepts

- [ ] Write full chapter prose explaining programs, maps, helpers, program types, attach points, context objects, event channels, and lifecycle.
- [ ] Explain instruction-set basics: 64-bit RISC model, registers, calling convention, and instruction encoding.
- [ ] Explain map types including hash, array, LRU hash, per-CPU maps, perf event array, ring buffer, LPM trie, and sockmap where relevant.
- [ ] Include an integrated example showing a simple tracing program with map interaction.
- [ ] Create standalone Chapter 2 examples under `book/examples/chapter-02/` if runnable code is included.
- [ ] Add kernel implementation analysis for `bpf()` syscall object creation, fd ownership, reference counting, and pinning.
- [ ] Add text visualization showing programs, maps, hooks, loader, and user-space control plane.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review against style, examples, terminology, and references.

### Chapter 3: Verifier and Safety Guarantees

- [ ] Write full chapter prose explaining verifier goals, control-flow graph construction, state exploration, state pruning, pointer types, bounds checks, stack limits, and helper constraints.
- [ ] Analyze `kernel/bpf/verifier.c`, `bpf_verifier_env`, `bpf_reg_state`, and the high-level verification pipeline.
- [ ] Include integrated examples that intentionally fail verification, then show corrected versions.
- [ ] Create standalone Chapter 3 verifier examples under `book/examples/chapter-03/`.
- [ ] Explain common errors involving unchecked map lookup, packet bounds, invalid pointer arithmetic, stack initialization, loops, and helper misuse.
- [ ] Add text visualization of verifier state branching and pruning.
- [ ] Add troubleshooting, summary, and references including kernel docs, LWN, and bpf mailing list discussions.
- [ ] Main-agent review for technical accuracy and clear “expected rejection” labels.

### Chapter 4: Program Types and Hook Points

- [ ] Write full chapter prose covering kprobe, kretprobe, tracepoint, raw tracepoint, fentry/fexit, uprobe, XDP, TC, cgroup skb, socket filter, sockops, sk_msg, LSM, and Netfilter where relevant.
- [ ] Explain `bpf_prog_type` versus `bpf_attach_type`, context structure differences, return values, and helper availability.
- [ ] Include at least two integrated hook examples from tracing, networking, or security categories.
- [ ] Create standalone Chapter 4 examples under `book/examples/chapter-04/`.
- [ ] Add text comparison table for trigger source, context, helper set, return behavior, stability, and typical use case.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review to ensure the chapter explains selection boundaries, not just hook taxonomy.

### Chapter 5: eBPF Subsystem Kernel Code Structure

- [ ] Write full chapter prose introducing eBPF subsystem source layout and ownership boundaries.
- [ ] Trace the load path from `BPF_PROG_LOAD` through `kernel/bpf/syscall.c`, verifier, JIT selection, and attach setup.
- [ ] Explain map create, lookup, update, delete, freeze, pin, and reference lifecycle at a source-code level.
- [ ] Include a source-reading walkthrough with commands for navigating a local Linux tree.
- [ ] Add text source tree and call-flow visualization.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for kernel path accuracy and conservative version language.

### Chapter 6: JIT Compilation and Performance Optimization

- [ ] Write full chapter prose explaining interpreter mode, JIT mode, architecture backends, and why JIT matters.
- [ ] Analyze x86_64 and arm64 JIT paths at a high level, including instruction lowering, prologue/epilogue, helper calls, and constant blinding where applicable.
- [ ] Include commands for inspecting translated and JITed output with `bpftool prog dump xlated` and `bpftool prog dump jited`.
- [ ] Include a performance comparison methodology for JIT enabled versus disabled, with clear safety notes.
- [ ] Add text visualization comparing interpreter and JIT execution paths.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review to distinguish verified results from expected methodology.

### Chapter 7: Helpers and Map Internals

- [ ] Write full chapter prose explaining helper dispatch, helper allowlists by program type, map operations, concurrency, and memory lifecycle.
- [ ] Analyze map ops, helper prototypes, RCU protection, spin locks, per-CPU storage, and memory accounting.
- [ ] Include integrated examples for hash map lookup/update, per-CPU counters, and event output through perf or ring buffer.
- [ ] Create standalone Chapter 7 examples under `book/examples/chapter-07/`.
- [ ] Compare ring buffer and perf buffer implementation and use cases.
- [ ] Add text visualization comparing hash, array, LRU, per-CPU, perf event array, and ring buffer.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for API names, helper names, and map type consistency.

### Chapter 8: BTF and CO-RE Principles

- [ ] Write full chapter prose explaining BTF goals, DWARF-to-BTF conversion, type IDs, field offsets, and CO-RE relocation.
- [ ] Analyze kernel BTF documentation and libbpf relocation behavior.
- [ ] Include integrated CO-RE example that reads a kernel structure field using `vmlinux.h`.
- [ ] Create standalone Chapter 8 examples under `book/examples/chapter-08/`.
- [ ] Explain `CONFIG_DEBUG_INFO_BTF`, `pahole`, `bpftool btf dump`, and missing-BTF fallback strategies.
- [ ] Add text visualization for compile-time BTF, object relocation records, runtime target BTF, and patched access.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for CO-RE boundary conditions and non-absolute portability language.

### Chapter 9: Observability Getting Started with ebpf.top

- [x] Rewrite Chapter 9 around all 12 ebpf.top BPF introductory-series articles.
- [x] Expand all “needs explanation” and “explanation focus” bullet lists into substantive explanations tied to examples.
- [x] Generate standalone Chapter 9 examples listed in Section 4.0.
- [x] Cross-link Chapter 9 prose to standalone examples after files are generated.
- [x] Verify examples in a privileged Linux eBPF environment or mark each one with explicit verification status.
- [x] Main-agent review after example assets are generated.

以下为 2026-09-21 教学标准重审（对照 spec 五条教学需求）确认的差距，安排在批次 4 第 9 章成稿时补齐：

- [ ] 教学标准改造第 9 章正文：将环境素材重组为五层环境阐述（依赖/编译/部署/运行/效果），把「计划代码/计划脚本」措辞替换为对已落地示例的逐段走读（含第 8 节占位代码补全）。
- [ ] 为第 9 章补齐学生侧环节：每大节增加动手实践清单、阶段性检查点与自测问题。
- [ ] 创建 `book/examples/chapter-09/exercises/`：不少于 3 项课后扩展任务，每项含完整可编译参考实现、防剧透结构 README（任务与提示在前、实现思路独立成节）与验证状态标注。
- [ ] 为 `book/examples/chapter-09/00-env/` 补充 README（check-env.sh 用法与输出解读）。
- [ ] 第 9 章教学改造后的 main-agent 审查（对照五条教学需求逐条核验）。

### Chapter 10: BCC Toolkit

- [ ] Write full chapter prose explaining BCC architecture, Python/Lua bindings, embedded C snippets, and runtime compilation.
- [ ] Cover installation and kernel/header requirements across major distributions.
- [ ] Include integrated tool walkthroughs for `execsnoop`, `opensnoop`, `biolatency`, `biosnoop`, `tcpconnect`, and `tcptracer`.
- [ ] Include a custom BCC Python program with explanation and expected output.
- [ ] Create standalone Chapter 10 examples under `book/examples/chapter-10/`.
- [ ] Add visualization of BCC script to compiled BPF program to kernel events to output.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for BCC/libbpf positioning and command copyability.

### Chapter 11: bpftrace

- [ ] Write full chapter prose explaining probes, predicates, actions, maps, aggregations, histograms, and scripts.
- [ ] Include one-liners for syscall latency, memory allocation tracing, short-lived process capture, and function timing.
- [ ] Include bpftrace scripts for latency heatmaps and duration distributions.
- [ ] Create standalone Chapter 11 scripts under `book/examples/chapter-11/`.
- [ ] Compare bpftrace with BCC and libbpf in terms of speed, portability, and production suitability.
- [ ] Add visualization from one-liner to generated BPF program to aggregation output.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for syntax accuracy and output interpretation.

### Chapter 12: System Performance Diagnosis

- [ ] Write full chapter prose covering CPU scheduler latency, run queue length, off-CPU time, memory leaks, page faults, VFS, and block I/O.
- [ ] Include integrated examples for CPU flame graphs, off-CPU flame graphs, memory allocation tracing, and I/O latency.
- [ ] Create standalone Chapter 12 scripts/examples under `book/examples/chapter-12/`.
- [ ] Explain storage path from syscall to VFS to filesystem to block layer to driver.
- [ ] Add visualization workflow for flame graph generation and interpretation.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for causal reasoning between symptoms, measurements, and conclusions.

### Chapter 13: Network Performance Observability and Diagnosis

- [ ] Write full chapter prose covering TCP retransmission, RTO, zero window, UDP drops, socket buffers, veth, bridge, and container network latency.
- [ ] Include integrated examples for TCP connect/retransmit tracing, UDP drop observation, and veth/bridge latency.
- [ ] Create standalone Chapter 13 examples under `book/examples/chapter-13/`.
- [ ] Explain sockops and sockmap observability and acceleration-related signals.
- [ ] Add packet-path visualization from NIC to driver, XDP, skb, TC, TCP/UDP, and userspace.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for network terminology and topology consistency.

### Chapter 14: Visualization Platforms and Monitoring Integration

- [ ] Write full chapter prose explaining eBPF metric exporters, logs, traces, event schemas, and dashboard integration.
- [ ] Include Prometheus exporter setup and metric design example.
- [ ] Include Grafana dashboard structure and panel interpretation.
- [ ] Include Cilium Hubble deployment and service flow topology walkthrough.
- [ ] Include Pixie quick-start notes and limitations.
- [ ] Create standalone Chapter 14 config/examples under `book/examples/chapter-14/`.
- [ ] Add visualization pipeline from kernel event to exporter to Prometheus to Grafana/Hubble/Pixie.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for environment prerequisites and no hidden cluster assumptions.

### Chapter 15: XDP Deep Dive

- [ ] Write full chapter prose explaining XDP execution model, driver/native/generic modes, actions, metadata, redirect, and AF_XDP.
- [ ] Include integrated examples for DDoS drop, load balancing, packet redirect, or AF_XDP path.
- [ ] Create standalone Chapter 15 examples under `book/examples/chapter-15/`.
- [ ] Include `xdp-loader` usage and safe attach/detach scripts.
- [ ] Include performance test methodology with pktgen or iperf.
- [ ] Add visualization of XDP in the receive path before skb allocation.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for NIC/kernel/permission requirements and safe networking warnings.

### Chapter 16: TC and eBPF

- [ ] Write full chapter prose explaining TC clsact, ingress/egress, qdisc, classifiers, actions, skb context, and XDP+TC cooperation.
- [ ] Include integrated examples for packet classification, policy routing, traffic shaping, or packet rewrite.
- [ ] Create standalone Chapter 16 examples under `book/examples/chapter-16/`.
- [ ] Include `tc qdisc` and `tc filter` attach/detach scripts.
- [ ] Add visualization of skb path through TC ingress and egress.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for clear XDP versus TC boundaries.

### Chapter 17: High-Performance eBPF Firewall

- [x] Write deep architecture skeleton for datapath/control-plane split, XDP/TC choice, conntrack, ACL, NAT, checksum, control plane, and benchmarks.
- [x] Upgrade Chapter 17 into a complete teaching chapter per Section 5.1: five-layer environment narrative, principle-annotated core code walkthroughs, teacher-student dual perspective, and post-chapter exercises with reference implementations.
- [x] Generate standalone Chapter 17 firewall assets listed in Section 5.1.
- [x] Verify firewall examples in isolated privileged network lab or mark each component with explicit verification status.
- [x] Cross-link Chapter 17 prose to standalone firewall files.
- [x] Main-agent review for safety warnings, NAT/checksum accuracy, and benchmark interpretation.

### Chapter 18: Cilium eBPF Networking

- [ ] Write full chapter prose covering Cilium architecture, identity, policy, L3/L4 load balancing, kube-proxy replacement, encryption, bandwidth manager, and Hubble.
- [ ] Explain Cilium's BPF program lifecycle and map management at a high level.
- [ ] Include integrated cluster walkthrough or architecture trace for one service request path.
- [ ] Create standalone Chapter 18 manifests/commands under `book/examples/chapter-18/`.
- [ ] Add visualization of Kubernetes resource to Cilium agent to BPF datapath to Hubble flow.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review to separate Cilium product behavior from generic eBPF behavior.

### Chapter 19: Advanced Network Acceleration

- [ ] Write full chapter prose covering sockmap, sk_msg, service mesh acceleration, AF_XDP, zero-copy, and SmartNIC-related context where appropriate.
- [ ] Include integrated example plan for one advanced acceleration path.
- [ ] Create standalone Chapter 19 examples under `book/examples/chapter-19/` where feasible.
- [ ] Explain applicability limits, kernel requirements, and deployment risks.
- [ ] Add end-to-end acceleration path visualization.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for conservative performance claims and clear prerequisites.

### Chapter 20: CO-RE

- [ ] Write full chapter prose explaining Compile Once - Run Everywhere, vmlinux.h, relocation kinds, field existence checks, and compatibility boundaries.
- [ ] Include integrated CO-RE skeleton generation and load example.
- [ ] Create standalone Chapter 20 examples under `book/examples/chapter-20/`.
- [ ] Explain how to inspect relocation information and diagnose CO-RE failures.
- [ ] Add visualization of one object file loading across multiple kernel BTF layouts.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for accurate CO-RE claims and BTF dependency handling.

### Chapter 21: libbpf Development Practice

- [ ] Write full chapter prose covering libbpf-bootstrap project layout, skeleton APIs, map operations, attach lifecycle, packaging, and distribution.
- [ ] Include integrated portable eBPF tool example.
- [ ] Create standalone Chapter 21 examples under `book/examples/chapter-21/`.
- [ ] Explain handling kernel-version differences through BTF and feature probing.
- [ ] Add visualization of BPF C, userspace loader, skeleton, object file, and runtime links.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for build reproducibility and API correctness.

### Chapter 22: eBPF and Go Interoperability

- [ ] Write full chapter prose covering `cilium/ebpf`, loading, map operations, attach APIs, event readers, and project layout.
- [ ] Include integrated Go-driven network observability microservice example.
- [ ] Create standalone Chapter 22 examples under `book/examples/chapter-22/`.
- [ ] Explain Go userspace plus C BPF kernel-side split and build pipeline.
- [ ] Add visualization of Go control plane to BPF object to kernel maps/events.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for Go module/build commands and capability requirements.

### Chapter 23: eBPF and Rust Interoperability

- [ ] Write full chapter prose covering Aya, libbpf-rs, Rust userspace, Rust eBPF options, and selection trade-offs.
- [ ] Include integrated Aya or libbpf-rs example with build/run commands.
- [ ] Create standalone Chapter 23 examples under `book/examples/chapter-23/`.
- [ ] Compare Go and Rust choices for control planes and kernel-side code.
- [ ] Add visualization of Rust crate, BPF object, maps, events, and kernel hooks.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for ecosystem currency and clear selection guidance.

### Chapter 24: Production Deployment and Security Hardening

- [ ] Write full chapter prose covering kernel parameters, capabilities, unprivileged BPF, JIT settings, signing/verification, deployment, hot upgrade, rollback, and container permissions.
- [ ] Include production readiness checklist and minimal privilege examples.
- [ ] Create standalone Chapter 24 checklist/config examples under `book/examples/chapter-24/`.
- [ ] Explain monitoring eBPF program overhead and safe upgrade strategy.
- [ ] Add deployment pipeline visualization from build to rollout to rollback.
- [ ] Add troubleshooting, summary, and references.
- [ ] Main-agent review for security rigor and distro/version caveats.

### Chapter 25: eBPF Frontiers and Roadmap

- [ ] Write full chapter prose covering eBPF on Windows, SmartNICs, PREEMPT_RT considerations, emerging program types, verifier evolution, and community roadmap.
- [ ] Clearly separate implemented facts, active development, and speculative future direction.
- [ ] Include a structured reading plan for following community updates.
- [ ] Add visualization of current capability landscape and future expansion areas.
- [ ] Add troubleshooting or “how to evaluate new eBPF features” guidance.
- [ ] Add summary and references to upstream roadmaps, mailing lists, docs, and project announcements.
- [ ] Main-agent review for dated claims and citation quality.

### Appendices

- [ ] Complete Appendix A instruction-set quick reference with instruction classes, register conventions, examples, and upstream-aligned terminology.
- [ ] Complete Appendix B command index with command purpose, typical output, and chapter cross-references.
- [ ] Complete Appendix C distro BTF guide with commands, decision flow, and conservative distro-specific caveats.
- [ ] Complete Appendix D reference link catalog with concrete pages, usage notes, and chapter cross-references.
- [ ] Main-agent review all appendices for consistency with chapters and source links.

## 10. Whole-Book Final Review

- [ ] Audit all chapters for substantive prose instead of outline-style notes.
- [ ] Audit all examples for code/prose integration, standalone files, build/run commands, expected output, and verification status.
- [ ] Audit all visualizations for relevance and readability.
- [ ] Audit all references for concrete source attribution.
- [ ] Audit terminology consistency across the whole book.
- [ ] Audit all chapter and example cross-links.
- [ ] Perform final word-count audit against the target full-manuscript size.

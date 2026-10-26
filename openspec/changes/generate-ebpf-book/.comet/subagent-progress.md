# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-10-25-chapter04-teaching.md（12 任务，第 4 章《eBPF 程序类型与钩子点深入》，Batch 2 收官章）— **build 进行中，停靠（2026-10-26，用户请求休息）**
- design-doc: docs/superpowers/specs/2026-10-25-chapter04-teaching-design.md（c9ae92e；章级分叉按推荐项采用，待用户复核）
- 上一章检查点：第 3 章全部收口（见 git 历史 a9a4a84 Park ch03 build session 与其后任务提交）
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-10-27，用户请求暂存）**：Task 1–6 已收口（实现+逐任务审查+计划勾选），Task 7–12 与终审未动
- HEAD = fcedbb5；工作树干净（正文 04-*.md = 23512 字符 Task 6 收口态）（实现+逐任务审查+计划勾选），全部审查通过；**Task 6 已提取 brief（`.superpowers/sdd/2026-10-25-chapter04-teaching/task-6-brief.md`），implementer 派发在启动前被取消——未产生任何改动**；Task 7–12 与终审未动
- HEAD = ab7119d；工作树干净
- SDD 台账权威文件：`.superpowers/sdd/2026-10-25-chapter04-teaching/progress.md`（Task 1–5 完成线、修复轮、回写、延后 Minor 全记录）
- 恢复方式：Task 6 重派**全新 implementer**（brief 已就绪；正文任务派发要点见台账与 ch03 会话先例——需给实现者 ch03/ch02 正文做文风先例、第 4 章 C7 标题逐字、机器实证事实清单）→ Task 7/8/9 → Task 10 exercises → Task 11 verify-build/交叉链接/篇幅终检（35–52k）→ Task 12 主审+tasks.md 勾选（Chapter 4 七条，Batch 2 随之收官）→ 最终完整审查 → 停靠

## 第 4 章任务台账

- Task 1 章级 README、00-env、.gitignore — done（8aa0600 + 修复轮 1 fc37d8b / 勾选 4766eaa）——修复项：check-env 权限分级改判 effective set（ch03 同款遗留问题的 ch04 侧修正）
- Task 2 01-multi-observe 同一事件五副眼镜 — done（c1db3f3 / 勾选 683299b）——2 处 loader 偏差获 reviewer 实证（skeleton 前缀 multi_observe_bpf、libbpf_set_print 回调形态），计划回写 8a2fd0b
- Task 3 02-xdp-tc 同一报文两种 ctx — done（ac73189 + 修复轮 1 ecfc807 / 勾选后 d5e77e3 前）——修复项：bpf_tc_detach 传填充 opts（NULL 是静默 no-op）；计划回写已同步
- Task 4 画廊前四类与总览 — done（5b65477 / 勾选后 d5e77e3 前）——Approved 0 修复
- Task 5 画廊后四类 — done（0d0d4c4 + 修复轮 1 d7cf474/3587c90 / 勾选 ab7119d）——实现者实证 bpf_nf_ctx{state, skb}（计划原文误写 in/out），计划回写 e27dcc2
- Task 6 正文 目标/导读/一/二 — done（637cea3+修复轮 bb83f7e / 回写 d5d7c33 / 勾选 82acc80）——修复项：1.2/1.3 sockops 返回值补「教学约定+内核消费面」保守标注（tcp_call_bpf 实证）；计划回写两笔[d5d7c33 C3 口径留痕、fcedbb5 Task 7 card 定位笔误 3.5/3.6→3.4/3.5]；5 Minor 挂账 Task 11 清扫
- Task 7 正文 三、类型画廊走读 — **brief 就绪待重派全新 implementer**（2026-10-27 停靠：实现者运行 45 分钟后被停靠中止于提交前——未提交、未写报告；三节草稿瞬时态 43453 字符已快照存档 task-7-draft-parked.md 供参考[非真相源，超 12–15k 预算]，工作树已恢复干净；派发要点[十四类口径互认/sockops 3.12 保守标注/card 六段/勘误后 card 定位 3.4 fentry 3.5 fexit]已注入该 brief 派发模板，重派时按 Task 6 同款完整注入）
- Task 8 正文 四、实战 — pending
- Task 9 正文 可视化/排错/课后/总结/参考 — pending
- Task 10 exercises 两项（01-type-selection、02-type-mismatch）— pending
- Task 11 verify-build.sh/全量编译/交叉链接/篇幅终检（35–52k）— pending
- Task 12 主审 + tasks.md「Chapter 4」七条勾选（Batch 2 收官）— pending
- 最终完整审查 — pending

## Task 11 顺带核对项（各任务延后 Minor 清扫，来自台账）

1. Task 3 Minors：xdp_tc.c 缺 `<errno.h>`（传递包含）；SIGTERM 未注册（仅 SIGINT）
2. Task 4 Minors：总览表「8 目录统一编译级已通过」瞬时超前（Task 5 落地后应已为真——核实即可）；uprobe README「偏移取符号地址」宜改「文件偏移（PIE/共享库下与虚拟地址不同）」；目录列反引号渲染（纯排版，可不改）
3. Task 5 Minors：07-lsm README「load 后即进入裁决链」宜补「（skeleton attach 自动）」限定；sockops key 未初始化声明（brief 逐字，逻辑安全，不改）
4. Task 11 自带：章级 README 验证摘要回填（实测工具版本 + PASS/FAIL）

## 已确认决策（不得重新询问）

- 四项工作方式（2026-09-25）：isolation=current（master）、build_mode=subagent-driven-development（subagent_dispatch=confirmed）、tdd_mode=direct、review_mode=thorough
- 验证策略：编译级真实验证 + 运行级一律「预期输出示例 / 运行级待验证」（无 root，unprivileged_bpf_disabled=2）
- 第 4 章特有：画廊 8 类与练习程序必须 clang 编译通过（compile-only）；两锚点全链；fentry/kprobe 签名以本机 BTF 解析为准（do_sys_openat2 实证签名；do_unlinkat 本机内联缺席；vfs_unlink 签名漂移实证进教学）；BPF_PROG/BPF_KPROBE/BPF_KRETPROBE 在 bpf_tracing.h；TC_ACT_OK/NF_ACCEPT 需手工 define；bpf_nf_ctx{state, skb}（BTF 实证）；raw_tp syscall 类形状 (regs, id) 以内核源码为准
- 篇幅：第 4 章 35–52k（设计 §3.3 分节预算）
- 计划回写先例：Task 2/3/5 的计划笔误均按「回写对齐落地形态 + 留痕」处理（8a2fd0b / e27dcc2 等）
- 本平台 Agent 工具无 model 参数，按可用工具派发（先例：ch02/ch03 会话）

## 恢复指引

1. 读本检查点 + SDD 台账，对照 plan 未勾选任务与 git log 恢复；以 git log + plan checkbox + 台账为准
2. Task 7 重派全新 implementer（brief 已就绪；Task 8/9/10 brief 已预提取在 SDD 工作区）后按任务序推进至 Task 12 + 终审
3. 全部完成 + 终审通过后：停靠（全书 tasks.md 未勾满，`comet guard build --apply` 按设计阻塞，同第 2/3 章）

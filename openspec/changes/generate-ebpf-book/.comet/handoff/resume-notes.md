# Resume Notes — generate-ebpf-book

- 暂存时间：2026-09-21（第二次暂存：第 9 章教学标准重审进行中被用户暂存）
- 状态机：`phase: build`，`plan: null`，`build_pause: null`，`isolation/build_mode/tdd_mode: null`，`review_mode: standard`（全局配置默认）
- 本文件是给「后续拉起」会话的直达断点记录；状态机本身已持久化于 `.comet.yaml`，恢复路由自动生效。
- **当前有两项待办，拉起后按序处理：① 第 9 章教学标准重审（最新请求，先做）；② build 计划创建（先前的暂存点）**

## 恢复方式

用户任一说法（如「请拉起当前项目任务」）→ `/comet` → 路由 `comet-classic` → Step 0/1 读取状态 → phase=build 且 plan 未建、工作方式未配置 → 进入 `/comet-build`。**但在此之前先完成第 9 章重审**：读取 `openspec/changes/generate-ebpf-book/.comet/handoff/ch9-teaching-audit.md`（审查证据与初步结论已固化），按其「剩余动作」补齐抽查 → 交付最终审查报告 → 用户选择补救排期（A 现在升级 / B 并入批次 4 / C 仅记录）→ 按选择更新 tasks.md（注意 handoff hash 失效处理，见「已知运行时坑」）→ 再继续下方 build 流程。

## 本次会话已完成（2026-09-20 ~ 09-21）

1. **open 阶段（补齐恢复）**：初始化缺失的 `.comet.yaml`；按用户审视反馈重整 OpenSpec 产物——spec.md 新增 5 条教学需求（五层环境阐述/核心代码原理注释/教师-学生双视角/课后扩展任务与参考实现/第 17 章完整教学章节，正文含 MUST 关键字以过校验）；design.md 新增决策 5–8；proposal.md 增补；tasks.md §5.1 重组（全书 269→289 任务，已完成 65）。用户已确认产物。
2. **design 阶段**：生成 design handoff；brainstorming 三项澄清 + 方案 A（渐进搭建式）均经用户逐项确认；Design Doc 落盘 `docs/superpowers/specs/2026-09-20-chapter17-teaching-design.md`；tasks.md 回写为渐进阶段结构；design guard 全过，phase=build。
3. **build 阶段**：入口验证通过；Step 1 的计划创建 subagent 已派发但被用户取消（暂存请求）——**计划尚未创建**。
4. 提交：`dd80cc3`（教学规范+设计阶段产物）、`910a82f`（gitignore 本地 comet 选择状态）。工作区干净。

### 第二次会话（2026-09-21，已暂存）

5. 用户请求「按照此标准重新审查第 9 章」→ 审查证据收集约 80% 后被暂存：正文全读、示例资产抽查（README 结构/验证状态/源码中文注释/无 exercises 目录）、5 条教学需求逐条初步判定（详见 `ch9-teaching-audit.md`）。**最终审查报告尚未交付**。

## 下一步精确动作（拉起后从这里开始）

### 0. 完成第 9 章教学标准重审（最新用户请求，先于 build）

详见 `ch9-teaching-audit.md`（同目录）：证据已收集约 80%（正文全读、示例资产抽查、双视角/练习缺失确认），剩余 = 补齐 00-env 与 03/05/06/07 README 抽查 + tasks.md Chapter 9 矩阵现状 → 产出最终审查报告 → 用 AskUserQuestion 请用户选择补救排期（A 现在升级为 §4.2 任务组并入本次 build / B 并入批次 4 / C 仅记录结论）→ 按选择更新 tasks.md。

### 1. /comet-build Step 1 — 派发计划创建 subagent

用 Agent 工具（general-purpose）派发，prompt 要点（可直接复用）：

> 你是实施计划专家，工作目录 /work/ai/ebpf-book（分支 master，工作区干净）。
> ① 用 Skill 工具加载 `writing-plans` 技能（必做），ARGUMENTS 注明 `Language: zh-CN`；
> ② 读取 `docs/superpowers/specs/2026-09-20-chapter17-teaching-design.md`、`openspec/changes/generate-ebpf-book/tasks.md` §5.1（5.1.0–5.1.5 共 28 项未勾选任务）与 Chapter 17 矩阵节、`book/STYLE.md`、`book/examples/chapter-09/README.md` 及 `01-hello-world/README.md`（既有渐进模式参考）；
> ③ 计划保存至 `docs/superpowers/plans/2026-09-21-chapter17-teaching.md`（日期用当天），frontmatter 必含 `change: generate-ebpf-book`、`design-doc: docs/superpowers/specs/2026-09-20-chapter17-teaching-design.md`、`base-ref: <git rev-parse HEAD>`；
> ④ 任务顺序：5.1.0 STYLE.md 教学规范 → 5.1.2 示例资产（00-env→05-firewall 逐阶段）→ scripts/ + verify-build.sh → 5.1.3 章节正文重写 → 5.1.1 五层环境阐述并入 → 5.1.4 课后练习 → 5.1.5 交叉链接/编译级验证/主审；每任务注明对应 tasks.md 唯一文本、涉及文件、验收口径；编译级验证为硬性验收；章节目标 40–60k 字符，四段式节奏；
> ⑤ 只建计划不实施；返回计划文件绝对路径。

subagent 失败则按 comet-build 降级：主 session 内联加载 writing-plans 建计划。

### 2. /comet-build Step 2 — 联合决策（用户阻塞点）

`comet state set generate-ebpf-book plan <路径>` 后，一次性向用户展示计划摘要 + 暂停选项 + 全部工作方式配置（工作区隔离 current/branch/worktree、执行方式 executing-plans/subagent-driven-development、TDD tdd/direct、审查 off/standard/thorough；选 branch 时同场确认分支名，full workflow 推荐名 `feature/YYYYMMDD/generate-ebpf-book`）。用户确认后写入对应 state 字段再开始执行。

## 用户已确认的关键决策（不得重新询问）

- 验证策略：**编译级真实验证 + 运行效果写「预期输出示例」+「运行级待验证」标注**（本机无 root，`unprivileged_bpf_disabled=2`，sudo 需密码）。
- 课后参考实现：**完整可编译**（每练习独立目录+完整源码+思路讲解+验证状态）。
- 示例组织：**渐进式阶段**（00-env → 01-xdp-drop → 02-parse-acl[含 LPM CIDR] → 03-conntrack → 04-nat-tc → 05-firewall[Go 控制面]）。
- 设计方案：**方案 A 渐进搭建式**；四段式教学节奏（教师视角引入→代码原理→学生动手→检查点）；4 项练习=IPv6 ACL/规则持久化/Prometheus 导出/按端口限速；练习 README 防剧透结构。
- 教学规范全书适用：写入 `book/STYLE.md` 约束后续实战章节。

## 环境事实（勿重复探测）

内核 7.0.0-30-generic；`/sys/kernel/btf/vmlinux` 存在；clang 18.1.3（Ubuntu）；bpftool 在 `/usr/sbin`；Go 1.27.1（snap）；**无** iperf3；uid 1000 无特权，CapEff=0；`kernel.unprivileged_bpf_disabled=2`。→ 编译级验证可行，加载/挂载/veth/压测不可行。

## 已知运行时坑（如再遇到）

- **handoff stale 检查**：design 阶段修改 proposal/design/tasks/specs 后，`comet handoff <name> design --write` 会被入口 stale 检查拦截（recorded hash ≠ 当前 hash）。解除：`comet handoff <name> --hash-only` 取当前 hash → `comet state set <name> handoff_hash <hash>` → 删除 `.comet/handoff/design-context.{md,json}`（强制走完整重生成）→ 重跑 `comet handoff <name> design --write`。guard 错误信息本身就指引重生成，属运行时缺口而非违规操作。
- spec 需求正文必须含 `SHALL`/`MUST` 关键字（中文写「必须（MUST）」可过 `openspec validate`）。
- `comet state set` 不接受 `handoff_hash null`（必须是 hex）；根目录 `.comet/` 是本地选择状态，已 gitignore。

## 执行注意（来自 comet-build 硬约束）

- full workflow 离开 build 前：`isolation`（非 null）、`build_mode`、`tdd_mode` 必须写入；`review_mode` 已有默认 standard。
- 执行中任务失败/构建失败 → 先加载 `systematic-debugging` 技能再修复。
- executing-plans + review_mode standard → 全部任务完成后、`comet guard build --apply` 前须加载 `requesting-code-review` 做一次全 change 轻量审查。
- 每完成一任务：勾选 tasks.md 对应条目并提交。

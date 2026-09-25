# Resume Notes — generate-ebpf-book

- 暂存时间：2026-09-25（第五次更新：build 执行中暂存——Task 1 已实现提交，任务级审查未派发）
- 状态机：`phase: build`，`plan: docs/superpowers/plans/2026-09-22-chapter17-teaching.md`，`build_pause: null`，**4 项工作方式配置已全部写入：`isolation: current`（master）、`build_mode: subagent-driven-development`（`subagent_dispatch: confirmed`）、`tdd_mode: direct`、`review_mode: thorough`**。无待决策项。
- 精确断点：Task 1（STYLE.md 教学模板）implementer 已完成 `10831f2`，**处于 task-review 阶段（0/2 轮）**；精确协调状态见 `.comet/subagent-progress.md`（权威恢复依据）。

## 恢复方式（最快路径）

`/comet` → comet-classic → 入口验证 `phase: build` 通过后进 `/comet-build`：**所有配置已写入，无 Step 2 联合决策阻塞**，直接按 `build_mode: subagent-driven-development` 恢复执行：

1. 加载 Superpowers `subagent-driven-development` 技能 + `comet/reference/subagent-dispatch.md`（Comet 扩展）。
2. 读 `openspec/changes/generate-ebpf-book/.comet/subagent-progress.md`：Task 1 阶段 = task-review（0/2 轮），不得重置、不得重复派发 implementer。
3. 用 `/home/zzw/.zcode/skills/subagent-driven-development/scripts/review-package docs/superpowers/plans/2026-09-22-chapter17-teaching.md aeaf23c 10831f2` 生成 diff 文件，派发全新后台 task reviewer（spec + quality，交接产物：brief/report/review-package 三路径 + 全局约束块）。
4. reviewer 通过 → 勾选 plan Task 1 checkbox → `comet state task-checkoff <plan> "Task 1: STYLE.md 新增「实战章节教学模板」"` → 台账记 complete → 立即连续派发 Task 2–15（**任务间禁止暂停**；thorough 下每任务派 reviewer，审查-修复最多 2 轮，2 轮未过才 BLOCKED 停给用户）。
5. OpenSpec tasks.md 勾选由 Task 15 统一执行（计划全局约束 10），每任务只勾 plan checkbox。

## 本次会话已完成（2026-09-25）

1. 从 plan-ready 决策点恢复：用户经结构化问答确认 4 项配置（**隔离=当前分支 master、执行=subagent 驱动、TDD=direct、审查=thorough**——thorough 由用户选定，非默认 standard），`comet state set` 全部写入，随 `aeaf23c` 提交。
2. 预检计划冲突扫描：无阻塞性冲突。备注一条：章节级 README 的 verify-build 编译声明按计划为终稿口径（Task 8/14 兑现），reviewer 若在 Task 2 提出按计划原文裁决。
3. SDD 工作区/台账建立（`.superpowers/sdd/2026-09-22-chapter17-teaching/`，git-ignored）；Comet 检查点 `.comet/subagent-progress.md` 建立。
4. Task 1 implementer（后台）完成：`10831f2` docs(ch17): STYLE.md 新增实战章节教学模板（五层环境/四段式/练习约定）。验证 grep 通过（4 个三级标题、章节头唯一、逐字一致），无风险信号，工作区干净。brief/report 在 SDD 工作区。reviewer 未派发（用户请求暂存）。

## 用户已确认的关键决策（不得重新询问）

- **build 工作方式（2026-09-25 新增）**：隔离=当前分支 master；执行=subagent-driven-development；TDD=direct；审查=**thorough**。
- 验证策略：**编译级真实验证 + 运行效果写「预期输出示例」+「运行级待验证」标注**（本机无 root，`unprivileged_bpf_disabled=2`，sudo 需密码）。
- 课后参考实现：**完整可编译**（每练习独立目录+完整源码+思路讲解+验证状态）。
- 示例组织：**渐进式阶段**（00-env → 01-xdp-drop → 02-parse-acl[含 LPM CIDR] → 03-conntrack → 04-nat-tc → 05-firewall[Go 控制面]）。
- 设计方案：**方案 A 渐进搭建式**；四段式教学节奏（教师视角引入→代码原理→学生动手→检查点）；4 项练习=IPv6 ACL/规则持久化/Prometheus 导出/按端口限速；练习 README 防剧透结构。
- 教学规范全书适用：写入 `book/STYLE.md` 约束后续实战章节。

## 环境事实（勿重复探测）

内核 7.0.0-31-generic；`/sys/kernel/btf/vmlinux` 存在；clang 18.1.3（Ubuntu）；bpftool 7.7.0 在 `/usr/sbin`；libbpf-dev 1.3.0；Go 1.27.1（snap）；**无** iperf3；uid 1000 无特权，CapEff=0；`kernel.unprivileged_bpf_disabled=2`。→ 编译级验证可行，加载/挂载/veth/压测不可行。注意：本平台 Agent 工具无 model 参数，无法显式选档，按可用工具派发即可。

## 已知运行时坑（如再遇到）

- **handoff stale 检查**：design 阶段修改 proposal/design/tasks/specs 后，`comet handoff <name> design --write` 会被入口 stale 检查拦截（recorded hash ≠ 当前 hash）。解除：`comet handoff <name> --hash-only` 取当前 hash → `comet state set <name> handoff_hash <hash>` → 删除 `.comet/handoff/design-context.{md,json}`（强制走完整重生成）→ 重跑 `comet handoff <name> design --write`。guard 错误信息本身就指引重生成，属运行时缺口而非违规操作。
- spec 需求正文必须含 `SHALL`/`MUST` 关键字（中文写「必须（MUST）」可过 `openspec validate`）。
- `comet state set` 不接受 `handoff_hash null`（必须是 hex）；根目录 `.comet/` 是本地选择状态，已 gitignore。
- comet reference 文件实际路径：`/home/zzw/.agents/skills/comet/reference/`（decision-point.md、subagent-dispatch.md 等；`/home/zzw/.zcode/skills/comet-build` 下无 reference 目录）。

## 执行注意（来自 comet-build 硬约束）

- 每完成一任务：按 thorough 完成审查验收 → 勾 plan checkbox → `comet state task-checkoff` → 提交 → 立即派发下一任务。
- 执行中任务失败/构建失败 → 先加载 `systematic-debugging` 技能再修复。
- 全部任务完成、final review（完整）通过后：`comet guard generate-ebpf-book build --apply` 推进 phase → `comet state next` 衔接 verify。
- 工作区当前干净（HEAD=`10831f2`）；`.superpowers/` 为 git-ignored 草稿区，台账在 `progress.md`。

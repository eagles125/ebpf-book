# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-09-22-chapter17-teaching.md（15 任务）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- **停靠（2026-09-25，第 17 章 plan 全部完成，守卫按设计阻塞）**

## 当前状态

- 第 17 章 plan 的 15 个任务全部实现、逐任务审查通过并勾选（final review 修复波 2a5a535 后无遗留开放项）
- 最终完整审查（681b67a..2a5a535，53 提交）：0 Critical / 0 Important；fix-before-delivery 4 项已修复并 scoped 复审 4/4 ADDRESSED
- 硬性验收记录：verify-build.sh 全量 PASS=9 WARN=0 FAIL=0（Task 14 真实运行，记录在 book/examples/chapter-17/README.md 验证状态说明区 + comet record-check）；正文 wc -m=69380（裁定口径 60–70k）
- tasks.md：§5.1（28 条）+ Chapter 17 矩阵（5 条）共 33 条已统一勾选（eb9ca21）；change 整体 98/296
- **`comet guard build --apply` 按设计 BLOCKED**：tasks.md 全部勾选条件未满足——本 change 覆盖整本书（Batch 2–8 其余章节未开工），§7 规定每章为独立交付单元。build→verify 的推进须等全书任务完成，或经用户决策改变 change 组织方式
- SDD 工作区 .superpowers/sdd/2026-09-22-chapter17-teaching/ 已按 Finish 删除（git 历史为记录）

## 已完成任务（第 17 章 plan）

- Task 15 主审与 tasks.md 勾选（`eb9ca21`；勾选 `66795e9`；Approved）
- Task 14 交叉链接与全量验证（`7bf6784`+终审修复波 `2a5a535`；勾选 `2927b44`；Approved）
- Task 13 控制面练习与总览（`ee43034`+修复 `591a808`；勾选 `693ca8a`；修复轮 1 通过）
- Task 12 数据面练习（`cfb8ebf`；勾选 `d1664c6`；Approved）
- Task 11 双视角与课后任务（`d491876`；勾选 `7370c85`；Approved）
- Task 10 五层环境并入正文（`4903d13`+修复 `e8d02c5`；勾选 `a12d704`；修复轮 1 通过）
- Task 9 章节正文重写（`9dd9c38`；勾选 `01e412e`；Approved）
- Task 8 scripts/ 脚本集（`b7c5cfa`+修复 `62d1def`；勾选 `2f4038a`；修复轮 1 通过）
- Task 7 阶段 5 完整系统（`a166c9e`+修复 `b5a131f`；勾选 `59dffef`；修复轮 1 通过）
- Task 6 阶段 4 NAT（`373be2a`；plan 同步 `4dcf242`；勾选 `6da92ed`；Approved）
- Task 5 阶段 3 conntrack（`72e9857`+修复 `812f4b1`；勾选 `98296d2`；修复轮 1 通过）
- Task 4 阶段 2 parse-acl（`a7099d9`；勾选 `56239a1`；Approved）
- Task 3 阶段 1 xdp-drop（`bebe4ea`+裁定 `e1196bb`；勾选 `7096d3f`；Approved）
- Task 2 章级 README 与 00-env（`deee667`+修复 `630f4d2`；勾选 `6583ea4`；修复轮 1 通过）
- Task 1 STYLE.md 教学模板（`10831f2`；勾选 `7390c5e`；Approved）

## 恢复指引

1. 用户决策下一章/下一批次（Batch 2–4、6–8 任一），为该章走 design → plan → build（同第 17 章模式：新设计文档 + 新 plan + build session）
2. 全书 tasks.md 勾满后：`comet guard build --apply` → verify → archive
3. Batch 8 检查单（来自本章审查）：全书引号统一（「」/“”）、STYLE.md 具名脚本泛化、正文计划逐字文本的裸 map（16/247/1308/1490/1493 行）

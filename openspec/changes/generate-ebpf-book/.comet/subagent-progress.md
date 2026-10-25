# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-10-22-chapter03-teaching.md（11 任务，第 3 章《验证器与安全保障》）
- design-doc: docs/superpowers/specs/2026-10-22-chapter03-teaching-design.md（ff88829；三项章级分叉按推荐项采用，待用户复核）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- 上一章检查点（第 2 章，2026-09-26 停靠）见 git 历史：34efa8d Park ch02 build session
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-10-22，用户请求暂存）**：Task 1–8 已收口（实现+审查+勾选）；**Task 9 实现 b9a057c 已提交、计划回写 02eb94a 已提交，处于 task-review 阶段（reviewer 已派发一次、被暂存停止，未返回结论——恢复时用现成材料重新派发全新 reviewer，不得重复派发 implementer）**
- 恢复方式：Task 9 reviewer 重派（三路径材料已在磁盘：task-9-brief.md / task-9-report.md / review-38beeee..02eb94a.diff，BASE=38beeee HEAD=02eb94a）→ 通过后勾选 Task 9 → Task 10（verify-build/交叉链接/篇幅终检+控制器上限裁定+终检清单清扫）→ Task 11（主审+tasks.md 勾选）→ 最终完整审查 → 停靠
- SDD 台账权威文件：`.superpowers/sdd/2026-10-22-chapter03-teaching/progress.md`

## 第 3 章任务台账

- Task 1 章级 README、00-env、.gitignore — done（8c8ba39 / 勾选 6b5b69a）
- Task 2 01-pkt-parse 正面锚点示例 — done（fef465c / 勾选 33edc7f）
- Task 3 画廊前三对 — done（1a120e3+c8e8fa8 / 勾选 ba95f0d）
- Task 4 画廊后四对与总览 — done（7adb1e5+5e8a450 / 勾选 882390c）
- Task 5 正文 目标/导读/一/二 — done（c162f24+567bc4e / 勾选 80d8382）
- Task 6 正文 三、失败画廊走读 — done（145716e / 勾选 40bd080）
- Task 7 正文 四、实战 — done（dfe7edf+07487fb / 勾选 621baa6）
- Task 8 正文 可视化/排错/课后/总结/参考 — done（1d3d242 / 勾选 38beeee）
- Task 9 练习参考实现与 exercises 总览 — **task-review 阶段**（实现 b9a057c：broken volatile 适配[预授权,三处 offending 指令 objdump 证实存活]+fixed/reproduce 逐字+防剧透七段 README+C9 总览；计划回写 02eb94a）
- Task 10 verify-build/交叉链接/篇幅终检 — pending
- Task 11 主审与 tasks.md 勾选（收口）— pending
- 最终完整审查 — pending

## Task 10 终检清单（控制器裁定与清扫项，来自各任务延后 Minor）

1. **篇幅上限裁定**：章当前 61073 字符 vs 终检口径 35–48k——超长主因计划强制的逐字代码贴入（三节 23.6k/四节 10.5k,结构性低估），按 ch02 先例（45k→48k 控制裁定 3d148e6）由控制器裁定新上限并同步计划/设计,或压缩重复叙述不删原理
2. 01-pkt-parse/README.md:42 同源 ping 措辞手术（Task 7 裁定遗留,README 属 Task 2 资产）
3. 学生动手「逐个加载 bad」对 3.4 补特权豁免括注（Task 6 Minor 1）
4. 2.6「CAP_BPF 一类」与 3.4「CAP_PERFMON 一类」统一（Task 6 Minor 2,2.6 宜补 perfmon 档）
5. 「最坏 40 亿次」与 README 05「近 43 亿」口径同步（Task 6 Minor 4,可选）
6. 图 1 解读「探索步数省一半」→「省去臂 2 在 J 之后的整段重复探索」（Task 8 Minor 1）
7. 排错②「2.7 预告的 log_level 细目」出处改 5.2（Task 8 Minor 2）
8. XDP gloss 归位至章内首现导读（Task 7 Minor ②）
9. 学习目标首条全角括号嵌套排版（Task 5 Minor,可选）
10. 972 行深一层日志保真（寄存器状态行 off= 语义/基址 r1 实测首行 R1）——「认措辞不认编号」对冲已并存,终检酌定（Task 7 re-review 移交）
11. 交叉链接核对后回填章级 README 验证摘要（Task 10 Step 2 自带）

## Task 11 顺带核对项

- 全书台账:全章 wc -m 终值与裁定口径记录入 book/examples/chapter-03/README.md;tasks.md「Chapter 3」8 条统一勾选;预告⑨勾稽表兑现确认（Task 8 已落）

## 已确认决策（不得重新询问）

- 四项工作方式（2026-09-25）：isolation=current（master）、build_mode=subagent-driven-development（subagent_dispatch=confirmed）、tdd_mode=direct、review_mode=thorough
- 验证策略：编译级真实验证 + 运行效果「预期输出示例」+「运行级待验证」（无 root，unprivileged_bpf_disabled=2）
- 第 3 章特有（2026-10-22）：画廊 bad/fixed 与练习 broken 必须 clang 编译通过；预期 verifier 日志按 5.10+ 内核源码校正基准（04 特权豁免/05 主形态 infinite loop detected/06 unknown func）+ 版本差异口径
- 检查点第 1 问修正版保留（第三层检查后无解引用,实测裁定）;实战节动手观察用 UDP 流量（ping 是 ICMP 不计数）
- 本平台 Agent 工具无 model 参数，按可用工具派发（先例：ch02 会话）

## 恢复指引

1. 读本检查点 + SDD 台账，对照 plan 未勾选任务与 git log 恢复；检查点若与台账不一致，以 git log + plan checkbox + 台账为准重写本文件
2. 全部任务完成 + 最终完整审查通过后：停靠（全书 tasks.md 未勾满，`comet guard build --apply` 按设计阻塞，同第 2 章）

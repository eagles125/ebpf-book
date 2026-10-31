# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-10-31-chapter05-teaching.md（13 任务，第 5 章《eBPF 子系统内核代码结构》，Batch 3 首章）— **全部完成并停靠**
- design-doc: docs/superpowers/specs/2026-10-31-chapter05-teaching-design.md（499db88；走读树=/work/linux/linux-6.6.78、方案 A 纯走读资产——设计确认点两次提问未获答复，按 ch03 先例推荐项默认采用、已标注待复核可推翻，主审 APPROVE 视为事实确认）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- 上一章检查点：第 4 章全部收口（git 历史 972f353 Park ch04 build session 与其后任务提交）
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-11-01）：第 5 章 build 全部收口**——13 任务实现+逐任务审查+勾选全毕；修复轮 2 次（T6 行号勘误/区6 文件数、T12 练习② README 防剧透收敛）均 scoped 复审全 ADDRESSED；主审（兼全章终审，29 提交）**APPROVE 零 Critical/Important，无开放项**
- HEAD = 收口提交链尾（6d3dc2e 勾选矩阵 / d490404 plan 勾选 T13）；tasks.md「Chapter 5」7/7 已勾选（6d3dc2e）；全书 change 仍未勾满（ch06–08 为 Batch 3 余量），`comet guard build --apply` 按设计阻塞（同第 2/3/4 章）
- 硬性验收终值：符号机检 `verify-citations.sh` TOTAL=43 PASS=43 FAIL=0；章正文 `wc -m`=48525 ∈ [35000, 50000]；交叉链接 21/1；禁语 grep 零命中；行号引用约 110 处对 v6.6.78 树零错误（主审实测）；两练习机检 10/10、6/6 全绿
- SDD 工作区 `.superpowers/sdd/2026-10-31-chapter05-teaching/` 已按 Finish 删除（本检查点 + git 历史为记录）

## 第 5 章任务台账（全部 done）

- Task 1 00-env 自检+章级 README 骨架 — done（82b916a / 3 Minor 记账 / 勾选 d986232）
- Task 2 01-source-map（citations.txt 43 锚点全实测） — done（8564a56 / 2 Minor / 勾选 efcd1c3；基线勘误：syscall.c 三处 include bpf_types.h :68/:2044/:2968、bpf_prog_types[] :2039）
- Task 3 verify-citations.sh 符号机检（43/43 全绿+负路径） — done（cf2c88d / 3 Minor 口径内 / 勾选 50362ce）
- Task 4 02-load-path 切片器六站 — done（d79e9bd / 1 Minor（报告文本） / 勾选 5c9b09b）
- Task 5 03-map-ops 切片器六站 — done（c12d517 / 2 Minor / 勾选 80ce215；基线校准：array_index_nospec :1165、sysctl :60）
- Task 6 正文目标导读+一节 — done（a08a473 + 修复轮 1 3ece5a6[白名单 :2624-2627 勘误/区6=14/首现前移/口径统一] / 勾选 0df5ded）
- Task 7 正文二节（加载路径 9991） — done（0e9ec00 / 4 Minor→T11 清扫 / 勾选 fd2d07a）
- Task 8 正文三节（映射生命周期 8993） — done（fe4ce31 / 3 Minor→T11 / 勾选 be44ed4）
- Task 9 正文四节（实战五层 7917） — done（7043fdb / 1 Minor→T11 / 勾选 383c700）
- Task 10 可视化/排错/课后/总结/参考 — done（acd5db6 / 裁决=分节预算服从全章 44–48k 硬区间 / 勾选 7236d4a）
- Task 11 交叉链接/机检终跑/篇幅终检/清扫 — done（3d54463 / 清扫 9 项 8 done+1 合理保留 / 勾选 7236d4a 后 d490404 前）
- Task 12 两项练习参考实现 — done（12fbbf6 + 修复轮 1 551ec45[README② 前半防剧透收敛] / 勾选 b38b62f）
- Task 13 主审 + tasks.md 勾选 — done（APPROVE / 6d3dc2e + d490404）

## 延后项登记（移交后续章节/统稿，非第 5 章开放项）

1. 章 :551-552 四节标题前缺空行（全章唯一排版不一致）——后续章节排版清扫顺手补
2. trace-map-ops.sh 站 2 校准注释「map 侧/prog 侧」（脚本注释非正文）——下次维护统一
3. 01-source-map/README「44 行映射/link」合并表述 vs 正文 1.2 分列精确口径——可保留，统稿可选统一
4. errno 表「idr_alloc 满」族名简写（实际 idr_alloc_cyclic:2105，2.5 已精确）——可保留
5. 章级示例 README:50 ASCII 括注（README 自身风格）——可保留
6. 机检脚本三分支口径（纯空白字段/缺失不输出汇总行/不可读归未命中）——契约口径内，CI 化时再议
7. 第 4 章延后项 1–10 继续有效（见 git 历史 972f353 版本检查点），其中「统稿/后续批次」标注项在后续章节或统稿批次处理

## 流程建议（来自 ch05 会话，供未来章节采用）

- **计划任务头下加任务级 checkbox 行**（`- [ ] Task N: <题>`）：comet task-checkoff 按该行匹配，无此行会报「found 0」（ch05 会话以 1850414 补齐对齐）
- **行号引用纪律实测闭环有效**：正文约 110 处「文件:行号」全部树上实测零错误——brief 必须写明「凡你新引入的行号必须 grep/sed 实测，不得沿用 brief 未验证数字」；T6 白名单错引（:2620→:2624-2627）证明 brief 给的参考行号也可能漂，实现者实测+审查者复测双闸缺一不可
- **防剧透检查要成为审查 prompt 的硬性 grep 项**（T12 泄题由审查抓出：前半 :4808×2+函数名点名）
- 资产先行（T1–T5 资产→T6–T10 正文）顺序正确：正文引用的行号全部来自已实跑校准的切片器/机检资产

## 已确认决策（不得重新询问）

- 四项工作方式（2026-09-25）：isolation=current（master）、build_mode=subagent-driven-development（subagent_dispatch=confirmed）、tdd_mode=direct、review_mode=thorough
- 验证策略：编译级（ch05=符号机检+脚本实跑）真实验证 + 运行级一律「预期输出示例 / 运行级待验证」（无 root，unprivileged_bpf_disabled=2）
- 第 5 章特有：走读树=/work/linux/linux-6.6.78（本机唯一完整树；运行内核 7.0.0-38 的 headers 树无 .c）；机检按符号不按行号、行号标注「（v6.6.78）」；方案 A 纯走读资产（无 BPF 程序资产，实物对照=bpftool 只读 introspection）；「可编译」章级等价物=机检绿+bash -n+实跑断言
- 篇幅：第 5 章 35–50k（终值 48525）；分节预算服从章级硬区间（T10 裁决：可视化 45 文件列举为数据硬下限）
- 设计确认点先例：结构化提问未获答复→按推荐项默认采用并在设计文档显著标注「待用户复核，可推翻」（ch03/ch05 两先例）
- 本平台 Agent 工具无 model 参数，按可用工具派发（先例：ch02/ch03/ch04/ch05 会话）

## 恢复指引

1. 用户决策下一章/下一批次（Batch 3 余量=第 6/7/8 章），为该章走 design → plan → build（同第 2/3/4/5 章模式）；上述「延后项登记」中标注「统稿/后续批次」的项在该章或统稿批次处理；第 6 章可回收本章二节「深挖留第 6 章」（JIT）的桥
2. 全书 tasks.md 勾满后：`comet guard build --apply` → verify → archive

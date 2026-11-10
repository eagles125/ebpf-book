# Comet 检查点 — generate-ebpf-book(ch09 改造,Batch 4 首项)

- Plan: docs/superpowers/plans/2026-11-08-chapter09-retrofit-teaching.md(7 任务,base c37b5f6;免设计文档——用户 2026-11-08 拍板「先看总体规划」后「请继续」按建议推进;comet plan 指针已切换)
- 工作方式(用户已确认,不得重新询问):isolation=current(master)、build_mode=subagent-driven-development(dispatch confirmed)、tdd_mode=direct、review_mode=thorough(每任务 reviewer,审查-修复最多 2 轮)
- 当前 Task 4「十二节学生侧环节(动手清单/检查点/自测)+T3 两 minors 顺手清偿」:阶段=task-review(2026-11-10 implementer 回报 DONE 无风险信号,提交 a9691bd+8759fe5,wc -m 29450→40818 ∈[30k,45k];审查包 review-e11bc3d..8759fe5.diff,reviewer 后台已派发,修复轮预算 thorough 2 轮;implementer 越出指派的两处自选对齐——§5 --pid→-p 正文修正、引号「」→“ ”全量转换——已随 diff 交 reviewer 判定)
- Task 3 完成(7ba8084+6eebee4,恢复:实现已提交但报告因会话中断未写出,未重复派发;review 一次通过 Approved C0/I0/m3,§8 五段摘录与示例源码逐字一致、输出块与真实 printf 字节一致;minors mT3-1/mT3-2 归 T4 顺手清偿,mT3-3 观察项留台账;plan 勾选 T1–T3 task-checkoff PASS,39a410c)
- Task 2 完成(95f5ffc+77b4659,review 一次通过 Approved C0/I0/m3;「计划」21→0;§3 五层与 ch02/05/08 同构;全章 26556)
- Task 1 完成(8399d30+5268817,review 一次通过 Approved C0/I0/m5;计划外发现:nfctl Go 控制面从未编译过,授权最小修复已兑现;verify-build 10/10+负路径 exit 1;ch09 示例此前「已编译级验证」声明由此坐实)
- Batch 4 总体规划已过目(2026-11-08):ch09 改造(7 任务)→ ch10 BCC → ch11 bpftrace → ch12 性能诊断 → ch13 网络 → ch14 可视化平台;每章开工点=设计拍板;ch07 无 sockmap 内容(ch13 该节全新,勿虚构回指);ch10-14 编译级锚点已探明(bpftrace -d 无 root 可用/BCC bpfcc-tools 已装/promtool 缺、无 k8s 集群)
- ⚠️ 编排文件保护:本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰,不得对其执行 git restore/checkout/stash

## 已完成批次与章节

- **Batch 3(第 5–8 章)验收完成**(2026-11-08,c37b5f6,已 push):批次验证报告 docs/superpowers/reports/2026-11-08-generate-ebpf-book-batch3-verify.md;ch08 SDD 台账 .superpowers/sdd/2026-11-02-chapter08-teaching/progress.md(14 任务全 ledger+延后项)
- **第 8 章**停靠 7dfde35:14/14 任务、终审 Approved、章 54987 ∈ [40k,55k]、机检 38/38、练习 9/9+6/6;⚠️台账勘误(未来触及 ch08 锚点者注意):verifier.c "future proofing"=:15753、bpf_check_uarg_tail_zero=:15754、注释区间=:15749-15751、verifier %8=:15771(旧记均偏 1);切片器汇总缺行实为「结论」行(tail -5 口径)
- ch07 已收口(park 检查点 de78ef1);ch02–06 状态见 git 历史与各自验收记录;ch17/ch01/ch09-骨架为早期批次(1-4 部分完成,9 章骨架待本批次改造)
- ⚠️ 推送状态:origin/master = c37b5f6;自 c37b5f6 起新增提交待下次 push(用户已授权 push,沿用 gh 凭据助手 `git -c credential.helper='!gh auth git-credential' push origin master`)

# Comet 检查点 — generate-ebpf-book(ch08 章级循环)

- Plan: docs/superpowers/plans/2026-11-02-chapter08-teaching.md(14 任务,base 8a11de8)
- 工作方式(用户已确认,不得重新询问):isolation=current(master)、build_mode=subagent-driven-development(dispatch confirmed)、tdd_mode=direct、review_mode=thorough(每任务 reviewer,审查-修复最多 2 轮)
- 设计文档: docs/superpowers/specs/2026-11-02-chapter08-teaching-design.md(方案 A 六资产+40–55k 五段,用户 2026-11-02 拍板)
- Task 1 完成(a04ed82)、Task 2 完成(02162ad,review PASS,3 minors 延后)、Task 3 完成(34ae8dc,review PASS,5 minors 延后)、Task 4 完成(07ef5af,review PASS,2 minors 延后)、Task 5 完成(113feb3+修复 ca2cee2,1 轮修复后 review PASS,3 minors 延后)
- 当前 Task 12「正文——五、实战五层:一条 BPF_CORE_READ 的完整旅程+可视化:CO-RE 全链与两引擎对照」:阶段=implementing(BASE=219bd1b;上次会话的后台 implementer 随会话中断丢失、零产出(无 task-12-report.md),2026-11-08 恢复会话重新派发,报告将写 task-12-report.md;注入:13 种 kind 口径对图①、本机活值现测现标(7.0.0-42)、加载补丁层预期口径、独立示例链接全部资产、双占位替换(五节+可视化))
- Task 11 完成(f9b1148+修复 25b53bf,1 轮修复 1 Important 全 ADDRESSED;5+1 minors 延后;勾选提交 219bd1b)
- Task 10 完成(db348d2,review 一次通过:spec ✅/质量 Approved,0C/0I/2 minors 延后;勾选提交 0d1a801;⚠️审查发现本机已重启 7.0.0-42,task_struct ID 快照值已漂移,已记台账)
- Task 9 完成(6083508+修复 222dce0,1 轮修复 2 Important 全 ADDRESSED;6+1 minors 延后;勾选提交 233a544;'my_type'→'demo_dev' 口径修正已认)
- Task 8 完成(3fe62e9+修复 f80ce28,1 轮修复::164 task_struct 表述消歧,复查 ADDRESSED;2 minors 延后;勾选提交 9b799a8;C7 计数笔误协调侧已修正 4c042c0)
- Task 7 完成(3793eb8+修复 eb3eb6f,1 轮修复:gen_loader 锚点 :970→:971 实测修正+留痕如实化,复查 ADDRESSED;3 minors 延后;勾选提交 c7c267b;⚠️实跑证据以 implementer 两轮报告为凭、复查者核实自洽,主会话已核锚点前提)
- Task 6 完成(a7e0139,review 一次通过:spec ✅/质量 Approved,0C/0I/3 minors 延后;⚠️实跑证据已由主会话正负路径复跑核销;勾选提交 0d2b5cd)
- ch07 已收口(park 检查点 de78ef1);ch06 状态见 git 历史

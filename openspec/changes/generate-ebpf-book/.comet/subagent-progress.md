# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-11-01-chapter07-teaching.md（13 任务，第 7 章《辅助函数与映射的内部实现》，Batch 3 第三章）— **全部完成并停靠**
- design-doc: docs/superpowers/specs/2026-11-01-chapter07-teaching-design.md（c0b14e7；运行章双轨方案——走读轨同 ch05/06 契约，运行轨三示例 libbpf-bootstrap 编译级验证/运行级预期）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- 上一章检查点：第 6 章全部收口（git 历史 cc15746 勾选矩阵 / 5b8100c 主审修正；ch05 park 见 6a161eb）
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-11-02）：第 7 章 build 全部收口**——13 任务实现+实跑验证+勾选全毕；主审（行号抽检 20+ 处、API/类型名 17+13 全检、双清单）修正 2 处已回写（gen_lookup :722→:719、user ringbuf helper 名→bpf_user_ringbuf_drain :745）；**无 Critical/Important 开放项**
- HEAD = cc15746（chore(ch07) 勾选矩阵）；tasks.md「Chapter 7」8/8 已勾选；全书 change 余 ch08，`comet guard build --apply` 按设计阻塞
- 硬性验收终值：符号机检 TOTAL=44 PASS=44 FAIL=0（负路径 exit 1）；章正文 wc -m=35093 ∈ [35000, 50000]（压线）；交叉链接 18/1（appendix B/C）；禁语 grep 零命中；两切片器 5/4 站实跑全绿；两练习机检 6/6、8/8 全绿；三示例 make 全链零告警（vmlinux.h 生成告警以 -Wno-missing-declarations 抑制，仅 03 需要）
- **未推送 GitHub**：本地领先 origin/master 自 2a53ae0 之后全部 ch06 T6–T13 与 ch07 全部（18+ 提交），等用户明确指示再 push
- SDD 工作区无（本会话为直连执行，无 .superpowers/sdd 工作区）

## 第 7 章任务台账（全部 done，无独立审查轮——实现者实测+主审终审双闸）

- 设计/计划 — done（c0b14e7 / 38c4c0e）
- Task 1 00-env 自检+章级 README 骨架 — done（a303512；走读级+编译级 PASS 实跑）
- Task 2 01-hash-lookup-update+citations.txt（44 锚点一次全绿） — done（18ac5c5；make 全链、multiarch 头与 skel 符号名两处现场修正）
- Task 3 verify-citations.sh — done（3d44235）
- Task 4 04-helper-dispatch 切片器五站 — done（b565f19；站 2 抽段 diff 逐字一致）
- Task 5 05-map-internals 切片器六站 — done（fe208c5；站 3 抽段 diff 逐字一致）
- Task 6 02-percpu-counter — done（19ebae5；一次零告警）
- Task 7 03-event-output 双变体 — done（e5d6a6f；vmlinux.h 告警定位与旗标抑制、未用参数三轮清零）
- Task 8 目标导读+一节 — done（16c2c81 + 扩写 kfunc 反例/patch_call_args/家族表；call imm 实证 85/1/2）
- Task 9 二节 — done（730a7a5；贴码三段 diff 逐字一致，1145→1146 空行勘误）
- Task 10 三节 — done（5a1ad4d；LRU 三链/记账结构先核后写）
- Task 11 四节+可视化 — done（6262514；perf/ringbuf 贴码 diff 逐字一致）
- Task 12 排错/课后/总结/参考+终检 — done（4c6ebcc + b54276b；篇幅压线补强六处+五处扩写后过 35k）
- Task 13 两练习+主审+勾选 — done（a7dfa8e / 5b8100c / cc15746）

## 延后项登记（移交统稿/后续批次，非第 7 章开放项）

1. 一节 6842/二节 6520/三节 4259 低于分节预算（章级 35093 达标）——统稿可选按预算补强
2. 03-event-output 的 -Wno-missing-declarations 旗标：bpftool 对 7.0.0-38 内核个别结构翻译残缺（7 处），内核/工具链更新后可移除复测
3. 运行级验证（三示例加载/attach/读数/事件输出）待 root 档环境，全章按预期口径标注——未来有实验机可补「实测对照」
4. ch05 延后项 1–7 继续有效（见 git 历史 6a161eb 版本检查点）
5. ch06 未做 park 检查点（13 任务收口即被 ch07 会话接续）——ch06 状态以 git 历史为准（16c2c81 前全链）

## 已确认决策（不得重新询问）


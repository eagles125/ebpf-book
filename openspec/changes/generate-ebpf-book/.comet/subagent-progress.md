# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-10-22-chapter03-teaching.md（11 任务，第 3 章《验证器与安全保障》）— **全部完成并停靠**
- design-doc: docs/superpowers/specs/2026-10-22-chapter03-teaching-design.md（ff88829；篇幅口径 35–63k 由裁定 83e13d2 同步）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- 上一章检查点：第 2 章 34efa8d；第 3 章过程停靠 adec2ce（Task 9 review in flight，已在本会话解除）
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-10-25）：第 3 章 build 全部收口**——11 任务实现+逐任务审查+勾选全毕；最终完整审查（With fixes）→ 修复波 1（03b0de0+bc3ea19）→ scoped 复审全 ADDRESSED → 微修复 ab8c75c → scoped 复审 clean，**无开放项**
- HEAD = ab8c75c；tasks.md「Chapter 3」8/8 已勾选（7334f4d）；全书 change 仍未勾满，`comet guard build --apply` 按设计阻塞（同第 2 章）
- 硬性验收终值：`verify-build.sh` PASS=11 FAIL=0（03b0de0 修复波复跑；微修复仅涉 README 未触代码）；章正文 `wc -m`=62341 ∈ [35000, 63000]（裁定 83e13d2，计划/设计已同步）；交叉链接 grep 23/1；预期被拒标注 8/8；章↔示例 15 处引文逐字节一致
- SDD 工作区 `.superpowers/sdd/2026-10-22-chapter03-teaching/` 已按 Finish 删除（本检查点 + git 历史为记录）

## 第 3 章任务台账（全部 done）

- Task 1 章级 README、00-env、.gitignore — done（8c8ba39 / 勾选 6b5b69a）
- Task 2 01-pkt-parse 正面锚点示例 — done（fef465c / 勾选 33edc7f）
- Task 3 画廊前三对 — done（1a120e3+c8e8fa8 / 勾选 ba95f0d）
- Task 4 画廊后四对与总览 — done（7adb1e5+5e8a450 / 勾选 882390c）
- Task 5 正文 目标/导读/一/二 — done（c162f24+567bc4e / 勾选 80d8382）
- Task 6 正文 三、失败画廊走读 — done（145716e / 勾选 40bd080）
- Task 7 正文 四、实战 — done（dfe7edf+07487fb / 勾选 621baa6）
- Task 8 正文 可视化/排错/课后/总结/参考 — done（1d3d242 / 勾选 38beeee）
- Task 9 练习参考实现与 exercises 总览 — done（b9a057c+计划回写 02eb94a / 审查通过 / 勾选 28677d6）——broken 补 volatile 偏离经 reviewer 技术裁定成立
- Task 10 verify-build/交叉链接/篇幅终检 — done（0b37165 / 审查通过 / 勾选 fe94a8a）——含控制器清扫 13 项（终检清单 11 项中 5 项已同步关闭、1 项裁定不动，余全落地）
- Task 11 主审与 tasks.md 勾选 — done（7334f4d / 审查通过 / 勾选 dfce129）——四组审计全绿，8/8 勾选
- 最终完整审查 — done（With fixes → 修复波 03b0de0+bc3ea19 → 复审 ADDRESSED → 微修复 ab8c75c → 复审 clean）

## 延后项登记（移交全书批次/统稿，非第 3 章开放项）

1. **章 1.3 四步矩阵 code block step ②「拒绝：返回 -EACCES」**——计划逐字块；紧邻对冲句已纠正（-EACCES/-EINVAL 三分类），矩阵块本身留统稿轮与 ch02 预告⑥同源措辞一并校准
2. check-env「具备 CAP_BPF」capsh bounding-set 误报——5.1 已解读兜底；未来章节脚本可改判 effective set
3. 章 :1017 附近「整段免验，省去…整段重复探索」语义重复（裁定措辞残留）——统稿轮可修剪前半句
4. 01-pkt-parse/README.md「…都会让计数增长」的「都」失去旧主语——微瑕
5. reproduce.bpf.c:20「有符号字段」措辞（描述 __s32 转换本身，reviewer 判可留）
6. C4 模板 `.DELETE_ON_ERROR`、-Wall、ARCH sed 覆盖面——未来章节统一升级
7. 「最坏 40 亿次」与 2^32−1≈43 亿的取整口径——两处已一致（40 亿），统稿轮酌定精确化
8. 教学简化记录：锚点未校验 ip->ihl；signal() 而非 sigaction——plan-mandated，保持
9. ch02 :894「if (!p) 让它证明指针非空」与 ch02 5.2 防御式框架轻微张力——ch02 范围，统稿轮勘酌

## 流程建议（来自终审，供未来章节采用）

- 按内核源码校正预期日志/errno 的 gate 必须覆盖**每一条**契约日志行与每一个 errno/行为断言，而非只查被点名的（本次 03 大小写与 errno 三分类恰是漏网项）
- 终检加入常规项：`grep -rn "C[1-9]"`（plan 内部契约编号不得泄漏到学生可见文本；vmlinux.h 生成物排除）

## 已确认决策（不得重新询问）

- 四项工作方式（2026-09-25）：isolation=current（master）、build_mode=subagent-driven-development（subagent_dispatch=confirmed）、tdd_mode=direct、review_mode=thorough
- 验证策略：编译级真实验证 + 运行效果「预期输出示例」+「运行级待验证」（无 root，unprivileged_bpf_disabled=2）
- 第 3 章特有：画廊 bad/fixed 与练习 broken 必须 clang 编译通过；预期 verifier 日志按 5.10+ 内核源码校正基准 + 版本差异口径；**04 特权豁免带版本限定（6.3+ 放行栈读；5.10–6.2 一律拒绝）**；03 关键行小写 prohibited；errno 三分类（-EACCES/-EINVAL，判别以有无 verifier 日志为准）
- 篇幅：第 3 章 35–63k（裁定 83e13d2，ch02 先例同型）；分节预算按讲解文字口径，不含逐字代码贴入体量
- 检查点第 1 问修正版保留；实战节动手观察用 UDP 流量（ping 是 ICMP 不计数）
- 本平台 Agent 工具无 model 参数，按可用工具派发（先例：ch02 会话）

## 恢复指引

1. 用户决策下一章/下一批次（Batch 2/4/6/8 等任一），为该章走 design → plan → build（同第 2/3 章模式）；上述「延后项登记」中标注「未来章节/统稿」的项在该章或统稿批次处理
2. 全书 tasks.md 勾满后：`comet guard build --apply` → verify → archive

---
change: generate-ebpf-book
design-doc: none(第 9 章教学改造无新设计——教学标准由 change design.md 五条需求与 ch05–08 成稿先例确立,矩阵 5 项为既定标准的执行)
base-ref: c37b5f6
---

# 第 9 章教学标准改造(build 阶段)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 9 章《eBPF 可观测性入门:基于 ebpf.top 入门系列》按教学标准改造为 30–45k 字符(wc -m)的完整教学章节,兑现 tasks.md 矩阵中 2026-09-21 教学标准重审确认的 5 项差距(253–257 行):①环境素材重组为五层环境阐述并把「计划代码/计划脚本」替换为对已落地示例的逐段走读(含第 8 节占位代码补全)②每大节补学生侧环节(动手实践清单/阶段性检查点/自测问题)③创建 `book/examples/chapter-09/exercises/`(≥3 项,完整可编译参考实现+防剧透结构 README+验证状态标注)④`00-env/` 补 README ⑤改造后 main-agent 审查(对照五条教学需求逐条核验)。**顺带清偿:补 `book/examples/chapter-09/scripts/verify-build.sh`**(目录现为空;同 ch02–04 口径)。

**现状基线:** 正文 20467 字符、12 大节(对应 ebpf.top 12 篇)+ 横向对比/可视化(已有 12 篇能力递进图)/排错/验收清单/总结/参考;9 个示例 + 00-env 已在库并经改写期编译级验证;已知改造点:§3 :111「计划脚本:」内联 bash 草稿、§8 :333 一带「kprobe 版本计划读取:」注释占位块。

**工作方式(用户已确认,不得重新询问):** isolation=current(master)、build_mode=subagent-driven-development(dispatch confirmed)、tdd_mode=direct、review_mode=thorough(每任务 reviewer,审查-修复最多 2 轮)。

**硬口径(全任务遵守):**
- 走读树 /work/linux/linux-6.6.78(本章正文基本不引内核行号;示例为 libbpf-bootstrap 用户态项目,锚点在示例源码);运行档:本机无 root、unprivileged_bpf_disabled=2——一切加载运行=「预期输出示例 / 运行级待验证」,编译级以 make 实跑为证;
- 正文与实物同一坐标:所有「逐段走读」必须引用示例库真实源码与真实实跑输出(00-env/check-env.sh 可无 root 实跑);
- 禁语:新增内容零「已验证通过/验证通过」、零「计划代码/计划脚本/待补」类占位措辞;「预期输出示例/运行级待验证/方法论级」三标注按实况在场;
- 篇幅:全章 wc -m ∈ [30000, 45000],分节服从章级;每任务落笔前后 wc -m 留痕;
- 编排保护:`.comet/**`、`.comet.yaml`、plan 勾选、tasks.md 勾选均为协调者专管,subagent 不触碰。

---

### Task 1: 资产前置——00-env README + verify-build.sh

- [x] Task 1: 资产前置——00-env README + verify-build.sh

**Files:** Create `book/examples/chapter-09/00-env/README.md`、`book/examples/chapter-09/scripts/verify-build.sh`(可执行)

- [x] **Step 1:** `00-env/README.md`——check-env.sh 用法与输出解读(五段? 按脚本实跑输出逐段解读;「本机实况,其他内核数值不同」口径;无 root 限制声明)
- [x] **Step 2:** `scripts/verify-build.sh`——同 ch02–04 口径:逐示例 `make` 编译级验证(00-env 跳过 make、09-netfilter-go 注意内核 6.6+ 判定),输出 PASS/FAIL 汇总,FAIL≠0 exit 1;实跑全绿留痕(含负路径 exit 1 证据)
- [x] **Step 3:** 提交 `feat(ch09): 00-env README 与 verify-build 机检脚本`

### Task 2: 正文 §3 五层环境改造(「计划脚本」清零)

- [x] Task 2: 正文 §3 五层环境改造(「计划脚本」清零)

**Files:** Modify `book/chapters/09-observability-getting-started-ebpf-top.md`

- [x] **Step 1:** §3「eBPF 开发环境搭建」按五层环境阐述重组(依赖/编译/部署/运行/效果):删除 :111「计划脚本:」内联草稿,替换为对 `00-env/check-env.sh` 的逐段走读(真实源码注解+本机实跑真实输出摘录,console 标注语言);发行版命令按「方法论级/预期档」如实标注
- [x] **Step 2:** 全章 grep「计划」零命中(§3 之外如有同病一并清偿,逐处登记);篇幅留痕
- [x] **Step 3:** 提交 `docs(ch09): 三节五层环境改造(计划脚本替换为逐段走读)`

### Task 3: 正文 §8 占位代码补全 + 全章占位清零

- [x] Task 3: 正文 §8 占位代码补全 + 全章占位清零

**Files:** Modify 同文件

- [x] **Step 1:** §8「kprobe 版本计划读取:」注释占位块 → `05-open-file-kprobe` 真实源码的逐段走读(do_sys_openat2 取参、bpf_probe_read_user_str 读取、ring buffer 提交,与 §8 现有指针/时序讲解逐段对齐)
- [x] **Step 2:** 全章占位扫描(占位/待补/TODO/略/「完整代码见」悬空),逐处清偿或如实改指真实文件;§12 Netfilter 与 09-netfilter-go 同口径核对(其代码若完整则只对齐描述)
- [x] **Step 3:** 提交 `docs(ch09): 八节 kprobe 代码补全与全章占位清零`

### Task 4: 学生侧环节——每大节动手清单/检查点/自测

- [x] Task 4: 学生侧环节——每大节动手实践清单、阶段性检查点与自测问题

**Files:** Modify 同文件

- [x] **Step 1:** 12 大节逐节补齐:动手实践清单(对应示例目录+具体命令,可复跑口径)+「检查点」3 问(自测后看提示,格式同 ch08 :668 先例)+自测问题;§1/§2(纯概念节)可用思辨式自测
- [x] **Step 2:** 篇幅自查(全章 ≤45k 约束下的压缩预案)
- [x] **Step 3:** 提交 `docs(ch09): 十二节学生侧环节(动手清单/检查点/自测)`

### Task 5: exercises ≥3 项 + 机检扩展

- [ ] Task 5: exercises ≥3 项 + 机检扩展

**Files:** Create `book/examples/chapter-09/exercises/{01…03…}/`(每项 README + 参考实现源码 + Makefile 按需);Modify `book/examples/chapter-09/scripts/verify-build.sh`(扩展覆盖)

- [ ] **Step 1:** ≥3 项练习:①改写类(如 02-pid-filter 变体:按 comm 过滤)②新增类(如给 05 加 minimal/failed 字段——呼应 §9/10 返回值)③综合类(如 ring buffer→perf buffer 迁移对照,呼应 §7/9);每项完整可编译参考实现+防剧透结构 README(任务与提示在前、思路独立成节)+验证状态标注(编译级已验证/预期档)
- [ ] **Step 2:** verify-build.sh 扩展覆盖 exercises(编译级断言);正路径全 PASS、负路径 exit 1 实跑留痕
- [ ] **Step 3:** 提交 `feat(ch09): 三项课后练习参考实现与机检扩展`

### Task 6: 教学标准自查+篇幅+链接+README 回填

- [ ] Task 6: 教学标准自查+篇幅+链接+README 回填

**Files:** Modify 正文;Modify `book/examples/chapter-09/README.md`

- [ ] **Step 1:** 五条教学需求逐条自查表(实质中文叙述/集成示例/可视化/排错/总结参考——五层环境+原理注解+双视角+练习参考实现);章级 README 落地状态列与 exercises 行回填实况;交叉链接全量解析(0 断链)
- [ ] **Step 2:** 篇幅终查(wc -m ∈ [30000,45000])+ 措辞终扫(禁语零命中;三标注在场)
- [ ] **Step 3:** 提交 `docs(ch09): 教学标准自查与 README 回填`

### Task 7: main-agent 主审(五条教学需求逐条)+ tasks.md 勾选(收口)

- [ ] Task 7: main-agent 主审(五条教学需求逐条)+ tasks.md 勾选(收口)

**Files:** Modify 正文/README(主审回修);Modify `openspec/changes/generate-ebpf-book/tasks.md`(勾选 253–257 行 5 项)

- [ ] **Step 1:** 主审专项:五条教学需求逐条核验(矩阵第 5 项)+ 行号/锚点抽检 ≥10 处 + 与 ch05–08 成稿先例的格式一致性;问题回修(回修净增守 45k 上限)
- [ ] **Step 2:** 提交 `feat(ch09): 主审专项回修(教学需求逐条核验)`(如有回修);**勾选提交** `docs(ch09): 勾选第 9 章教学改造矩阵任务`(tasks.md 5 项;确认 `.comet/**` 未入库)

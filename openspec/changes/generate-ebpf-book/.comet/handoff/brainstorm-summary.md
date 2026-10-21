# Brainstorm Summary

- Change: generate-ebpf-book
- Date: 2026-10-22
- 主题:第 3 章《验证器与安全保障》章级教学设计(章级循环位于全书 change 的 build 阶段内,同第 2/17 章模式;ch17 期检查点见 git 历史)

## 确认的技术方案

第 3 章定位为**机制章(完整章节模板)+ 完整实战化**(失败画廊示例资产触发 STYLE.md 实战章节教学模板四条规范),正文目标 35–48k 字符(与第 2 章同口径)。示例资产采用**画廊式 + 正面锚点**:`book/examples/chapter-03/` 下含 00-env、01-pkt-parse(能过验证的最小 XDP 报文解析锚点程序)、02-fail-gallery/(七类失败模式,每类 bad.bpf.c + fixed.bpf.c + README)、scripts/verify-build.sh、exercises/(2 项练习)。

三项章级分叉(结构化提问未获答复,按推荐项采用,**待用户复核,可推翻**):

1. **示例组织 = 画廊式 + 正面锚点**:01-pkt-parse 为 verify-build 必绿目标;02-fail-gallery/ 每失败模式一个子目录,章级 README 一张总览表索引全部 bad/fixed 对。
2. **失败覆盖 = 6+1**:tasks.md 六项(①未检查 map lookup ②报文边界 ③无效指针运算 ④栈未初始化 ⑤循环可终止性 ⑥helper 误用)+ 第 ⑦ 项「任意内核指针必须走 probe 系 helper」(回收 ch02 明确预告)。
3. **练习 = 2 项**:①修复挑战(画廊外新坏程序,参考实现=修复版+逐条报错讲解)②日志诊断(按 verifier 日志片段反推原因,参考实现=触发程序+逐行解读)。与 ch02 两项体量对齐。

## 关键取舍与风险

- **ch02 预告回收清单**(第 3 章正文必须兑现):验证发生在加载路径哪一步(-EACCES)、「编译成功≠能加载」、helper 白名单与 ctx 申报访问 vs probe 界限、数组越界运行时定界(3.2)、跳转偏移与可终止性、verifier 日志排错指引。设计已将其编入正文结构与画廊条目。
- verifier 日志文本随内核版本漂移 → 以 5.10+ 稳定格式与 Documentation/bpf/verifier.rst 措辞为基准,标注「日志行随版本略有差异」;本机(7.0.0-38)仅作口径参考。
- 内核实现分析深度 → 只讲概念结构(`bpf_verifier_env`、`bpf_reg_state` 状态族、探索-剪枝-边界推导三板斧),函数级细节标注「以所用内核源码为准」。
- bad/fixed 都必须编译通过(拒绝发生在加载期,clang 层面全部合法)→ 把「编译成功≠能加载」变成 verify-build 的机械验收事实。

## 测试策略

编译级 verify-build.sh(单章版,同 ch02 模式):BTF 前置检查 → 01-pkt-parse 与画廊七对全部 make(必须编译成功)→ exercises 两项 make → PASS/FAIL 汇总,结果写入章级 README。加载级(实际通过/拒绝的 verifier 日志)一律「预期输出示例 + 运行级待验证」(本机无 root,`unprivileged_bpf_disabled=2`)。结构级:九节齐备、五层/双视角/注释五类要点/可视化/排错/总结/参考/课后任务在场,正文代码块与示例文件一一对应,bad 程序必须带「预期 verifier 拒绝」标注。

## Spec Patch

无(delta spec 无需变更)。

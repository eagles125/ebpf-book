# Comet Subagent 进度检查点 — generate-ebpf-book

- plan: docs/superpowers/plans/2026-10-25-chapter04-teaching.md（12 任务，第 4 章《eBPF 程序类型与钩子点深入》，Batch 2 收官章）— **全部完成并停靠**
- design-doc: docs/superpowers/specs/2026-10-25-chapter04-teaching-design.md（c9ae92e；篇幅口径 35–54k 由篇幅裁定回写 f32f917 同步）
- review_mode: thorough；tdd_mode: direct；isolation: current（master）
- 上一章检查点：第 3 章全部收口（git 历史 a9a4a84 Park ch03 build session 与其后任务提交）
- ⚠️ 编排文件保护：本文件与 `.comet/**`、plan checkbox 为控制器专管——subagent 不得触碰，不得对其执行 git restore/checkout/stash

## 当前状态

- **停靠（2026-10-30）：第 4 章 build 全部收口**——12 任务实现+逐任务审查+勾选全毕；最终完整审查（PASS with fixes）→ 修复波 d472369（exercises README 陈旧括注改写 + multi_observe.c 补 <string.h>）→ scoped 复审全 ADDRESSED 无开放项，**无开放项**
- HEAD = d472369；tasks.md「Chapter 4」7/7 已勾选（5e68abb）——**Batch 2（第 2–4 章）随之收官**；全书 change 仍未勾满，`comet guard build --apply` 按设计阻塞（同第 2/3 章）
- 硬性验收终值：`verify-build.sh` PASS=13 FAIL=0（修复波复跑）；章正文 `wc -m`=53954 ∈ [35000, 54000]（篇幅裁定 f32f917）；交叉链接 28/1 全目标存在；禁语 grep 零命中；gcc 14+ 硬错误口径下 multi_observe.c 独立编译通过（复审实证）
- SDD 工作区 `.superpowers/sdd/2026-10-25-chapter04-teaching/` 已按 Finish 删除（本检查点 + git 历史为记录）

## 第 4 章任务台账（全部 done）

- Task 1 章级 README、00-env、.gitignore — done（8aa0600 + 修复轮 1 fc37d8b / 勾选 4766eaa）
- Task 2 01-multi-observe 同一事件五副眼镜 — done（c1db3f3 / 勾选 683299b；计划回写 8a2fd0b：skeleton 前缀 multi_observe_bpf）
- Task 3 02-xdp-tc 同一报文两种 ctx — done（ac73189 + 修复轮 1 ecfc807[填充 opts detach] / 计划回写同步）
- Task 4 画廊前四类与总览 — done（5b65477 / Approved 0 修复）
- Task 5 画廊后四类 — done（0d0d4c4 + 修复轮 1 d7cf474/3587c90 / 勾选 ab7119d；计划回写 e27dcc2：bpf_nf_ctx{state,skb}）
- Task 6 正文 目标/导读/一/二 — done（637cea3 + 修复轮 1 bb83f7e[sockops 教学约定保守标注] / 回写 d5d7c33 / 勾选 82acc80）
- Task 7 正文 三、类型画廊走读 — done（aa5e158 + 修复轮 1 3db4000[篇幅收敛 17011+raw_tp 条件化口径] / 篇幅裁定回写 f32f917[52k→54k] / Task 7 定位笔误勘误 fcedbb5 / 勾选 c2ef37f）
- Task 8 正文 四、实战 — done（9f45c8d + 修复轮 1 669e4bc[引文标注/移镜指引/bpf_xdp_attach 补注] / 勾选 7e26dcf）
- Task 9 正文 可视化/排错/课后/总结/参考 — done（fba6b42 / Approved 0 修复 / 勾选 94cce7f）
- Task 10 exercises 两项 — done（796772e / Approved 0 修复 / 勾选 bfc54bd）
- Task 11 verify-build/终检/交叉链接/清扫 — done（b351c4f + 修复轮 1 4cea2d8[:49 generic 回退残留] / 勾选 e87113a）——27 项清扫清单逐项处置
- Task 12 主审 + tasks.md 勾选 — done（5e68abb / 四组审计全绿 / 勾选 f5ec092）
- 最终完整审查 — done（PASS with fixes → 修复波 d472369 → 复审 clean，无开放项）

## 延后项登记（移交全书批次/统稿，非第 4 章开放项）

1. **raw_tp 资产 SEC 可挂性**：锚点① `raw_tp/sys_enter_openat2` 本机 kallsyms 无 per-syscall 静态 tracepoint（属 ftrace 路径），attach 预期 ENOENT——正文七处已条件化+移镜指引+排错表闭环；终审裁定维持现状，统稿可评估改 `raw_tp/sys_enter`+id 过滤（需同步七处叙事）
2. 01-multi-observe/README「分镜 map」×2 与章正文「分镜映射」分叉（multi_observe.bpf.c 代码注释内 1 处受逐字红线保护）——统稿术语统一
3. multi_observe.c 仅注册 SIGINT vs xdp_tc.c SIGINT+SIGTERM 信号卫生不对称——后续批次
4. 02-uprobe/README「文件偏移」与 nm/readelf（给 vaddr）小抵牾——统稿与 ch03 侧同款措辞一并校准
5. :49/README②「lo 无原生路径」定言式 vs 5.1:623「随内核版本」存疑式档位差（终审裁定维持：loopback 无 ndo_bpf 结构性事实）
6. 排错节 fentry 两行未补预期日志原文形态（篇幅余量 46 不足；信息经 README① 排错表闭环）
7. 三节末无汇总「独立示例」行（ch03:785 有先例；14 card 各自收口，终审维持豁免，统稿可补）
8. xdp_tc.c:52 SIGTERM 复用 on_sigint 处理函数名（功能正确，语义错位）——后续批次
9. Task 2 时代统稿挂账延续：attach 失败分支未带 errno、last_name 空串误标、README 五行计数教学近似（strerror 缺 <string.h> 已在终审修复波解决）
10. 教学简化记录：sockops 返回值教学约定口径（1.2/1.3/3.12 三处已带保守标注，C3 表回写 d5d7c33）、raw_tp 条件化口径——plan-mandated 保持

## 流程建议（来自终审，供未来章节采用）

- 清扫清单的 grep 模式必须覆盖**措辞变体**（Task 11 修复轮 1：清单 grep「自动回退」漏掉「generic 回退」变体——同义表述先枚举再 grep）
- 清扫跨资产口径统一时，章内正典（如 5.1:623）与 README 侧措辞档位（定言式/存疑式）应一并声明，避免复审轮再发现档位差

## 已确认决策（不得重新询问）

- 四项工作方式（2026-09-25）：isolation=current（master）、build_mode=subagent-driven-development（subagent_dispatch=confirmed）、tdd_mode=direct、review_mode=thorough
- 验证策略：编译级真实验证 + 运行级一律「预期输出示例 / 运行级待验证」（无 root，unprivileged_bpf_disabled=2）
- 第 4 章特有：画廊 8 类与练习程序 clang 编译通过（compile-only）；两锚点全链；签名以本机 BTF 为准（do_sys_openat2 实证；vfs_unlink 签名漂移实证进教学）；BPF_PROG/BPF_KPROBE/BPF_KRETPROBE 在 bpf_tracing.h；TC_ACT_OK/NF_ACCEPT 手工 define；bpf_nf_ctx{state,skb}；raw_tp syscall 类形状 (regs, id)
- 篇幅：第 4 章 35–54k（篇幅裁定回写 f32f917；ch04 设计 §3.3 为纯 wc -m 口径，ch03「讲解文字口径」裁定不外溢）
- sockops 返回值教学约定口径回写（d5d7c33）：C3 表与正文三处统一「教学约定+内核实际消费面较窄」保守标注
- raw_tp 资产维持现状裁定（终审）：ENOENT 即挂载查找路径差异教学点，改 SEC 方案登记统稿候选
- 计划回写先例：Task 2/3/5/7 的计划笔误均按「回写对齐落地形态 + 留痕」处理（8a2fd0b / e27dcc2 / f32f917 / fcedbb5 等）
- 本平台 Agent 工具无 model 参数，按可用工具派发（先例：ch02/ch03/ch04 会话）

## 恢复指引

1. 用户决策下一章/下一批次（Batch 3/5/7 等），为该章走 design → plan → build（同第 2/3/4 章模式）；上述「延后项登记」中标注「统稿/后续批次」的项在该章或统稿批次处理
2. 全书 tasks.md 勾满后：`comet guard build --apply` → verify → archive

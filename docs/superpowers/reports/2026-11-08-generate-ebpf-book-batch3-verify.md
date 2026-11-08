# Batch 3 验证报告 — generate-ebpf-book(第 5–8 章 + 全书回归)

- 日期:2026-11-08
- 范围:**Batch 3**(第 5–8 章,章级循环 2026-10-31 ~ 2026-11-08 完成)加上全书级回归清扫
- 性质:**批次级验证(build 阶段内证据)**。全书 change 的 comet verify 门(`comet guard build --apply`)前置条件为 tasks.md 全部任务勾选,Batch 4–8 尚未开工,故按设计**尚未进入** comet verify 阶段;`verify_result` 保持 `pending`。本报告为 build 期内批次验收留痕,不替代全书终验。

## 一、第 5–8 章章级验收状态(章级循环闭环记录)

| 章 | plan | 章级任务 | 审查 | 勾选提交 | 章终态要点 |
|---|---|---|---|---|---|
| 5 子系统内核代码结构 | 2026-10-31-chapter05-teaching.md | 全部完成 | 每任务 thorough reviewer,审查-修复≤2 轮 | 见 git 历史 | 停靠检查点在册(2026-11-01) |
| 6 JIT 编译与性能 | 2026-11-01-chapter06-teaching.md | 全部完成 | 同上 | 见 git 历史 | 停靠检查点在册(2026-11-01) |
| 7 辅助函数与映射内部 | 2026-11-01-chapter07-teaching.md | 13 任务全毕+主审终审 | 20+ 行号抽检、API/类型名 30 处全检 | cc15746 | 硬性验收 44/44 全绿等(停靠 2026-11-02) |
| 8 BTF 与 CO-RE 原理 | 2026-11-02-chapter08-teaching.md | 14 任务全毕 | 每任务 thorough,3 任务各 1 轮修复全 ADDRESSED;终审 Approved C0/I0 | plan 14/14(c8de401) | 停靠 7dfde35(2026-11-08) |

章级循环的完整审查-修复台账:`openspec/changes/generate-ebpf-book/.comet/subagent-progress.md`(各章延后 Minor 均在册,无 Critical/Important 开放项)。

## 二、本次清扫实测(2026-11-08,机 /work/linux/linux-6.6.78,内核 7.0.0-42)

### 1. 符号机检复跑(verify-citations.sh,负路径均实测 exit 1 能力在各章收口时已验)

| 章 | 汇总 |
|---|---|
| ch05 | TOTAL=43 PASS=43 FAIL=0 |
| ch06 | TOTAL=32 PASS=32 FAIL=0 |
| ch07 | TOTAL=44 PASS=44 FAIL=0 |
| ch08 | TOTAL=38 PASS=38 FAIL=0 |

### 2. 篇幅(wc -m,均带内)

| 章 | 实测 | 预算 |
|---|---|---|
| ch05 | 48525 | 35–50k |
| ch06 | 46153 | 35–50k |
| ch07 | 35093 | 35000–50000(压线,停靠时已知) |
| ch08 | 54987 | 40000–55000 |

### 3. 交叉链接完整性(全书级)

扫描对象:10 个章文件 + 9 个 examples 章级 README + 4 个 appendix 文件,全部相对链接逐一解析目标文件。
**断链 = 0**。

### 4. 保守措辞

- 「已验证通过」:章正文+示例 README+appendix **实际违规 0 处**(grep 命中 2 处均为 ch02:582/ch03:876 三档验证约定表格中的规则原文);
- 「拷贝」:ch03(1)/ch04(8)/ch05(7)/ch07(5) 属各章语境合法用法(拷贝语义/宏注释),非 ch08 式双实现禁语场景;ch08 正文 0 处。

### 5. ch08 示例资产终态(本日终审实测)

两练习 check:①relo-kind-detect TOTAL=9 PASS=9 exit 0(负路径 exit 1);②core-compat TOTAL=6 PASS=6 exit 0(负路径 exit 1);机检 38/38;C8 交叉链接 28/2。

## 三、已完成章节清单(全书 10 章文件在库)

01(导论示例章)、02、03、04(Batch 2 已验收)、05、06、07、08(Batch 3 本次验收)、09(骨架+ebpf.top 改写,教学标准改造属 Batch 4)、17(深骨架+完整教学章,Batch 2/5 交界按批次记录验收)。

## 四、余下批次(不在本次验证范围)

- Batch 4:第 9–14 章(含第 9 章教学标准改造矩阵:253–256 行四项)
- Batch 5:第 15–19 章(17 章加深)
- Batch 6:第 20–23 章;Batch 7:第 24–25 章+附录;Batch 8:全书统稿/参考/示例终验/字数终审(含第 1 章教学标准改造 :164-165)
- 全部勾选后进入 comet verify 门(`comet guard generate-ebpf-book build --apply` → verify 阶段)与归档。

## 五、结论

Batch 3(第 5–8 章)全部任务完成、审查通过、机检/篇幅/交叉链接/措辞四类清扫全绿,批次验收 **PASS**。全书 change 保持 `phase: build`(verify_result: pending),等待后续批次。

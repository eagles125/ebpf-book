# 第 5 章独立示例资产

本目录是《第 5 章 eBPF 子系统内核代码结构》的配套走读资产,对应正文「一、源码地图:布局与所有权边界」「二、加载路径追踪:BPF_PROG_LOAD 的一站一停」「三、映射生命周期:从一个 fd 到一块内存」「四、实战:把走读变成可复现的动作」四节。本章不写 BPF C 程序、不做编译、不加载——全部资产围绕「源码走读」组织:一张引用锚点清单、两个源码切片器、一个符号机检脚本,把「读内核源码」从口号变成可复现、可机检的动作。

## 演示目标

把「eBPF 子系统以十万行计的内核代码为什么读得动」拆成三件可操作的事:

- **源码地图**(`01-source-map/`):`kernel/bpf/` 与三层 `include/` 的分区导览,附全量引用锚点清单(citations.txt),每条锚点 = 文件 + 符号 + 一句话职责;
- **两条走读链**(`02-load-path/`、`03-map-ops/`):加载路径(`BPF_PROG_LOAD` 一站一停)与映射生命周期(create/lookup/update/delete/freeze/pin/release)各六站,切片器对走读树实跑,提取每站定位行、关键代码段与职责注;
- **可机检的引用**(`scripts/`):`verify-citations.sh` 对 citations.txt 逐条做「文件存在 + 符号 grep」机检——源码走读章的「编译级」等价物。

## 目录结构

| 目录 | 类别 | 内容 | 落地状态 |
|---|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:走读树/工具链/BTF/权限档位分级诊断,末尾三档结论 | 已落地:本机实跑四检查区+三档结论通过,缺树时给出 kernel.org 获取指引 |
| `01-source-map/` | 源码树地图 | `citations.txt` 全量引用锚点清单 + 分区导览 README | 已落地:citations.txt 43 条锚点本机机检 FAIL=0,README 六分区表与正文一节互为详略 |
| `02-load-path/` | 源码切片器 | `trace-load-path.sh`:`BPF_PROG_LOAD` 加载链六站 | 已落地:六站切片器对 v6.6.78 树实跑(全量 307 行),站号与正文二节一一对应 |
| `03-map-ops/` | 源码切片器 | `trace-map-ops.sh`:映射生命周期六站 | 已落地:六站切片器对 v6.6.78 树实跑(全量 427 行),站号与正文三节一一对应 |
| `scripts/` | 符号机检 | `verify-citations.sh`:逐条机检引用锚点,输出 TOTAL/PASS/FAIL 汇总 | 已落地:verify-citations.sh 本机实跑 TOTAL=43 PASS=43 FAIL=0,退出码可作门禁 |
| `exercises/` | 课后扩展 | 2 项走读练习:`01-trace-freeze/` BPF_MAP_FREEZE 全链走读卡(check-answer.sh 十锚点机检)、`02-extend-tracer/` BPF_BTF_LOAD 三站切片器(trace-btf-load.sh + check-trace.sh 六断言自检),均含防剧透参考实现 README | 已落地:机检级本机 v6.6.78 实跑全绿(01:TOTAL=10 PASS=10 FAIL=0;02:TOTAL=6 PASS=6 FAIL=0),冻结的运行级效果为预期输出示例/运行级待验证 |

## 学习路径

1. `bash 00-env/check-env.sh` 自检:无 root 也可运行,按三档口径给出结论;走读树不在场时按提示从 kernel.org 获取(git clone 或 tar 两种方式)。
2. 进入 `01-source-map/`:对照正文一节读分区导览,记住 citations.txt 的锚点格式(文件|符号|说明)。
3. 用 `scripts/verify-citations.sh` 机检锚点清单:汇总行 FAIL=0 即全绿,再开始走链。
4. 对照正文二节实跑 `02-load-path/trace-load-path.sh`,对照正文三节实跑 `03-map-ops/trace-map-ops.sh`:每站 = 定位行(标注 v6.6.78)+ 关键代码段 + 职责注。
5. 进入 `exercises/`:先自行完成走读任务,再看各练习 README 后部的参考实现思路(防剧透结构)。

## 验证状态约定(三档口径)

- **编译级**:本章无 BPF 程序可编译,等价前提为「走读树齐备 + grep/sed/wc 在场」——无 root 可真实验证。
- **机检级(脚本与机检)**:机检与切片脚本对走读树实跑(`bash -n` 语法检查 + 实跑断言)——无 root 可真实验证。
- **加载级与特权查询(bpftool prog show 等)**:需要特权——无 root 机器上一律标注「需要特权,输出为预期示例 / 运行级待验证」。

## 机检汇总

本机实测(2026-11-01;运行环境一句话:普通用户 shell,仅需 bash/grep/sed/wc,clang 不需要,无 root 可复现)——`bash book/examples/chapter-05/scripts/verify-citations.sh` 实跑输出:

```text
TOTAL=43 PASS=43 FAIL=0 WALK_TREE=/work/linux/linux-6.6.78 TREE_VERSION=v6.6.78
```

退出码 0。换树或回修 `citations.txt` 后以同一命令复跑,以汇总行 `FAIL=0` 收口;FAIL 的三类处置见正文四节排错③。

## 走读树版本声明

本目录全部机检与切片脚本默认针对 `/work/linux/linux-6.6.78`(Linux v6.6.78 完整源码树,非 git)实跑;正文所有内核路径/行号引用锚定该树,行号一律标注「(v6.6.78)」并注明随版本漂移。换用其他版本树时以 `WALK_TREE=<路径>` 覆盖默认值,行号需重新校准——机检按符号不按行号,不受行号漂移影响。

## 安全与只读声明

全部脚本只读源码树与系统文件(`/sys`、`/proc`、`/boot`),不构建、不加载任何 eBPF 程序,无生产系统风险。

---
comet_change: generate-ebpf-book
role: technical-design
canonical_spec: openspec
---

# 第 5 章完整教学章节技术设计(eBPF 子系统内核代码结构,机制章 + 源码走读实战化)

> **确认状态**:brainstorming 结构化提问两轮未获答复,按 ch03 会话先例采用推荐项(走读树=6.6.78、方案 A 纯走读资产、篇幅 35–50k、练习 2 项走读口径),**待用户复核,可推翻**;推翻任一项只需在 build 阶段前提出,按 brainstorming 流程迭代。

## 1. 背景与目标

### 1.1 矩阵任务(openspec tasks.md「Chapter 5: eBPF Subsystem Kernel Code Structure」,7 条)

1. Write full chapter prose introducing eBPF subsystem source layout and ownership boundaries.
2. Trace the load path from `BPF_PROG_LOAD` through `kernel/bpf/syscall.c`, verifier, JIT selection, and attach setup.
3. Explain map create, lookup, update, delete, freeze, pin, and reference lifecycle at a source-code level.
4. Include a source-reading walkthrough with commands for navigating a local Linux tree.
5. Add text source tree and call-flow visualization.
6. Add troubleshooting, summary, and references.
7. Main-agent review for kernel path accuracy and conservative version language.

### 1.2 前章预告回收清单(正文必须兑现)

| 出处 | 预告内容 | 兑现位置 |
|---|---|---|
| ch02:423 | 「子系统源码地图(`syscall.c`/`verifier.c`/各映射实现文件的关系)是第 5 章的正题」 | 一节整节 |
| ch02:131 | 「JIT 编译为本机指令,架构相关(第 6 章,源码地图见第 5 章)」 | 二节 JIT 选择站 |
| ch02:289 | 「映射在内核里的实现结构是第 5 章的内容」 | 三节 |
| ch04:805 | 第 5 章「把 2.1 的一行宏展开成源码地图」 | 一节 `include/linux/bpf_types.h` 宏展开 |

### 1.3 定位

机制章 + 源码走读实战化。本章与前四章的根本差异:**交付物不是能跑的 BPF 程序,而是经得起机检的源码引用**——读者读完应能拿着一棵本地源码树,独立走通「一条命令从用户态到内核」的完整路径。示例资产因此以**机检脚本 + 源码切片器**为核心(方案 A),全部无 root 可真实验证。

## 2. 示例资产设计(方案 A:纯走读资产)

`book/examples/chapter-05/`:

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:源码树存在性与版本(`Makefile` 的 VERSION/PATCHLEVEL/SUBLEVEL)、`kernel/bpf/` 完整性(syscall.c/verifier.c 在场)、工具链(grep/sed/wc/bpftool)、BTF、权限档位分级诊断;无源码树时给出获取指引 |
| `01-source-map/` | 源码树地图资产 | `README.md`(45 个 .c 的分区导览表:系统调用/验证/JIT 核心/map 实现/BTF/存储/结构化等)、`citations.txt`(正文全部引用锚点清单,机检脚本的数据源) |
| `02-load-path/` | 加载链切片器 | `trace-load-path.sh`:对走读树提取 `BPF_PROG_LOAD` 路径各站(分发→`bpf_prog_load`→`bpf_check`→`bpf_prog_select_runtime`→fd/挂载准备)的函数签名与关键行,打印走读地图;只读树,不构建 |
| `03-map-ops/` | 映射链切片器 | `trace-map-ops.sh`:对 map 生命周期链(create/lookup/update/delete/freeze/pin/release)同样切片;以 arraymap 为正典 |
| `scripts/` | 符号机检 | `verify-citations.sh`:读 `01-source-map/citations.txt`,对每个锚点(文件存在性 + 符号 grep)复核,PASS/FAIL 汇总退出码——**源码走读章的「编译级」等价物** |
| `exercises/01-trace-freeze/` | 练习① | 追踪 `BPF_MAP_FREEZE` 完整调用链:参考实现=走读文档(含调用链图)+ 符号表 + `check-answer.sh`(对参考答案逐符号机检) |
| `exercises/02-extend-tracer/` | 练习② | 扩展切片器支持 `BPF_BTF_LOAD`:参考实现=完整可运行 bash 脚本(bash -n 语法检查 + 对树实跑断言) |

资产设计规则:

- **「可编译」的章级等价口径**:机检脚本全绿 + 切片器/练习脚本 `bash -n` 通过且对 6.6.78 树实跑输出正确。全部真实验证,无 root 需求(优于 BPF 程序类资产的验证覆盖率)。
- 练习 README 沿用防剧透结构(任务在前、参考实现思路独立成节在后)与验证状态标注(脚本级已验证;凡涉及加载/bpftool 特权查询的输出一律「预期输出示例/运行级待验证」)。
- 不放 BPF 程序资产(方案 B 否决理由:与 ch04 资产重叠;`vmlinux.h`/BTF 完整故事是第 20 章专题,本章实物对照只用 bpftool 只读 introspection 的预期输出)。

## 3. 章节正文设计

### 3.1 结构(完整模板十一小节)与四段主轴

```markdown
# 第 5 章 eBPF 子系统内核代码结构

## 学习目标
## 本章导读
## 一、源码地图:布局与所有权边界        ← 矩阵①;ch02:423/ch04:805 兑现
## 二、加载路径追踪:BPF_PROG_LOAD 的一站一停 ← 矩阵②;ch02:131 兑现
## 三、映射生命周期:从一个 fd 到一块内存    ← 矩阵③;ch02:289 兑现
## 四、实战:把走读变成可复现的动作(五层环境)  ← 矩阵④
## 可视化:源码树全景与加载调用流          ← 矩阵⑤
## 常见问题与排错                        ← 矩阵⑥
## 课后扩展任务
## 总结
## 本章参考来源
```

四段主轴每段收尾带「**独立示例**」链接(一→01-source-map;二→02-load-path;三→03-map-ops;四→scripts)。

### 3.2 各节要点

**一、源码地图**(7–9k 字符):`kernel/bpf/` 45 个 .c 的分区导览(入口分发 syscall.c / 验证 verifier.c / 执行与 JIT core.c / 各 map 实现 arraymap.c·hashtab.c·… / BTF btf.c / 本地存储 / cgroup·net 相关)与所有权边界(`include/uapi/linux/bpf.h` 是 UAPI 契约、`include/linux/bpf*.h` 是内核内部接口、`include/linux/bpf_types.h` 宏展开为 prog 类型注册表——「一行宏展开成源码地图」的兑现点,6.6.78 树 32 处 `BPF_PROG_TYPE` 实证;net 侧 `net/core/filter.c` 的历史角色一句话交代)。双视角落位:教师视角=「为什么这个子系统能长成 20 万行而不散架」;学生动手=用 3 条命令自己生成文件清单。

**二、加载路径追踪**(8–10k):`bpf(BPF_PROG_LOAD)` → `__sys_bpf` 命令分发 → `bpf_prog_load`(syscall.c:2586)→ 属性拷贝与 `bpf_check`(verifier)→ `bpf_prog_select_runtime`(core.c:2357,JIT/解释器选择——ch02:131 兑现,深挖留第 6 章)→ fd 分配与引用计数、attach 准备(程序本身与挂载解耦,程序类型决定可用挂载点回指第 4 章)。每站固定三要素:函数签名(标注 v6.6.78 行号,随版本漂移)、职责、失败出口(errno)。

**三、映射生命周期**(7–9k):create(`BPF_MAP_CREATE`→map 类型查找→`map_create`)、lookup/update/delete(通用层 `bpf_map_update_value`(syscall.c:153)→ 各 map 实现的 ops 回调,arraymap 为正典)、freeze、pin(`bpf_obj_pin` 与 fd 生命周期)、引用释放(fd 关闭/RCU/工作队列的引用计数链)。讲清「通用封装层 + ops 回调」的内核惯用法——这正是第 4 章 prog 类型 ops 表的 map 侧镜像。

**四、走读实战**(6–8k,五层环境,同 ch04 模式组织):
1. 依赖环境:走读树选型理由(本机唯一完整树、LTS)、获取源码的通用方法(kernel.org tar/git clone)、版本核对命令;
2. 「编译」环境(本章=机检):`verify-citations.sh` 的原理与运行;
3. 部署环境(本章=定位):切目录、定位符号的命令目录(`grep -n`/`sed -n`/`wc -l`/ctags 可选);
4. 运行环境:两个切片器实跑;
5. 运行效果:走读地图输出示例 + bpftool 只读 introspection(`bpftool version`/`feature probe` 等)预期输出(运行级待验证标注)。

**可视化**(1.5–2k):①源码树全景文本图(目录树+每个文件一句话职责);②`BPF_PROG_LOAD` 调用流文本图(用户态→syscall→verifier→JIT→fd,标注失败出口)。各配 150 字以上解读段。

**排错**(2.5–3.5k):走读类排错(引用找不到——函数改名/搬文件/内联了的检索策略;行号对不上——版本漂移定位法;tags/cscope 工具化建议)+ 机检 FAIL 的三类原因与修复;对照前章失败画廊口径回指。

### 3.3 篇幅预算(总 35–50k,wc -m)

目标+导读 2–2.5k、一 7–9k、二 8–10k、三 7–9k、四 6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后+总结+参考 2.5–3.5k。

### 3.4 课后练习设计(2 项,走读口径)

- **练习① trace-freeze**:独立走通 `BPF_MAP_FREEZE` 链(`syscall.c:5461` 分发→`map_freeze`(syscall.c:1998)自包含实现:`fdget`→`__bpf_map_get`→`map_get_sys_perms`/`FMODE_CAN_WRITE` 权限检查→`freeze_mutex`+`bpf_map_write_active` 并发边界→`frozen` 标志;6.6.78 实证)。参考实现=走读文档+符号表+`check-answer.sh`。教学目标:把二/三节学到的方法迁移到没讲过的命令上,并体会「不是所有命令都经 ops 回调」。
- **练习② extend-tracer**:给 `trace-map-ops.sh` 增加 `BPF_BTF_LOAD` 支持。参考实现=完整 bash 脚本,验收=bash -n + 对树实跑断言输出。教学目标:把「切片器」从使用者变成维护者。

## 4. 验证策略(符号机检=编译级等价物)

- `scripts/verify-citations.sh`:citations.txt 每行 = `文件路径|符号|说明`;机检 = 文件存在 && `grep -q 符号 文件`;汇总 PASS/FAIL,FAIL≠0 退出码非零;输出工具版本与树版本实测值回填章级 README。
- 切片器与练习脚本:`bash -n` + 对 6.6.78 树实跑,断言输出含预期符号。
- 凡涉及加载/bpftool 特权查询的输出:「预期输出示例/运行级待验证」;全文零「已验证通过」式运行级表述(grep 复核,排除约定表原文自引)。
- 机检**按符号不按行号**;正文行号一律「(v6.6.78)」标注。

## 5. 技术风险与边界

| 风险 | 消化方式 |
|---|---|
| 6.6.78 树 ≠ 7.0.0-38 运行内核:函数改名/搬文件/新增 map 类型 | 版本演进本身是教学点:差异只举例说明「如何排查」,不作跨版本断言;运行内核对接只用 BTF 实证事实 |
| 正文行号随读者树漂移 | 机检按符号;行号标注树版本;排错节给「行号漂移定位法」 |
| verifier.c 2 万行只讲入口 | 明确边界:本节只走「加载路径上 verifier 的入口与返回」,内部机制是第 3 章已讲内容的指针,不重复 |
| 走读树非 git、无 commit 元数据 | 参考来源写 kernel.org v6.6 版本锚点;树版本以 Makefile 为准 |

## 6. tasks.md 回写清单

矩阵 7 条由末任务统一勾选;「Batch 3: Complete Chapters 5-8」总项在 Batch 3 全部章节完成后勾选(本单勾第 5 章时不动)。

## 7. 测试策略汇总

1. 符号机检全绿(真实验证);
2. 切片器/练习脚本 bash -n + 实跑断言;
3. 结构级:十一小节齐备、五层落位、双视角(一/二/三节内「教师视角」「学生动手」+检查点)、可视化两图带解读、排错/总结/参考齐备、正文代码块与示例文件一一对应(抽查)、引用锚点与 citations.txt 一一对应;
4. 保守措辞:全文 grep 复核零运行级「已验证」表述;「英文(中文解释)」首现格式;术语译名一致。

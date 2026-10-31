---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-10-31-chapter05-teaching-design.md
base-ref: 82ae041
---

# 第 5 章完整教学章节(build 阶段)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 5 章《eBPF 子系统内核代码结构》从零写为 35–50k 字符(wc -m)的完整教学章节(机制章 + 源码走读实战化),并落地 `book/examples/chapter-05/` 全部走读资产:00-env 环境自检、01-source-map 源码树地图、02-load-path 与 03-map-ops 两个源码切片器、scripts/verify-citations.sh 符号机检(源码走读章的「编译级」等价物)、2 项走读练习(含参考实现与机检答案)。

**架构:** 资产先行——01-source-map 的 `citations.txt` 是全章共享接口(正文引用锚点清单),`verify-citations.sh` 消费它做符号级机检(文件存在 + 符号 grep,不锚行号);两个切片器脚本对走读树 `/work/linux/linux-6.6.78` 实跑提取加载链/映射链各站签名与关键行;正文四段主轴(源码地图→加载路径→映射生命周期→走读实战五层)每段引用的锚点全部可机检。本章无 BPF C 程序、无 Go;全部验证无 root 可真实验证。

**技术栈:** bash(grep/sed/wc 核心工具;切片器与机检脚本,`bash -n` + 实跑断言)、Markdown(中文正文)。走读树:Linux 6.6.78(`/work/linux/linux-6.6.78`)。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md`「Chapter 5: eBPF Subsystem Kernel Code Structure」7 条;Batch 3 其余章节(6/7/8)另立计划。
- delta spec 无需变更(无 Spec Patch),不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 不触碰 `book/chapters/01-*.md`~`04-*.md`、`09-*.md`、`17-*.md` 及对应 examples 目录;不动 `.gitignore`(本章产物全部为脚本/Markdown/citations 文本,无二进制)。
- 只修改:`book/chapters/05-ebpf-subsystem-kernel-code-structure.md`(新建)、`book/examples/chapter-05/**`(新建)、`openspec/changes/generate-ebpf-book/tasks.md`(末任务勾选)。

## 全局约束

1. **语言**:全书与全部脚本注释使用中文;术语遵循 `book/STYLE.md`(map=映射、helper=辅助函数、program type=程序类型、attachment point=挂载点);术语首现「英文(中文解释)」格式。
2. **走读树口径(硬性)**:正文所有内核路径/函数/宏引用锚定 `/work/linux/linux-6.6.78`;行号引用一律标注「(v6.6.78)」并注明随版本漂移;机检**按符号不按行号**。涉及运行内核(7.0.0-38)的对接只用 BTF 实证事实(`CONFIG_BPF_SYSCALL=y`、`CONFIG_DEBUG_INFO_BTF=y`、`/sys/kernel/btf/vmlinux` 在场),差异处保守措辞「随内核版本演进,以所用内核源码为准」。
3. **验证策略(硬性)**:机检脚本与切片器在本机对 6.6.78 树**真实验证**(`bash -n` + 实跑);凡涉及加载/bpftool 特权查询的输出一律「预期输出示例/运行级待验证」,全文零「已验证通过」式运行级表述(本机无 root,`unprivileged_bpf_disabled=2`——该事实本身是本章教学素材,`syscall.c` 的 `sysctl_unprivileged_bpf_disabled` 是它的内核出处)。
4. **篇幅**:章节正文目标 35–50k 字符(`wc -m`);分节预算按设计 §3.3:目标+导读 2–2.5k、一(源码地图)7–9k、二(加载路径)8–10k、三(映射生命周期)7–9k、四(实战五层)6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后+总结+参考 2.5–3.5k。低于下限补原理讲解而非注水;高于上限压缩重复叙述不删原理。
5. **叙事**:双视角落「一、二、三」三节——各节内用「**教师视角**」「**学生动手**」加粗引导段,检查点用编号列表(2–4 个自测问题+答案提示)。「四、实战」按五层组织(依赖→机检→定位→运行→效果),内含教师视角引入/核心命令原理讲解/学生动手/检查点四段。
6. **脚本注释**:所有 bash 脚本中文注释,覆盖四类要点:脚本用途与输入、输出结构、依赖命令与降级行为、失败模式(机检 FAIL 的三类原因)。脚本头部注明「只读源码树,不构建、不加载」。
7. **任务对应**:每个任务必须引用 tasks.md「Chapter 5」矩阵的任务文本片段;全部任务完成后由 Task 13 统一勾选 tasks.md 的 7 条。
8. **提交纪律**:每个任务以一次 git 提交结束;提交信息 `feat(ch05): ...` / `docs(ch05): ...` / `chore(ch05): ...`;从仓库根 `/work/ai/ebpf-book` 执行。Comet 编排产物(`openspec/changes/generate-ebpf-book/.comet/*`)的未提交改动不属于任何 ch05 提交,每次 `git add` 只加当前任务 Files 列出的路径。

## 现状基线(编写计划时已在 6.6.78 树实证)

| 事实 | 值 |
|---|---|
| HEAD | `82ae041`(ch05 设计文档已提交 `499db88`;工作区干净) |
| 走读树 | `/work/linux/linux-6.6.78`(完整源码树,非 git,预构建含 .o;`Makefile` VERSION=6 PATCHLEVEL=6 SUBLEVEL=78) |
| 文件规模 | `kernel/bpf/syscall.c` 5823 行、`verifier.c` 20459 行、`btf.c` 8601 行、`core.c` 2975 行、`hashtab.c` 2630 行、`arraymap.c` 1383 行;`kernel/bpf/` 共 45 个 .c |
| 系统调用入口 | `SYSCALL_DEFINE3(bpf,` syscall.c:5567 → `__sys_bpf` :5426 → 命令分发 switch:BPF_MAP_CREATE :5446、BPF_MAP_FREEZE :5461、BPF_PROG_LOAD :5464、BPF_BTF_LOAD :5509 |
| X-macro 双展开(「一行宏展开」兑现点) | `bpf_map_types[]` syscall.c:63(`BPF_MAP_TYPE(_id,_ops)` → `[_id] = &_ops`,第一次 include bpf_types.h :68);`bpf_prog_types[]` ~:2043(`BPF_PROG_TYPE(_id,_name,...)` → `[_id] = &_name ## _prog_ops`,第二次 include :2044);同一头文件 `include/linux/bpf_types.h`(32 处 BPF_PROG_TYPE;BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops) 在 :87) |
| id 注册表 | `prog_idr`/`map_idr`/`link_idr` + 各自 spinlock,syscall.c:54–59 |
| 加载路径 | `bpf_prog_load` syscall.c:2586 → `bpf_check`(verifier.c:20222)调用 ≈:2743 → `bpf_prog_select_runtime`(core.c:2357;内部 `bpf_int_jit_compile` ≈core.c:2384,失败回退解释器)调用 :2747 → `bpf_prog_alloc_id` ≈:2751 → `bpf_prog_new_fd` :2293;**加载与挂载解耦**:attach 走独立命令(link_create/BPF_PROG_ATTACH),回指第 4 章 |
| 程序释放链 | `__bpf_prog_put_rcu` :2136、`__bpf_prog_put` :2181、`bpf_prog_put` :2195 |
| 映射创建 | `map_create` syscall.c:1127;类型查找 `bpf_map_types[map_type]` + `array_index_nospec` :1163–1166;非特权检查 `sysctl_unprivileged_bpf_disabled && !bpf_capable()` :1186 |
| 映射通用层 | `bpf_map_update_value` :153、`bpf_map_copy_value` :210;命令包装 `map_lookup_elem` :1434、`map_update_elem` :1509、`map_delete_elem` :1565;fd 取 map:`__bpf_map_get` :1322、`bpf_map_get_with_uref` :1363 |
| 映射冻结 | `map_freeze` :1998 自包含(无 ops 回调):`fdget`→`__bpf_map_get`→`map_get_sys_perms`/`FMODE_CAN_WRITE` 权限检查→`freeze_mutex`+`bpf_map_write_active`→`frozen` 标志 |
| 映射释放链 | `bpf_map_put_uref` :717、`bpf_map_free_deferred`(工作队列) :691、`bpf_map_free_rcu_gp` :734、`bpf_map_put` :750 |
| 映射 pin | `bpf_obj_pin` :2797(`bpf_obj_pin_user`)、`bpf_obj_get` :2813 |
| 练习②锚点 | `bpf_btf_load` :4808(分发 :5509) |
| 文档 | `Documentation/bpf/`(btf.rst、classic_vs_extended.rst、index.rst、helpers.rst 等) |
| 本机环境 | clang 18.1.3、bpftool 7.7.0(/usr/sbin)、gcc、libbpf 1.3;无 root、`unprivileged_bpf_disabled=2`;BTF 在场 |
| 可复用模式 | ch02/03/04 计划的 C5(README 结构)/C6(验证状态)/C7(标题固定)/C8(交叉链接)——本章同字母契约引用并给出替换与新增 |

## 共享接口契约(跨任务定义,一致性以此为准)

### C1. citations.txt 格式与全量清单(01-source-map/,Task 2 产出,Task 3 机检消费,Task 11 终跑)

每行格式:`<相对 walk 树根路径>|<符号>|<一句话中文说明>`;注释行以 `#` 开头。全量清单(43 行,顺序即机检顺序):

```text
# 第 5 章引用锚点机检清单(v6.6.78)——格式:文件|符号|说明
kernel/bpf/syscall.c|SYSCALL_DEFINE3(bpf,|系统调用入口
kernel/bpf/syscall.c|__sys_bpf|命令分发总入口
kernel/bpf/syscall.c|bpf_map_types[]|映射类型注册表(第一次展开)
kernel/bpf/syscall.c|bpf_prog_types[]|程序类型注册表(第二次展开)
kernel/bpf/syscall.c|sysctl_unprivileged_bpf_disabled|非特权开关(本机=2 的内核出处)
kernel/bpf/syscall.c|map_create|映射创建
kernel/bpf/syscall.c|array_index_nospec|类型索引侧信道防护
kernel/bpf/syscall.c|bpf_prog_load|程序加载
kernel/bpf/syscall.c|bpf_prog_new_fd|程序 fd 分配
kernel/bpf/syscall.c|bpf_check_call-site|占位(见下注)
kernel/bpf/verifier.c|int bpf_check(|验证入口
kernel/bpf/core.c|bpf_prog_select_runtime|JIT/解释器选择
kernel/bpf/core.c|bpf_int_jit_compile|架构 JIT 入口
kernel/bpf/syscall.c|__bpf_prog_put|程序引用释放
kernel/bpf/syscall.c|bpf_prog_put|程序引用释放(对外)
kernel/bpf/syscall.c|bpf_map_update_value|映射更新通用层
kernel/bpf/syscall.c|bpf_map_copy_value|映射查找通用层
kernel/bpf/syscall.c|map_lookup_elem|查找命令包装
kernel/bpf/syscall.c|map_update_elem|更新命令包装
kernel/bpf/syscall.c|map_delete_elem|删除命令包装
kernel/bpf/syscall.c|map_freeze|冻结(自包含,无 ops 回调)
kernel/bpf/syscall.c|bpf_map_write_active|冻结并发边界
kernel/bpf/syscall.c|bpf_map_put_uref|映射用户引用递减
kernel/bpf/syscall.c|bpf_map_free_deferred|工作队列延迟释放
kernel/bpf/syscall.c|bpf_map_free_rcu_gp|RCU 宽限期释放
kernel/bpf/syscall.c|bpf_map_put|映射引用释放(对外)
kernel/bpf/syscall.c|bpf_obj_pin|pin 到文件系统
kernel/bpf/syscall.c|bpf_obj_get|从路径取回 fd
kernel/bpf/syscall.c|bpf_btf_load|BTF 加载(练习②)
kernel/bpf/syscall.c|prog_idr|程序 id 注册表
kernel/bpf/syscall.c|map_idr|映射 id 注册表
kernel/bpf/arraymap.c|array_map_ops|数组映射 ops
kernel/bpf/hashtab.c|htab_map_ops|哈希映射 ops
kernel/bpf/btf.c|btf_parse|BTF 解析入口
include/linux/bpf_types.h|BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER|一行宏的实例
include/linux/bpf_types.h|BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY|一张 ops 表的实例
include/uapi/linux/bpf.h|BPF_PROG_LOAD|UAPI 命令字
include/uapi/linux/bpf.h|union bpf_attr|UAPI 属性联合
include/linux/bpf.h|struct bpf_prog|程序内核对象
include/linux/bpf.h|struct bpf_map|映射内核对象
include/linux/bpf.h|struct bpf_map_ops|映射操作回调表
include/linux/filter.h|struct bpf_binary_header|JIT 映像头
```

**实现注**:`bpf_check_call-site` 占位行无效——`bpf_check` 的调用点不定义符号;实际清单中该行写为 `kernel/bpf/syscall.c|bpf_check(&prog|验证调用点`,grep -F 字面匹配。其余行以「文件真实存在 + grep -F 符号命中」为机检口径;Task 2 实现时逐行实测修正(命中失败=清单错误,不是脚本错误),修正留痕进任务报告。

### C2. verify-citations.sh 接口(scripts/,Task 3)

- 输入:环境变量 `WALK_TREE`(默认 `/work/linux/linux-6.6.78`);数据源固定为仓库内 `../01-source-map/citations.txt`(相对脚本定位,`cd "$(dirname "$0")/../01-source-map"` 解析)。
- 逐行机检:跳过空行/`#` 注释;字段数≠3 → 记 FAIL(格式错);文件存在性 + `grep -F -q -- "$sym" "$WALK_TREE/$file"`。
- 输出:每行 `PASS <file>|<sym>` / `FAIL <原因>`;末尾汇总 `TOTAL=43 PASS=n FAIL=m WALK_TREE=<路径> TREE_VERSION=v6.6.78`;FAIL≠0 → 退出码 1。头部注释含「只读源码树,不构建、不加载」与失败三类原因说明(文件不在树/符号改名或内联/清单行写错)。

### C3. 切片器接口(02-load-path/trace-load-path.sh、03-map-ops/trace-map-ops.sh,Task 4/5)

- 调用:`bash trace-load-path.sh [WALK_TREE]`(默认同 C2);**只读树**,输出到 stdout。
- 结构:按「站」分节打印——每站 = `## 站名` + `grep -n` 定位行(标注 v6.6.78)+ `sed -n 'A,Bp'` 提取的关键代码段(每段 ≤25 行)+ 一行中文职责注。
- trace-load-path.sh 六站:①系统调用入口与分发(SYSCALL_DEFINE3/`__sys_bpf`/case BPF_PROG_LOAD)②`bpf_prog_load` 签名与属性处理③验证调用点(`bpf_check(&prog`)④JIT 选择(`bpf_prog_select_runtime` 调用点与 core.c 定义头)⑤fd 分配(`bpf_prog_new_fd` 与 prog_idr)⑥释放链(`__bpf_prog_put`/`bpf_prog_put`)。
- trace-map-ops.sh 六站:①入口分发与 `bpf_map_types[]` ②`map_create` 与类型查找③通用层(`bpf_map_update_value`/`bpf_map_copy_value` + arraymap 的 `array_map_ops` 对照)④命令包装(map_lookup/update/delete_elem)⑤freeze 与 pin(map_freeze/bpf_obj_pin)⑥释放链(bpf_map_put_uref/deferred/rcu_gp)。
- 输出末尾汇总:`TREE_VERSION=v6.6.78 STATIONS=6`(站内提取失败 → 打印 `STATION n: NOT FOUND` 且退出码 1)。

### C4. 练习接口(exercises/,Task 12)

- `01-trace-freeze/`:README(防剧透七段)+ `check-answer.sh`(内嵌符号表,对 `WALK_TREE` 逐符号 grep -F,输出 PASS/FAIL 汇总,退出码语义同 C2)。参考答案符号表(实测锚点):`SYSCALL_DEFINE3(bpf,`、`__sys_bpf`、`case BPF_MAP_FREEZE`、`static int map_freeze(`、`__bpf_map_get`、`map_get_sys_perms`、`FMODE_CAN_WRITE`、`freeze_mutex`、`bpf_map_write_active`、`frozen`(syscall.c 内)。
- `02-extend-tracer/`:README(防剧透)+ 参考实现 `trace-btf-load.sh`(三站:分发 case BPF_BTF_LOAD、`bpf_btf_load` 签名、`btf_parse` 调用点 in btf.c;断言输出含三个锚点,接口语义同 C3)。参考实现附 `check-trace.sh` 做实跑断言(bash -n + grep 输出)。

### C5. 章级 README(01-source-map 与总览)

`book/examples/chapter-05/README.md`:目录结构表(7 行)、学习路径(00-env → 章正文对照 01→02→03 → exercises)、验证状态约定(机检级=已验证/加载级与 bpftool 特权查询=预期输出示例/运行级待验证)、走读树版本声明。`01-source-map/README.md`:45 个 .c 分区导览表(分区/文件/一句话职责)与三层 include 边界说明。

### C7. 章节标题固定(同 ch04 契约)

`# 第 5 章 eBPF 子系统内核代码结构`;十一小节名依设计 §3.1(学习目标/本章导读/一、源码地图:布局与所有权边界/二、加载路径追踪:BPF_PROG_LOAD 的一站一停/三、映射生命周期:从一个 fd 到一块内存/四、实战:把走读变成可复现的动作/可视化:源码树全景与加载调用流/常见问题与排错/课后扩展任务/总结/本章参考来源)。

### C8. 交叉链接(同 ch04 契约)

章首「本章独立示例已全部落地到 [`book/examples/chapter-05/`](../examples/chapter-05/)」;每段主轴末尾「**独立示例**」链接;正文代码块/切片前后标注对应文件路径;练习节链接 exercises/ 两目录;附录 B/C 链接保留。硬性核对:`grep -c "](../examples/chapter-05/" ≥ 8`、`grep -c "](../appendix/" ≥ 1`。

## Tasks

### Task 1: 00-env 环境自检 + 章级 README 骨架

- [x] Task 1: 00-env 环境自检 + 章级 README 骨架
**tasks.md 条目**:「Include a source-reading walkthrough with commands for navigating a local Linux tree.」(00-env 部分)+ C5 总览骨架。

**Files:**
- Create: `book/examples/chapter-05/00-env/check-env.sh`(可执行)
- Create: `book/examples/chapter-05/README.md`

**Interfaces:**
- Produces: check-env.sh 分级输出(编译级/机检级/加载级三档),供正文四节引用其输出示例;README 骨架含验证状态占位段(Task 11 回填)。

- [x] **Step 1: 写 `check-env.sh`**——分级诊断:①走读树:`WALK_TREE` 默认 `/work/linux/linux-6.6.78`,检查 `Makefile` 存在并 grep `^SUBLEVEL` 打印版本、`kernel/bpf/syscall.c|verifier.c` 在场性;②工具:grep/sed/wc/bpftool(缺失给降级提示:仅影响四节 introspection 预期输出);③BTF:`/sys/kernel/btf/vmlinux` 与 `CONFIG_DEBUG_INFO_BTF`(读 /boot/config-*);④权限档位:uid、`kernel.unprivileged_bpf_disabled` 值(打印并解释 2=非特权禁用,回指正文一节)。全部只读;无走读树时打印 kernel.org 获取指引(git clone/tar 两种)并退出码 2。
- [ ] **Step 2: 实跑验证(硬性)**

Run: `bash -n book/examples/chapter-05/00-env/check-env.sh && bash book/examples/chapter-05/00-env/check-env.sh; echo "exit=$?"`
Expected: 各级 PASS/降级提示正确;本机 exit=0(树在、工具齐、BTF 在、非特权档)。

- [ ] **Step 3: 章级 README 骨架**——C5 结构,验证状态段写占位句「机检汇总由 Task 11 回填」。

- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-05/
git commit -m "feat(ch05): 00-env 环境自检与章级示例 README 骨架"
```

---

### Task 2: 01-source-map 源码树地图(citations.txt 全量清单)

- [x] Task 2: 01-source-map 源码树地图(citations.txt 全量清单)
**tasks.md 条目**:「Write full chapter prose introducing eBPF subsystem source layout and ownership boundaries.」(资产部分)。

**Files:**
- Create: `book/examples/chapter-05/01-source-map/README.md`
- Create: `book/examples/chapter-05/01-source-map/citations.txt`

**Interfaces:**
- Produces: citations.txt(C1 格式,43 行左右,实测修正后为准)——Task 3 机检与 Task 11 回填的唯一数据源;01-source-map/README.md 的分区表供正文一节引用。

- [ ] **Step 1: 写 README.md**——C5 口径:①分区导览表(六区:入口分发 syscall.c/验证 verifier.c/执行与 JIT core.c/映射实现 arraymap.c·hashtab.c·ringbuf.c 等/BTF btf.c·与本地存储/其他 cgroup·netdev·struct_ops;每文件一句话职责,行数列实测)②三层 include 边界(`include/uapi/linux/bpf.h` UAPI 契约 / `include/linux/bpf.h` 内核对象 / `include/linux/bpf_types.h` 注册表 X-macro;各一段说明谁允许包含谁)③双展开现场说明(syscall.c 两处 include、两张表,引 Task 5 正文)。
- [ ] **Step 2: 逐行实测 citations.txt**(C1 清单)——对每行执行 `grep -F -q -- "<sym>" "/work/linux/linux-6.6.78/<file>"`,未命中即修清单(换真实符号)并留痕;最终 `grep -vc "^#\|^$" citations.txt` = 实际锚点数。
- [ ] **Step 3: 提交**

```bash
git add book/examples/chapter-05/01-source-map/
git commit -m "feat(ch05): 源码树地图资产与全量引用锚点清单"
```

---

### Task 3: verify-citations.sh 符号机检(本章「编译级」等价物)

- [x] Task 3: verify-citations.sh 符号机检(本章「编译级」等价物)
**tasks.md 条目**:「Main-agent review for kernel path accuracy and conservative version language.」的机检基座。

**Files:**
- Create: `book/examples/chapter-05/scripts/verify-citations.sh`(可执行)

**Interfaces:**
- Consumes: C1 citations.txt。
- Produces: `TOTAL/PASS/FAIL/WALK_TREE/TREE_VERSION` 汇总行——正文四节输出示例与 README 回填的固定口径。

- [ ] **Step 1: 写脚本**(C2 契约逐条落地;中文注释四类要点;`set -u`;禁止对树写入)。
- [ ] **Step 2: 首跑(硬性)**

Run: `bash -n book/examples/chapter-05/scripts/verify-citations.sh && bash book/examples/chapter-05/scripts/verify-citations.sh; echo "exit=$?"`
Expected: 全部 PASS,`FAIL=0`,exit=0。任何 FAIL → 回 Task 2 修清单(不是改脚本),重跑至全绿。

- [ ] **Step 3: 提交**

```bash
git add book/examples/chapter-05/scripts/
git commit -m "feat(ch05): 符号机检脚本(引用锚点全绿)"
```

---

### Task 4: 02-load-path 切片器

- [x] Task 4: 02-load-path 切片器
**tasks.md 条目**:「Trace the load path from `BPF_PROG_LOAD` through `kernel/bpf/syscall.c`, verifier, JIT selection, and attach setup.」(资产部分)。

**Files:**
- Create: `book/examples/chapter-05/02-load-path/trace-load-path.sh`(可执行)
- Create: `book/examples/chapter-05/02-load-path/README.md`

**Interfaces:**
- Produces: C3 六站输出——正文二节逐站引用其行号与代码段;README 记录本机实跑首尾各 10 行作为「预期输出示例(已在本机 6.6.78 树实跑)」。

- [ ] **Step 1: 写脚本**——C3 结构;六站锚点与 sed 区间取自「现状基线」表(区间实现期用 grep -n 实测校准,校准值写进脚本注释);每站 ≤25 行提取。
- [ ] **Step 2: 实跑(硬性)**

Run: `bash -n .../trace-load-path.sh && bash .../trace-load-path.sh | tail -3; echo "exit=$?"`
Expected: `TREE_VERSION=v6.6.78 STATIONS=6`,exit=0,无 `NOT FOUND`。
- [ ] **Step 3: README**——用途/调用方法/预期输出示例(实跑摘录)/失败模式(树不在/行号漂移的处理)。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-05/02-load-path/
git commit -m "feat(ch05): 加载路径切片器(六站实跑通过)"
```

---

### Task 5: 03-map-ops 切片器

- [x] Task 5: 03-map-ops 切片器
**tasks.md 条目**:「Explain map create, lookup, update, delete, freeze, pin, and reference lifecycle at a source-code level.」(资产部分)。

**Files:**
- Create: `book/examples/chapter-05/03-map-ops/trace-map-ops.sh`(可执行)
- Create: `book/examples/chapter-05/03-map-ops/README.md`

**Interfaces:**
- Consumes: C3 契约(同构)。
- Produces: C3 六站输出——正文三节引用;README 同 Task 4 口径。

- [ ] **Step 1: 写脚本**——C3 六站(映射链);实现注记:freeze 站必须打印「自包含、无 ops 回调」职责注(与三节正文呼应)。
- [ ] **Step 2: 实跑(硬性)**——同 Task 4 口径,`STATIONS=6`。
- [ ] **Step 3: README**——同 Task 4 口径,补一张「命令 → 通用层 → ops 回调」三层小表。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-05/03-map-ops/
git commit -m "feat(ch05): 映射生命周期切片器(六站实跑通过)"
```

---

### Task 6: 正文——学习目标、导读与「一、源码地图」

- [x] Task 6: 正文——学习目标、导读与「一、源码地图」
**tasks.md 条目**:「Write full chapter prose introducing eBPF subsystem source layout and ownership boundaries.」(正文部分);回收 ch02:423、ch04:805。

**Files:**
- Create: `book/examples/../..` 无;Create: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`

**Interfaces:**
- Consumes: Task 2 的 01-source-map/README 分区表(正文引用不复制全文,链接+摘 3 行)。
- Produces: C7 标题与小节骨架;一节成品(7–9k);「本章导读」末尾写明前章预告回收(2.1 的宏、2.4 的源码地图、ch04 五座桥之一)。

- [ ] **Step 1: 建文件写目标+导读(2–2.5k)**——学习目标 4–5 条(能指认分区/能走通加载链/能走通映射链/能自检引用/能排错行号漂移);导读含「前四章在用门,本章拆门框」类比与前章债回收清单。
- [ ] **Step 2: 写一节(7–9k)**——按 01-source-map/README 六分区展开,每区:背景问题→文件清单与职责→关键行摘录(≤15 行/处)→失败点/边界;核心兑现场面:`bpf_map_types[]` 与 `bpf_prog_types[]` 双展开逐段贴源码(syscall.c 两处 include 区间,各 ≤25 行)+ `include/linux/bpf_types.h` 逐行读一行实例(`BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER, ...)` 与 `BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops)`)——「一行宏展开成源码地图」;所有权边界三层 include 谁包含谁;`sysctl_unprivileged_bpf_disabled` 与本机 2 的对接(教师视角素材);双视角(教师视角=「20 万行为什么不散架:一张注册表+一个分发器」;学生动手=3 条命令生成自己的文件清单;检查点 3 问)。
- [ ] **Step 3: 篇幅自查**——`wc -m` 一节 7–9k 区间,超限压缩。
- [ ] **Step 4: 提交**

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md
git commit -m "docs(ch05): 目标导读与一节(源码地图与所有权边界)"
```

---

### Task 7: 正文——「二、加载路径追踪」

- [x] Task 7: 正文——「二、加载路径追踪」
**tasks.md 条目**:「Trace the load path from `BPF_PROG_LOAD` through `kernel/bpf/syscall.c`, verifier, JIT selection, and attach setup.」(正文);回收 ch02:131。

**Files:**
- Modify: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`

**Interfaces:**
- Consumes: Task 4 六站输出(行号/区间以切片器实跑校准值为准)。
- Produces: 二节成品(8–10k),六站与切片器一一对应。

- [ ] **Step 1: 写二节**——六站各:函数签名(标注 v6.6.78 行号)→职责→关键代码段(≤25 行)→失败出口(errno 表:EINVAL/EACCES/EPERM/EBUSY…,回指第 3 章日志);JIT 站兑现 ch02:131 并明确「JIT 细节第 6 章」;「attach setup」站讲清加载与挂载解耦(fd 返回≠已挂载,link_create/attach 是独立命令,回指第 4 章 C3 表);教师视角(为什么拆成「验证→选执行→编号→发 fd」四拍);学生动手(用切片器逐站对照);检查点 3 问。
- [ ] **Step 2: 篇幅自查**(8–10k)。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md
git commit -m "docs(ch05): 二节(BPF_PROG_LOAD 一站一停)"
```

---

### Task 8: 正文——「三、映射生命周期」

- [x] Task 8: 正文——「三、映射生命周期」
**tasks.md 条目**:「Explain map create, lookup, update, delete, freeze, pin, and reference lifecycle at a source-code level.」(正文);回收 ch02:289。

**Files:**
- Modify: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`

**Interfaces:**
- Consumes: Task 5 六站输出。
- Produces: 三节成品(7–9k);「命令→通用层→ops 回调」三层模型图,回指第 4 章 prog ops 表(镜像结构)。

- [ ] **Step 1: 写三节**——按生命周期七动作组织(create/lookup/update/delete/freeze/pin/release),每动作:入口命令→通用层函数→ops 回调(arraymap 对照)→失败出口;freeze 站写「自包含、无 ops 回调」对照面(为什么:冻结是通用属性不是 map 私有语义);release 站讲引用计数三层(uref/kref)+ 工作队列/RCU 延迟释放(为什么不能在 syscall 上下文直接 free:其他 CPU 可能还在读);pin 站讲 fd↔路径的往返与生命周期含义;教师视角(通用封装层+回调表是内核处理「多实现」的惯用法,第 4 章镜像);学生动手(切片器对照 arraymap);检查点 3 问。
- [ ] **Step 2: 篇幅自查**(7–9k)。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md
git commit -m "docs(ch05): 三节(映射生命周期七动作)"
```

---

### Task 9: 正文——「四、实战:把走读变成可复现的动作」(五层)

- [x] Task 9: 正文——「四、实战:把走读变成可复现的动作」(五层)
**tasks.md 条目**:「Include a source-reading walkthrough with commands for navigating a local Linux tree.」(正文部分)。

**Files:**
- Modify: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`

**Interfaces:**
- Consumes: Task 1 check-env.sh 输出、Task 3 机检汇总行、Task 4/5 切片器输出。
- Produces: 四节成品(6–8k);五层各落具体命令/脚本/表格;四段节奏(教师视角引入/核心命令原理/学生动手/检查点)。

- [ ] **Step 1: 写五层**——依赖环境(走读树获取两法:kernel.org tar 与 git clone;版本核对 `grep ^SUBLEVEL Makefile`;对照读者自己机器的保守措辞);「编译」环境=机检(verify-citations.sh 原理:为什么按符号不按行号;汇总行输出示例=本机实跑值);部署环境=定位(命令目录表:grep -n 找符号/sed -n 切区间/wc -l 看规模/ctags 与 cscope 可选工具化;「内联了的函数怎么办」一句预埋排错节);运行环境=两个切片器实跑与输出阅读;运行效果=走读地图输出示例 + `bpftool version`/`feature probe` 预期输出(标注「预期输出示例/运行级待验证」,特权查询说明 root 需求)。
- [ ] **Step 2: 篇幅自查**(6–8k)。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md
git commit -m "docs(ch05): 四节(走读实战五层)"
```

---

### Task 10: 正文——可视化、排错、课后任务、总结、参考

- [ ] Task 10: 正文——可视化、排错、课后任务、总结、参考
**tasks.md 条目**:「Add text source tree and call-flow visualization.」+「Add troubleshooting, summary, and references.」

**Files:**
- Modify: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`

**Interfaces:**
- Consumes: C7 小节名;排错回指第 3 章失败画廊口径。
- Produces: 后四节成品(可视化 1.5–2k/排错 2.5–3.5k/课后 1–1.5k/总结+参考 1.5–2k);课后节链接 exercises/ 两目录。

- [ ] **Step 1: 可视化两图**——①源码树全景文本图(kernel/bpf 六区+include 三层,每文件一句话)②BPF_PROG_LOAD 调用流文本图(用户态→syscall→verifier→JIT→fd→attach,标注每站失败出口);各配 ≥150 字解读段。
- [ ] **Step 2: 排错**——走读类五条:符号 grep 不到(改名/搬文件/内联的检索策略:先 grep 短名再缩小、查 `include/` 声明、用 ctags)、行号对不上(版本漂移定位法)、机检 FAIL 三类原因与修复、切片器 NOT FOUND 处理、bpftool 特权报错(预期,回指本机权限档)。
- [ ] **Step 3: 课后扩展任务节**——2 项练习的任务描述(参考实现链接到 exercises/,防剧透口径:正文只给任务,不给答案)。
- [ ] **Step 4: 总结+参考**——总结 5 点内化;参考来源按 STYLE.md 格式:kernel 源码树(v6.6.78)、Documentation/bpf/(index/btf/classic_vs_extended)、`include/uapi/linux/bpf.h`、第 2/3/4 章回指。
- [ ] **Step 5: 篇幅自查与提交**

Run: `wc -m book/chapters/05-ebpf-subsystem-kernel-code-structure.md`(全章,Expected: 达到 30k 以上,不足按预算补原理)

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md
git commit -m "docs(ch05): 可视化/排错/课后/总结/参考收尾"
```

---

### Task 11: 交叉链接、机检终跑与篇幅终检

- [ ] Task 11: 交叉链接、机检终跑与篇幅终检
**tasks.md 条目**:矩阵 4/5 条的收口与 C8 硬性核对。

**Files:**
- Modify: `book/chapters/05-ebpf-subsystem-kernel-code-structure.md`(交叉链接)
- Modify: `book/examples/chapter-05/README.md`(回填机检实测)

- [ ] **Step 1: C8 交叉链接落地与核对**

Run: `grep -c "](../examples/chapter-05/" book/chapters/05-ebpf-subsystem-kernel-code-structure.md && grep -c "](../appendix/" book/chapters/05-ebpf-subsystem-kernel-code-structure.md`
Expected: ≥8;≥1。

- [ ] **Step 2: 机检终跑(硬性)并回填 README**

Run: `bash book/examples/chapter-05/scripts/verify-citations.sh`
Expected: FAIL=0;把 TOTAL/PASS/FAIL 与树版本实测值替换进章级 README 占位句。

- [ ] **Step 3: 保守措辞全文复核(硬性)**

Run: `grep -n "已验证通过" book/chapters/05-ebpf-subsystem-kernel-code-structure.md book/examples/chapter-05/ -r`
Expected: 无命中(约定表原文自引除外,如有需在任务报告说明)。

- [ ] **Step 4: 篇幅终检(硬性)**

Run: `wc -m book/chapters/05-ebpf-subsystem-kernel-code-structure.md`
Expected: 35000–50000;低于下限补原理,高于上限压重复。

- [ ] **Step 5: 提交**

```bash
git add book/chapters/05-ebpf-subsystem-kernel-code-structure.md book/examples/chapter-05/
git commit -m "docs(ch05): 交叉链接、机检终跑回填与篇幅终检"
```

---

### Task 12: 两项课后练习参考实现

- [ ] Task 12: 两项课后练习参考实现
**tasks.md 条目**:STYLE.md 课后扩展任务约定(矩阵任务的练习口径)。

**Files:**
- Create: `book/examples/chapter-05/exercises/01-trace-freeze/{README.md,check-answer.sh}`(check-answer.sh 可执行)
- Create: `book/examples/chapter-05/exercises/02-extend-tracer/{README.md,trace-btf-load.sh,check-trace.sh}`(两脚本可执行)

**Interfaces:**
- Consumes: C4 符号表与接口。
- Produces: 两项完整参考实现,验证状态标注齐备。

- [ ] **Step 1: 练习①**——README 防剧透七段(任务描述与提示在前;参考实现思路独立成节在后;验证状态:check-answer.sh 机检级已验证/冻结的加载级效果待验证);check-answer.sh 按 C4 符号表对 WALK_TREE 逐符号 grep -F,汇总输出同 C2 口径。
- [ ] **Step 2: 练习①实跑(硬性)**:全 PASS,exit=0。
- [ ] **Step 3: 练习②**——trace-btf-load.sh 三站(C4);check-trace.sh = `bash -n` 断言 + 实跑输出 grep 三个锚点断言;README 防剧透同构。
- [ ] **Step 4: 练习②实跑(硬性)**:check-trace.sh 全绿,exit=0。
- [ ] **Step 5: 提交**

```bash
git add book/examples/chapter-05/exercises/
git commit -m "feat(ch05): 两项走读练习参考实现(机检全绿)"
```

---

### Task 13: 主审(kernel path accuracy + conservative version language)与 tasks.md 勾选(收口)

- [ ] Task 13: 主审(kernel path accuracy + conservative version language)与 tasks.md 勾选(收口)
**tasks.md 条目**:「Main-agent review for kernel path accuracy and conservative version language.」;统一勾选矩阵 7 条。

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`(勾选)
- 产出:审查结论进入最终回复(不创建报告文件)

- [ ] **Step 1: 逐项审计**(问题回对应任务修复后复验,全绿才进 Step 2):
  1. 教学完整性四条(STYLE.md):五层落位(四节五个层级各落到命令/脚本/表格);核心命令/脚本原理注释(bash 脚本四类要点抽查);双视角(一/二/三节「教师视角」「学生动手」+检查点,四节四段);课后练习(2 项目录、防剧透 README、参考实现实跑验证、验证状态标注)。
  2. **内核路径准确性(矩阵⑦主审重点)**:正文每个 `文件:符号` 引用抽查机检(verify-citations 全绿之外,人工抽查 10 处行号区间与切片器输出一致);每处行号带「(v6.6.78)」;跨版本断言零容忍(全文 grep「7.0」检查运行内核对接处是否保守措辞)。
  3. 结构级:C7 十一小节齐备;双展开现场两处贴码与 bpf_types.h 实例在场;可视化两图带解读;C8 链接数核对复跑。
  4. 术语与口径:译名一致;首现格式;「预期输出示例/运行级待验证」统一;机检级/加载级/运行级三档措辞不混用。
- [ ] **Step 2: 勾选 tasks.md**:「Chapter 5: eBPF Subsystem Kernel Code Structure」7 条全部置 `[x]`(仅当交付物真实存在且验收通过)。
- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch05): 勾选 Chapter 5 矩阵任务"
```

---

## 自审记录(writing-plans Self-Review)

1. **Spec 覆盖**:矩阵 7 条 → ①Task 2+6;②Task 4+7;③Task 5+8;④Task 1+9;⑤Task 10;⑥Task 10;⑦Task 3+13。无缺口。
2. **占位符扫描**:C1 中 `bpf_check_call-site` 占位行已附实现注(替换为 `bpf_check(&prog` 字面量);其余任务无 TBD/「类似 Task N」。
3. **类型一致性**:citations 行格式(C1)与 verify-citations.sh 解析(C2)一致;切片器接口 C3 与 Task 4/5/12 产出一致;汇总行口径 C2/Task 11 回填一致。

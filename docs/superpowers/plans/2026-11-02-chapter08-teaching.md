---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-11-02-chapter08-teaching-design.md
base-ref: 8a11de8
---

# 第 8 章完整教学章节(build 阶段)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 8 章《BTF 与 CO-RE 原理》从零写为 40–55k 字符(wc -m)的完整教学章节(机制章 + CO-RE 全链编译级实证),并落地 `book/examples/chapter-08/` 全部资产(方案 A 六资产):00-env 自检、01-btf-kinds 编译级剖析、02-vmlinux-h 生成、03-core-read 四件套(编译级全链)、04-relo-slicer(python)与 05-kernel-relo(bash)两个切片器、scripts/verify-citations.sh 符号机检、2 项练习(防剧透+机检)。

**架构:** 双轨资产——走读轨(两个切片器 + 符号机检,同 ch05/06/07 契约:05-kernel-relo 切内核 btf.c/verifier.c,04-relo-slicer 切自编对象 .BTF.ext 二进制)与实证轨(01/02/03 编译级产物:kind 指认、vmlinux.h 生成、CO-RE 读 task_struct 全链)。citations.txt 在章级树根,是机检唯一数据源;正文五节逐节对应资产,四节讲两台重定位引擎,五节实战五层串全链并给出缺失-BTF 回退策略。

**技术栈:** bash(grep/sed/wc)、C 双侧(BPF 侧 clang 18 `-g -target bpf`;用户侧 gcc + libbpf 1.3.0,`-lbpf -lelf -lz`)、python3 3.12(仅标准库 struct/subprocess,解析 .BTF.ext)、bpftool 7.7.0(btf dump/format c/gen skeleton)、llvm-objdump 18(段表探针)、Markdown(中文正文)。走读树 Linux 6.6.78。

## 范围与边界

- 本计划只覆盖 tasks.md「Chapter 8: BTF and CO-RE Principles」8 条;其余章另立计划。
- delta spec 无需变更,不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 不触碰第 1–7、9、17 章正文与 examples。
- 只修改:`book/chapters/08-btf-and-co-re-principles.md`(新建)、`book/examples/chapter-08/**`(新建)、`.gitignore`(追加示例可执行,逐任务)、`openspec/changes/generate-ebpf-book/tasks.md`(Task 14 勾选 8 条)。
- `.comet/**` 不入库(任何任务不得 add)。
- 不重复前章:vmlinux.h 构建链用法(ch07/ch09 已示范)只解释机制不重讲用法;BTF 字段登记簿(btf_record)回指 ch07 不展开;验证器 BTF 用途回指 ch03。

## 全局约束

1. **语言**:正文与脚本注释全中文;术语 map=映射、helper=辅助函数;首现「英文(中文解释)」。
2. **走读树口径(硬性)**:锚定 `/work/linux/linux-6.6.78`,行号标「(v6.6.78)」;**机检按符号不按行号**(行号仅供人读,机检 `grep -F` 符号名);运行内核(本机 7.0.0-38)差异保守措辞。
3. **验证口径三档(硬性,设计 §4)**:
   - 编译级已验证(本机可复现,无 root):`bpftool btf dump`、`format c` 生成 vmlinux.h、clang `-g -target bpf` 产出 `.BTF`/`.BTF.ext`/`.rel.BTF.ext`、skeleton 生成、gcc 链接、切片器/机检实跑、只读 CONFIG/BTF/ELF 段读取——全部以实跑输出为证;
   - 运行级待验证:skeleton 加载、attach、CO-RE 字段修正后实际读取、compat 分支运行行为——一律标「预期输出示例 / 运行级待验证」(`unprivileged_bpf_disabled=2` 无 root);
   - 方法论级:pahole DWARF→BTF 转换——给流程与判读法,标注「本机未装 pahole,转换级验证需自备工具链」。
   - **零「已验证通过」式运行级表述**;relo_core.c 双实现只写「逻辑同源、各自维护,接口对齐 uapi」,禁写拷贝关系。
4. **篇幅**:40–55k(wc -m);预算:目标+导读 2–2.5k、一 8–10k、二 7–9k、三 9–11k、四 6–8k、五 7–9k、可视化 1.5–2k、排错 2.5–3.5k、课后 1–1.5k、总结+参考 1.5–2k;分节服从章级。
5. **叙事**:双视角(教师视角/学生动手)+检查点 3 问落「一、二、三、四、五」五节;各段主轴末「**独立示例**」链接(一→00-env/02-vmlinux-h;二→01-btf-kinds;三→03-core-read/04-relo-slicer;四→05-kernel-relo;五→全部资产)。
6. **本机实况标注**:类型 ID(task_struct id=156855)、结构尺寸(9920)、/sys/kernel/btf 文件数(110)、段尺寸等数值统一标「本机实况,其他内核数值不同」;探针输出标「clang 18 实测」。
7. **前章埋线回收(硬性,设计 §1.2 表)**:ch02 02:95/02:851/02:853、ch07 07:255/07:603/07:36、ch07/ch09 构建链七条必须按表落位于一/三/四/五节+总结。
8. **任务对应**:每任务引用 tasks.md 矩阵片段;Task 14 统一勾选 8 条;每任务一提交,提交信息前缀 `feat(ch08):`(资产)或 `docs(ch08):`(正文/勾选)。
9. **产物入库边界**:构建产物(vmlinux.h/**.skel.h/**.bpf.o)已由 `.gitignore` 全局规则覆盖(`book/examples/**/vmlinux.h`、`book/examples/**/*.skel.h`、`book/examples/**/*.o`),**新增可执行须逐任务追加 .gitignore**(ch02/07/17 先例);02 生成的 vmlinux.h 不入库,README 记录生成方法与规模统计。

## 现状基线(编写计划时已在 6.6.78 树/本机实测,与设计文档「已确认事实」一致)

| 事实 | 值 |
|---|---|
| HEAD | `8a11de8`(ch08 设计文档提交;后续基线) |
| 走读树锚点·内核引擎 | `bpf_core_apply` btf.c:8423;`bpf_core_find_cands` :8336;`bpf_core_types_are_compat` :8094;`check_core_relo` verifier.c:15718(调用点 :15778 调 bpf_core_apply、:15817 接线 err=check_core_relo);`btf_parse_vmlinux` btf.c:5720;`btf_parse_module` :5789(装载点 :7378) |
| 走读树锚点·libbpf 引擎 | `bpf_object__collect_prog_relos` libbpf.c:4236(解析 .BTF.ext :6763);`bpf_object__relocate_core` :5783(调用点 :6526);`bpf_gen__record_relo_core` :7296(轻骨架记录);relo_core.c:`__bpf_core_types_are_compat` :146、`bpf_core_parse_spec` :263、`bpf_core_patch_insn` :1024、`bpf_core_format_spec` :1156、`bpf_core_calc_relo_insn` :1280、`__bpf_core_types_match` :1544 |
| 走读树锚点·UAPI | `enum bpf_core_relo_kind` uapi bpf.h:7262(12 种 0–11,`BPF_CORE_FIELD_BYTE_OFFSET = 0` :7263);`struct bpf_core_relo` :7326(四字段);attr 载体 :1463-1466(`core_relo_cnt` :1463/`core_relos` :1465/`core_relo_rec_size` :1466);`attach_btf_obj_fd` :1461;`enum` BTF kind 块 uapi btf.h:60-79(UNKN=0…ENUM64=19,NR_BTF_KINDS=20);`BTF_INFO_KIND/VLEN/KFLAG` :55-57 |
| 走读树锚点·宏族 | bpf_core_read.h:`BPF_CORE_READ_BITFIELD_PROBED` :70、`BPF_CORE_READ_BITFIELD` :88、`bpf_core_field_exists` :132、`bpf_core_type_exists` :185、`bpf_core_enum_value_exists` :216、`bpf_core_read` :246、`BPF_CORE_READ_INTO` :351;`btf_type_by_id` include/linux/btf.h:488 |
| 内核文档 | `Documentation/bpf/btf.rst`、`clang-notes.rst`、`llvm_reloc.rst` 三件在场(ls 实证) |
| 本机工具链 | clang 18.1.3、llvm-objdump 18、bpftool 7.7.0、libbpf 1.3.0、python3 3.12.3;**pahole 不在 PATH**(方法论级档);无 root、unprivileged_bpf_disabled=2 |
| 本机 BTF 实况 | /sys/kernel/btf 110 文件(vmlinux 7119843 字节 mode 444 全局可读 + 109 模块);`bpftool btf dump file /sys/kernel/btf/vmlinux` 401978 行;task_struct id=156855 size=9920 vlen=274;`format c` 生成 vmlinux.h 3500327 字节;clang `-g` 自编对象产 `.BTF`(0x17a)+`.BTF.ext`(0x9c)+`.rel.BTF.ext`(0x60);bpftool 读自编对象 BTF:STRUCT 'my_type' size=24 vlen=3 带位偏移 |
| CONFIG | DEBUG_INFO_BTF=y、DEBUG_INFO_BTF_MODULES=y、MODULE_ALLOW_BTF_MISMATCH 未设 |
| .gitignore | vmlinux.h/**.skel.h/**.o 全局规则在;可执行逐文件登记(ch02/07/17 先例) |
| 可复用契约 | ch05/06/07 的 C1(citations)/C2(机检)/C3(切片器)/C4(练习)/C5(章级 README)/C7(标题)/C8(交叉链接)+ ch09 的 libbpf-bootstrap Makefile 模式 |

## 共享接口契约(跨任务定义)

### C1. citations.txt(chapter-08 树根,Task 7 产出,机检与终检消费)

行格式 `<文件>|<符号>|<中文说明>`,`#` 注释;机检=文件存在+`grep -F -q` 符号(不按行号)。约 40 行五组(锚点已实测,Task 7 逐行实测修正留痕):
- ①BTF 基础与解析:`kernel/bpf/btf.c` btf_parse_vmlinux(:5720)、btf_parse_module(:5789);
- ②kind 体系:`include/uapi/linux/btf.h` BTF_KIND_INT(:61 枚举块 :60-79)、BTF_INFO_KIND(:55);
- ③CO-RE 编译期:`tools/lib/bpf/bpf_core_read.h` :70/:88/:132/:185/:216/:246/:351 七宏;`include/uapi/linux/bpf.h` :7262/:7326;
- ④libbpf 引擎:`tools/lib/bpf/relo_core.c` :146/:263/:1024/:1156/:1280/:1544;`tools/lib/bpf/libbpf.c` :4236/:5783;
- ⑤内核引擎+UAPI:`kernel/bpf/btf.c` :8094/:8336/:8423;`kernel/bpf/verifier.c` :15718/:15778/:15817;`include/uapi/linux/bpf.h` :1463-1466;`tools/lib/bpf/gen_loader.c` :970。
说明列可含「文件:行号(v6.6.78)」供人读;机检仅匹配符号列。

### C2. verify-citations.sh(scripts/,Task 7)

同 ch05/06/07 C2 逐字(数据源 `../citations.txt`,逐行:先验文件存在再 `grep -F -q` 符号;汇总行 `TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78`;FAIL≠0 exit 1;头部「只读源码树,不构建、不加载」+失败三类注释:文件不存在/符号未命中/树版本不符;bash 中文注释四类要点;支持 `WALK_TREE` 环境变量覆盖)。

### C3. 切片器接口(04-relo-slicer python 五站、05-kernel-relo bash 五站,Task 5/6)

- **04 `slice-relos.py`**:`python3 slice-relos.py <path/to/.bpf.o>`;**仅标准库**(subprocess 调 `llvm-objdump -h` 拿段表,struct 解析 .BTF.ext 字节);五站:①ELF 段布局(.BTF/.BTF.ext/.rel.BTF.ext 尺寸)②btf_ext 头(magic 0xEB9F/长度/各段偏移)③core_relo 记录解析(insn_off/type_id/access_str_off/kind)④kind 对照 uapi 枚举(0–11 名称表)⑤access string 解码(字符串表偏移→`0:1:2` 形式)与源码字段对照;断言:记录数>0、kind∈[0,11];失败 exit 1(文件不存在/无 .BTF.ext/记录数=0);输出中文站名「## 站 n:…」+汇总 `RELOS=<n>`。
- **05 `trace-kernel-relo.sh`**:`bash trace-kernel-relo.sh [WALK_TREE]`,只读树;五站:①内核引擎入口 `bpf_core_apply`(btf.c:8423 区间)②候选查找 `bpf_core_find_cands`(:8336)与兼容 `bpf_core_types_are_compat`(:8094)③verifier 接线 `check_core_relo`(verifier.c:15718 区间+调用点 :15778/:15817 各 1 行)④UAPI 载体(bpf.h:1463-1466 区间)⑤轻骨架提交(gen_loader.c:970 区间);同 ch05/06/07 C3 口径:每站「## 站名」+`grep -n` 定位(标 v6.6.78)+`sed` 区间(≤25 行/段)+一行中文职责注;定位失败 `STATION n: NOT FOUND`+exit 1;汇总 `TREE_VERSION=v6.6.78 STATIONS=5`。

### C4. 练习接口(exercises/,Task 14)

- **01-relo-kind-detect/**:`README.md`(防剧透:任务=对预置源码自建的 demo.bpf.o 用 04 切片器判读每条重定位记录「补丁什么、怎么补」(kind 语义);提示=先跑切片器五站、再对照 uapi :7262 十二种;参考思路独立成节后置)+`relo-demo.bpf.c`(刻意含 ≥3 种 kind:字段字节偏移/字段存在/类型 ID)+`Makefile`(仿 03,产 demo.bpf.o,构建产物不入库)+`check-relo-kind.sh`(实跑切片器,断言输出含指定 kind 值与记录数 ≥3;负路径 exit 1;汇总 PASS/FAIL)。
- **02-core-compat/**:`README.md`(防剧透同构:任务=仿 03 写跨版本兼容分支)+`compat.bpf.c`(用 `bpf_core_field_exists` :132/`bpf_core_type_exists` :185 构造兼容分支,参考实现完整可编译)+`Makefile`(编译级产 compat.bpf.o)+`check-compat.sh`(两级断言:①`clang` 编译级通过;②机检锚点 grep -F 断言两个宏与注释锚点在场)。

### C5. 章级 README 骨架(Task 1)

同 ch07 C5:资产目录表(**九行**:00-env/01-btf-kinds/02-vmlinux-h/03-core-read/04-relo-slicer/05-kernel-relo/scripts/exercises/citations.txt,含「内容/验证档/落地状态」列,结构与设计文档 §2 资产表对齐,头部链接 `docs/superpowers/specs/2026-11-02-chapter08-teaching-design.md` §2)/学习路径(建议顺序 00→01→02→03→04→05)/三档验证状态约定(本章细化:编译级已验证/预期输出示例·运行级待验证/方法论级 pahole)/机检汇总占位「由 Task 13 回填」/走读树声明(/work/linux/linux-6.6.78,v6.6.78 行号)/安全声明(本示例只读 BTF 与编译,不加载程序;03 加载运行为预期档需 root)。

### C6. 示例资产规格(方案 A,Task 1–4)

- **00-env `check-env.sh`**(Task 1,只读无 root):五段——①走读树版本+本章走读文件在场(kernel/bpf/btf.c、tools/lib/bpf/relo_core.c、tools/lib/bpf/bpf_core_read.h、include/uapi/linux/btf.h)②工具链(bpftool btf dump 探针:对 /sys/kernel/btf/vmlinux 取第 1 行;clang `-g -target bpf` 空文件探针;python3 --version)③BTF 家族(/sys/kernel/btf 文件计数、vmlinux 大小、模块 BTF 抽样 1 个)④CONFIG 三项(只读 /boot/config-$(uname -r) 或 /proc/config.gz)⑤**pahole 缺席声明**(command -v pahole;不在场则打印方法论级档说明);三档结论(编译级可跑/加载级需 root/方法论级 pahole);无树 exit 2。
- **01-btf-kinds**(Task 2):`kinds.bpf.c` 刻意覆盖多 kind——struct/union/enum/typedef/const/ptr/array/全局变量(VAR+DATASEC)/func/func_proto/btf_decl_tag;`Makefile` 目标 all=产 kinds.bpf.o(clang -g -O2 -target bpf -D__TARGET_ARCH_x86,仅编译不链接);`README.md` 覆盖表(源码项→kind 名→`bpftool btf dump file kinds.bpf.o` 输出行摘录)+类型 ID 编址观察+剩余 kind(FWD/FLOAT/ENUM64/TYPE_TAG)从 vmlinux BTF 抽样指认命令。
- **02-vmlinux-h**(Task 3):`gen-vmlinux-h.sh`(`bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`+规模统计输出:字节/行数/struct 声明数);`README.md` 生成方法、3.5MB 规模统计(标「本机实况」)、头文件结构导读(enum/struct 声明区/typedef 兼容区)、**回收 ch07/ch09 构建链**(那两章 Makefile 里同款 `bpftool btf dump` 步骤的机制解释入口);vmlinux.h 产物不入库(全局规则)。
- **03-core-read 四件套**(Task 4):`core.bpf.c`——SEC("tracepoint/sched/sched_process_exec"),`bpf_get_current_task_btf()` 取 task_struct 指针,`BPF_CORE_READ` 读 pid/comm,含 `bpf_core_field_exists` 兼容分支演示;`loader.c`——libbpf skeleton 全链(open→load→attach→poll 一轮→销毁);`Makefile`——ch09 bootstrap 模式(vmlinux.h←bpftool btf dump;core.bpf.o←clang -g;skeleton←bpftool gen skeleton;core_read←gcc -lbpf -lelf -lz);`README.md`——头部声明「编译级已验证;加载运行为预期(需 root)」+字段漂移说明(task_struct 字段随版本漂移正是 CO-RE 用武之地:字段在本机 BTF 存在才可编译,这正是本章要教的工作流)+运行命令与预期输出示例(预期档标注)。可执行 `core_read` 逐文件追加 .gitignore。

### C7. 章节标题固定

文件 `book/chapters/08-btf-and-co-re-principles.md`(新建);`# 第 8 章 BTF 与 CO-RE 原理`;十一小节:学习目标/本章导读/一、BTF:内核的类型说明书/二、BTF kind 体系与类型 ID 编址/三、CO-RE 编译期:从宏到重定位记录/四、两台重定位引擎/五、实战五层:一条 BPF_CORE_READ 的完整旅程/可视化:CO-RE 全链与两引擎对照/常见问题与排错/课后扩展任务/总结/本章参考来源。骨架用 HTML 注释占位未写小节。

### C8. 交叉链接

章首示例目录链接、各段主轴末「独立示例」、附录 B/C ≥1;`grep -c "](../examples/chapter-08/" ≥ 8`、`grep -c "](../appendix/" ≥ 1`。

## Tasks

### Task 1: 00-env 自检 + 章级 README 骨架

- [x] Task 1: 00-env 自检 + 章级 README 骨架

**tasks.md 条目**:矩阵④(独立示例基座)+⑤(CONFIG/bpftool btf dump 前置);C5/C6。

**Files:** Create `book/examples/chapter-08/00-env/check-env.sh`(可执行)、`book/examples/chapter-08/README.md`

- [ ] **Step 1: check-env.sh**(C6 规格;五段顺序;头部「只读源码树与系统状态,不构建、不加载」;无树 exit 2;三档结论打印)。
- [ ] **Step 2: 实跑(硬性)**——`bash -n` + 实跑 exit 0;输出与基线一致(110 文件、CONFIG 两 y 一未设、pahole 未装方法论档声明)。
- [ ] **Step 3: 章级 README 骨架**(C5;九行资产表引用设计文档 §2)。
- [ ] **Step 4: 提交** `feat(ch08): 00-env 环境自检与章级示例 README 骨架`

### Task 2: 01-btf-kinds 编译级剖析

- [x] Task 2: 01-btf-kinds 编译级剖析

**tasks.md 条目**:矩阵①(type IDs/field offsets 资产面)。

**Files:** Create `book/examples/chapter-08/01-btf-kinds/{kinds.bpf.c,Makefile,README.md}`(构建产物 kinds.bpf.o 不入库,全局规则已覆盖)

- [ ] **Step 1: kinds.bpf.c + Makefile**(C6 规格;注释中文,头部声明「编译级已验证;本示例仅编译 BPF 对象,不加载」;11 类 kind 覆盖项逐一加中文行注)。
- [ ] **Step 2: 实跑(硬性)**——make 产 kinds.bpf.o;`bpftool btf dump file kinds.bpf.o` 全程输出贴报告,逐一指认 kind 与类型 ID 递增;`bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 "FWD\|FLOAT\|ENUM64\|TYPE_TAG"` 抽样指认剩余 kind;`llvm-objdump -h kinds.bpf.o` 确认 `.BTF` 在场。
- [ ] **Step 3: README**(覆盖表含 bpftool 输出摘录;「本机实况」标注)。
- [ ] **Step 4: 提交** `feat(ch08): BTF kind 剖析示例(自编对象指认+vmlinux 抽样)`

### Task 3: 02-vmlinux-h 生成

- [x] Task 3: 02-vmlinux-h 生成

**tasks.md 条目**:矩阵③(vmlinux.h)+⑤(bpftool btf dump);回收 ch07/ch09 构建链。

**Files:** Create `book/examples/chapter-08/02-vmlinux-h/{gen-vmlinux-h.sh,README.md}`(脚本可执行;vmlinux.h 产物不入库)

- [ ] **Step 1: gen-vmlinux-h.sh**(C6 规格;`bpftool btf dump file /sys/kernel/btf/vmlinux format c`;产 vmlinux.h 于本目录;末尾打印规模统计:wc -c/-l、grep -c "^struct")。
- [ ] **Step 2: 实跑(硬性)**——bash -n + 实跑产 vmlinux.h;规模统计与基线一致(≈3500327 字节,标「本机实况」);`git status` 确认 vmlinux.h 被 .gitignore 忽略。
- [ ] **Step 3: README**(生成方法/结构导读/回收 ch07/ch09 Makefile 同款步骤)。
- [ ] **Step 4: 提交** `feat(ch08): vmlinux.h 生成脚本与结构导读(回收 ch07/ch09 构建链)`

### Task 4: 03-core-read 四件套(集成示例)

- [x] Task 4: 03-core-read 四件套(集成示例)

**tasks.md 条目**:矩阵③(读内核结构体字段的 CO-RE 示例)+④。

**Files:** Create `book/examples/chapter-08/03-core-read/{core.bpf.c,loader.c,Makefile,README.md}`;Modify `.gitignore`(追加 `book/examples/chapter-08/03-core-read/core_read`)

- [ ] **Step 1: 四件套**(C6 规格④;注释中文;Makefile ch09 bootstrap 四步:vmlinux.h→.bpf.o→.skel.h→可执行)。
- [ ] **Step 2: 编译级实跑(硬性)**——`make` 全链零告警产 core_read;`llvm-objdump -h core.bpf.o` 断言 `.BTF.ext` 尺寸>0(编译级证据,标「clang 18 实测」);`bpftool btf dump file core.bpf.o | head` 可读;`bpftool gen skeleton core.bpf.o` 产 .skel.h。运行命令与输出写「预期输出示例 / 运行级待验证」。
- [ ] **Step 3: README**(头部声明+字段漂移说明+预期输出示例)。
- [ ] **Step 4: 提交** `feat(ch08): CO-RE 读 task_struct 字段示例(vmlinux.h+四件套编译级全链)`

### Task 5: 04-relo-slicer 切片器(python 五站)

- [ ] Task 5: 04-relo-slicer 切片器(python 五站)

**tasks.md 条目**:矩阵②(libbpf 重定位行为资产面)。

**Files:** Create `book/examples/chapter-08/04-relo-slicer/{slice-relos.py,README.md}`(脚本可执行)

- [ ] **Step 1: slice-relos.py**(C3 python 五站;仅标准库;中文注释;-m py_compile 通过)。
- [ ] **Step 2: 实跑(硬性)**——`python3 -m py_compile slice-relos.py`;`python3 slice-relos.py ../03-core-read/core.bpf.o` exit 0、五站输出、`RELOS=<n>` n>0、kind 全落 0–11;负路径(`/nonexistent.o`、无 .BTF.ext 的普通 .o)exit 1。
- [ ] **Step 3: README**(用途/调用/五站解读/失败模式/依赖=llvm-objdump+python3 标准库)。
- [ ] **Step 4: 提交** `feat(ch08): .BTF.ext 重定位记录切片器(python 五站实跑通过)`

### Task 6: 05-kernel-relo 切片器(bash 五站)

- [ ] Task 6: 05-kernel-relo 切片器(bash 五站)

**tasks.md 条目**:矩阵②(kernel BTF documentation 走读面)。

**Files:** Create `book/examples/chapter-08/05-kernel-relo/{trace-kernel-relo.sh,README.md}`(脚本可执行)

- [ ] **Step 1: trace-kernel-relo.sh**(C3 bash 五站:①bpf_core_apply btf.c:8423 ②bpf_core_find_cands :8336 + bpf_core_types_are_compat :8094 ③check_core_relo verifier.c:15718 + 调用点 :15778/:15817 ④UAPI 载体 bpf.h:1463-1466 ⑤gen_loader.c:970;基线行号自查校准)。
- [ ] **Step 2: 实跑(硬性)**——bash -n + 实跑 `STATIONS=5` exit 0 无 NOT FOUND;`WALK_TREE=/nonexistent` exit 1。
- [ ] **Step 3: README**(用途/调用/实跑摘录首尾 10 行/失败模式)。
- [ ] **Step 4: 提交** `feat(ch08): 内核态重定位引擎切片器(五站实跑通过)`

### Task 7: citations.txt 全量 + verify-citations.sh 符号机检

- [ ] Task 7: citations.txt 全量 + verify-citations.sh 符号机检

**tasks.md 条目**:矩阵②(内核 BTF 文档与 libbpf 重定位行为锚点收口)。

**Files:** Create `book/examples/chapter-08/citations.txt`、`book/examples/chapter-08/scripts/verify-citations.sh`(可执行)

- [ ] **Step 1: citations.txt 逐行实测**(C1 五组约 40 行;格式「文件|符号|说明」;`grep -vc "^#\|^$"` 计数;实测修正留痕注于组注释)。
- [ ] **Step 2: verify-citations.sh + 首跑(硬性)**——C2 逐字;首跑全 PASS exit 0;`WALK_TREE=/nonexistent` exit 1;**机检按符号不按行号**。
- [ ] **Step 3: 提交** `feat(ch08): 引用锚点清单与符号机检(五组全绿)`

### Task 8: 正文——C7 骨架 + 目标导读 + 一节「BTF:内核的类型说明书」

- [ ] Task 8: 正文——C7 骨架 + 目标导读 + 一节「BTF:内核的类型说明书」

**tasks.md 条目**:矩阵①(BTF goals/DWARF-to-BTF)+⑤(CONFIG_DEBUG_INFO_BTF/pahole/bpftool btf dump)。

**Files:** Create `book/chapters/08-btf-and-co-re-principles.md`

- [ ] **Step 1: C7 骨架(HTML 注释占位十一小节)+ 章首示例目录链接**。
- [ ] **Step 2: 目标+导读(2–2.5k)**——目标 4–5 条;导读回收埋线:ch02 02:95(跨版本访问由 CO-RE 修正)、07:603(vmlinux.h 四问在本章作答)、07:36(第 9 章起依赖本章机制)。
- [ ] **Step 3: 一节(8–10k)**——按设计 §3.2:BTF 三重身份(DWARF 大且只在编译期 vs BTF 紧凑且常驻内核;验证器 ctx 校验/attach_btf_id/字段级能力登记簿——回指 ch07:255 不展开);教师视角(为什么不用 DWARF);DWARF→BTF 转换=方法论级(pahole -J 流程+判读法+「本机未装 pahole」标注;CONFIG_DEBUG_INFO_BTF 在内核构建期由 pahole 完成);vmlinux+模块双层 BTF 实况(110 文件/7119843 字节/「本机实况」标注;`btf_parse_vmlinux` btf.c:5720、`btf_parse_module` :5789);`bpftool btf dump` 三用法预告(file 对象/file /sys/format c);学生动手(00-env 自检+02 生成);检查点 3 问;节末链接 00-env/02-vmlinux-h。
- [ ] **Step 4: 篇幅自查(wc -m 一节 8–10k)+锚点行号实测**。
- [ ] **Step 5: 提交** `docs(ch08): 目标导读与一节(BTF 类型说明书)`

### Task 9: 正文——「二、BTF kind 体系与类型 ID 编址」

- [ ] Task 9: 正文——「二、BTF kind 体系与类型 ID 编址」

**tasks.md 条目**:矩阵①(type IDs/field offsets)。

**Files:** Modify 同文件

- [ ] **Step 1: 写二节(7–9k)**——`struct btf_type` 三元组(name_off/info:kind+vlen 位域/size);19 种 kind 分四组(uapi btf.h :60-79:基础 INT/PTR/ARRAY/STRUCT/UNION/ENUM/ENUM64/FWD、修饰 VOLATILE/CONST/RESTRICT、功能 TYPEDEF/FUNC/FUNC_PROTO、扩展 VAR/DATASEC/FLOAT/DECL_TAG/TYPE_TAG);btf_member 与 bits_offset 位域偏移(01 实物:'my_type' size=24 vlen=3 带位偏移);类型 ID 编址(void=0、从 1 顺序递增、跨 Section 共享命名空间;task_struct id=156855 实况+「本机实况,其他内核数值不同」标注);`btf_type_by_id`(include/linux/btf.h:488);教师视角(为什么 ID 而不是名字)/学生动手(01 指认命令)/检查点;节末链接 01-btf-kinds。
- [ ] **Step 2: 篇幅自查+行号实测**。
- [ ] **Step 3: 提交** `docs(ch08): 二节(kind 体系与类型 ID 编址)`

### Task 10: 正文——「三、CO-RE 编译期:从宏到重定位记录」

- [ ] Task 10: 正文——「三、CO-RE 编译期:从宏到重定位记录」

**tasks.md 条目**:矩阵①(CO-RE relocation)+②+③(vmlinux.h 工作流)。

**Files:** Modify 同文件

- [ ] **Step 1: 写三节(9–11k)**——问题陈述(BCC 旧路:运行期编译/头文件依赖/每次启动重编;CO-RE 目标=编译一次处处运行);工作流四件:vmlinux.h 生成(02 实物 3.5MB;回收 ch07/ch09 构建链)→ `-g` 编译(没有 -g 就没有 .BTF.ext,实证探针)→ clang 内建 `__builtin_preserve_access_index`(宏族背后)→ 加载期补丁(预告四节);宏族逐个(bpf_core_read.h :246 读值/:351 读入/:132 存在性/:185 类型存在/:216 枚举值存在/:88 与 :70 位域;埋线回收 ch02:853→bpf_core_field_exists);**BTF.ext 重定位记录**:`struct bpf_core_relo` :7326 四字段(insn_off/type_id/access_str_off/kind)、`enum bpf_core_relo_kind` :7262 十二种(0–11)分三组讲解(字段类/类型类/枚举类);访问字符串 `0:1:2` 语义与 spec 三元组(类型 ID:字段索引:数组下标;`bpf_core_parse_spec` relo_core.c:263、`bpf_core_format_spec` :1156);04 五站实物(记录数>0、kind 落 0–11);教师视角(为什么记录在 .BTF.ext 而不是普通 ELF 重定位)/学生动手(03+04)/检查点;节末链接 03-core-read/04-relo-slicer。
- [ ] **Step 2: 篇幅自查+行号实测**。
- [ ] **Step 3: 提交** `docs(ch08): 三节(CO-RE 编译期:从宏到重定位记录)`

### Task 11: 正文——「四、两台重定位引擎」

- [ ] Task 11: 正文——「四、两台重定位引擎」

**tasks.md 条目**:矩阵②(libbpf relocation behavior)。

**Files:** Modify 同文件

- [ ] **Step 1: 写四节(6–8k)**——libbpf 用户态引擎:collect(`bpf_object__collect_prog_relos` libbpf.c:4236,解析 .BTF.ext :6763)→relocate(`bpf_object__relocate_core` :5783,调用点 :6526)→四步(候选查找/兼容检查/计算/补丁:`bpf_core_calc_relo_insn` relo_core.c:1280、`bpf_core_patch_insn` :1024;兼容判定 `__bpf_core_types_are_compat` :146、`__bpf_core_types_match` :1544);**内核态引擎**:uapi 载体(attr `core_relos` bpf.h:1465)→verifier `check_core_relo`(verifier.c:15718,接线 :15817)→`bpf_core_apply`(btf.c:8423,候选 `bpf_core_find_cands` :8336);谁在用内核态:**轻骨架 gen_loader**(libbpf.c:7296 记录→gen_loader.c:970 提交)——「普通骨架 libbpf 补丁,轻骨架内核补丁」;**双实现措辞(硬性)**:「逻辑同源、各自维护,接口对齐 uapi」,禁写拷贝关系;失败语义:候选找不到→-ESRCH/-EINVAL(预期口径);教师视角(为什么内核也要一台)/学生动手(05 五站)/检查点;节末链接 05-kernel-relo。
- [ ] **Step 2: 篇幅自查+行号实测+措辞 grep(「拷贝」零命中)**。
- [ ] **Step 3: 提交** `docs(ch08): 四节(两台重定位引擎)`

### Task 12: 正文——「五、实战五层」+ 可视化

- [ ] Task 12: 正文——「五、实战五层」+ 可视化

**tasks.md 条目**:矩阵③④⑤⑥。

**Files:** Modify 同文件

- [ ] **Step 1: 写五节(7–9k)**——五层:①环境(00-env:110 文件/CONFIG 三项/三档结论)②生成(02:vmlinux.h 结构导读——为什么能生成数万行声明)③剖析(01:自编对象 BTF 指认)④编译与记录(03 全链编译零告警+04 记录切片)⑤加载补丁[预期口径](skeleton open→load→attach 流程、字段修正演示**预期输出示例**、读取);**缺失-BTF 回退策略(矩阵⑤)**:/sys/kernel/btf/vmlinux 不存在三情形(旧内核/CONFIG 未开/容器掩蔽)→三选项逐个评估(升级内核=正解;BCC 运行期编译=旧路回退;固定偏移裸读=危险末选——回指 ch02:851);模块 BTF 与 attach_btf_obj_fd(uapi bpf.h:1461 一带);教师视角/学生动手(全链跑一遍)/检查点;节末链接全部资产。
- [ ] **Step 2: 可视化两图(1.5–2k,矩阵⑥)**——图① CO-RE 全链四段图(编译期 BTF[vmlinux.h/.BTF]→对象重定位记录[.BTF.ext 十二种 kind]→运行期目标 BTF[/sys/kernel/btf]→补丁后访问[imm/offset 改写];标注各段所在文件与本章资产对应);图②两引擎对照表图(libbpf vs kernel:入口函数/候选查找/补丁对象/使用者[普通骨架 vs 轻骨架]/失败语义);各 ≥150 字解读。
- [ ] **Step 3: 篇幅自查+提交** `docs(ch08): 五节(实战五层与缺失-BTF 回退)与可视化`

### Task 13: 排错/课后/总结/参考 + 交叉链接终检 + README 回填 + 篇幅双查

- [ ] Task 13: 排错/课后/总结/参考 + 交叉链接终检 + README 回填 + 篇幅双查

**tasks.md 条目**:矩阵⑦。

**Files:** Modify 正文;Modify `book/examples/chapter-08/README.md`

- [ ] **Step 1: 排错(2.5–3.5k)四类**——编译类(漏 -g→.BTF.ext 空;字段在本机 BTF 不存在→clang 报错=编译期暴露,回指 ch02:853;vmlinux.h 与自定义头类型冲突)、加载类(目标机无 BTF→libbpf 重定位失败[预期口径];CO-RE 候选不兼容→-ESRCH 语义;模块 BTF 缺失)、走读类(切片器锚点漂移/机检三类,回指 ch05)、环境类(/sys/kernel/btf 可读性差;CONFIG_DEBUG_INFO_BTF 未开→回退策略)。
- [ ] **Step 2: 课后+总结+参考**——课后(1–1.5k)链接 exercises/ 两项;总结 5 点(BTF 三重身份/19 种 kind 与 ID 编址/.BTF.ext 十二种 kind/两台引擎/缺失-BTF 回退)+展望第 9 章动手篇(回收 07:36);参考(源码树 btf.c/btf.h uapi/relo_core.c/bpf_core_read.h/gen_loader.c;`Documentation/bpf/btf.rst`、`clang-notes.rst`、`llvm_reloc.rst`——文件名 ls 实证;uapi bpf.h;回指 ch02 1.6/ch07 三处/ch09 构建链)。
- [ ] **Step 3: 终检(硬性)**——C8(`grep -c "](../examples/chapter-08/" ≥ 8`、`grep -c "](../appendix/" ≥ 1`)+ 机检终跑全绿 + README 回填(机检实测汇总/落地状态列)+ **篇幅双查**(章级 `wc -m` 40000–55000;分节预算逐节核对)+ 保守措辞 grep(「已验证通过」零命中;「预期输出示例/运行级待验证/方法论级」三标注在场;「拷贝」零命中)。
- [ ] **Step 4: 提交** `docs(ch08): 排错课后总结参考收尾与终检回填`

### Task 14: 两项练习 + 主审专项(CO-RE 边界/非绝对可移植)+ tasks.md 勾选(收口)

- [ ] Task 14: 两项练习 + 主审专项(CO-RE 边界/非绝对可移植)+ tasks.md 勾选(收口)

**tasks.md 条目**:矩阵⑧+八条统一勾选。

**Files:** Create `book/examples/chapter-08/exercises/01-relo-kind-detect/{README.md,relo-demo.bpf.c,Makefile,check-relo-kind.sh}`、`exercises/02-core-compat/{README.md,compat.bpf.c,Makefile,check-compat.sh}`(脚本可执行;构建产物不入库);Modify `openspec/changes/generate-ebpf-book/tasks.md`(勾选 8 条)

- [ ] **Step 1: 练习①落地+实跑(硬性)**(C4;relo-demo.bpf.c ≥3 种 kind;make 产 demo.bpf.o;check-relo-kind.sh 正路径全 PASS、断言指定 kind 值与记录数 ≥3;负路径 exit 1)。
- [ ] **Step 2: 练习②落地+实跑(硬性)**(C4;compat.bpf.c 完整可编译;check-compat.sh 编译级+机检锚点全绿)。
- [ ] **Step 3: 主审专项(矩阵⑧)**——CO-RE 边界逐条(位域/数组/指针链/union/跨类型 ID)+「可移植≠绝对」措辞(版本窗口、字段存在≠语义一致)+ relo kind 名称对照 uapi(12 个逐一,`BPF_CORE_FIELD_BYTE_OFFSET` :7263 起)+行号抽检 ≥15 处+验证双清单(编译级/预期)+术语一致性;问题回修。
- [ ] **Step 4: 提交练习** `feat(ch08): 两项练习参考实现(机检全绿)`;**勾选提交** `docs(ch08): 勾选 Chapter 8 矩阵任务`(tasks.md 231–240 行八条 `- [ ]`→`- [x]`;确认 `.comet/**` 未入库)

---

## 自审记录

1. **Spec 覆盖**:矩阵 8 条 → ①T8/T9/T10(正文五段:BTF 目标/DWARF→BTF/kind/字段偏移/CO-RE 重定位)②T5/T6/T7/T10/T11(内核 BTF 文档+libbpf 重定位行为)③T3/T4/T10/T12(vmlinux.h 读内核字段集成示例)④T1–T7(chapter-08 独立示例)⑤T1/T3/T8/T12(CONFIG/pahole/bpftool btf dump/缺失-BTF 回退)⑥T12 可视化(编译期 BTF→记录→目标 BTF→补丁)⑦T13 ⑧T14。无缺口。
2. **占位符扫描**:全部锚点在走读树实测(btf.c :5720/:5789/:8094/:8336/:8423;relo_core.c :146/:263/:1024/:1156/:1280/:1544;libbpf.c :4236/:5783/:6526/:6763/:7296;verifier.c :15718/:15778/:15817;gen_loader.c :970;uapi bpf.h :1461/:1463-1466/:7262/:7326;uapi btf.h :60-79;bpf_core_read.h 七宏;btf_type_by_id :488);文档三件 ls 实证;示例规格给定;无 TBD。
3. **类型一致性**:C1 位置(树根)与 C2 数据源一致;C3 与 T5/T6/C4 练习①一致(04 输出供 01-relo-kind-detect 消费);C4 练习②宏锚点与 C1 组③一致;C6 与 T1–T4 一致;C7 文件名与范围边界一致;汇总行口径 C2/T7/T13 一致;验证口径三档 C2(机检)/C6(编译级)/T8(方法论级)/T12(预期档)分布一致。

# 第 8 章独立示例资产

本目录是《第 8 章 BTF 与 CO-RE 原理》的配套资产,资产设计与总览见章设计文档 [§2 示例资产设计](../../../docs/superpowers/specs/2026-11-02-chapter08-teaching-design.md)。本章是「编译章」:一切验证围绕 BTF(BPF Type Format,BPF 类型格式)与编译产物展开——读 `/sys/kernel/btf`、生成 vmlinux.h、编译带 CO-RE(Compile Once – Run Everywhere,一次编译处处运行)重定位记录的 `.bpf.o`、切片重定位记录;除 03 的加载运行为预期档外,全部资产在本机**无 root** 即可复现。

## 演示目标

把「CO-RE 怎么让一份 .bpf.o 跑遍不同内核」拆成五件可操作的事:

- **环境自检**(`00-env/`):五段式盘点——走读树与四个走读文件、工具链三件带真实探针、BTF 家族、CONFIG 三项、pahole 缺席声明,末尾按本章三档口径给结论;
- **kind 剖析**(`01-btf-kinds/`):刻意覆盖多 kind 的 BPF 源码,用 `bpftool btf dump file` 逐一指认 kind 与类型 ID 编址;
- **vmlinux.h 生成**(`02-vmlinux-h/`):`bpftool btf dump format c` 把内核 BTF 变成 3.5MB 的头文件,并回收 ch07/ch09 构建链里同款步骤的机制解释;
- **CO-RE 编译与切片**(`03-core-read/`+`04-relo-slicer/`):第一个 `BPF_CORE_READ` 程序(编译级验证)+ 纯标准库 Python 切片器解剖 `.BTF.ext` 里的重定位记录;
- **内核引擎对照**(`05-kernel-relo/`):bash 切片器五站走读内核态重定位引擎 `bpf_core_apply`,与 03/04 的 libbpf 用户态引擎两两对照。

## 目录结构

| 目录 | 类别 | 内容 | 验证档 | 落地状态 |
|---|---|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:五段——①走读树版本+四走读文件(btf.c/relo_core.c/bpf_core_read.h/btf.h uapi)②工具链三件带探针(bpftool btf dump 取 vmlinux 第 1 行、clang 空文件 `-g -target bpf` 编译、python3)③BTF 家族(/sys/kernel/btf 文件计数、vmlinux 大小与权限、模块 BTF 抽样 1 个)④CONFIG 三项(DEBUG_INFO_BTF / DEBUG_INFO_BTF_MODULES / MODULE_ALLOW_BTF_MISMATCH)⑤pahole 缺席声明;无树 exit 2,末尾三档结论 | 编译级(只读无 root 自检) | 已落地(Task 1):本机实跑 exit 0,输出与设计基线一致(110 文件、CONFIG 两 y 一未设、pahole 方法论档) |
| `01-btf-kinds/` | 编译级剖析 | `kinds.bpf.c`(刻意覆盖 struct/union/enum/typedef/const/ptr/array/func/func_proto/全局变量 VAR+DATASEC/btf_decl_tag)+`Makefile`(仅编译不链接)+`README` 覆盖表与类型 ID 编址观察;剩余 kind(FWD/FLOAT/ENUM64/TYPE_TAG)从 vmlinux BTF 抽样指认 | 编译级已验证(clang `-g -target bpf` + `bpftool btf dump file`) | Task 2 落地 |
| `02-vmlinux-h/` | 编译级生成 | `gen-vmlinux-h.sh`(`bpftool btf dump file /sys/kernel/btf/vmlinux format c` + 规模统计)+`README`(生成方法、3.5MB 规模统计「本机实况」、头文件结构导读、回收 ch07/ch09 构建链);产物不入库 | 编译级已验证(bpftool btf dump format c) | Task 3 落地 |
| `03-core-read/` | 运行型示例 | 四件套:`core.bpf.c`(tracepoint sched/sched_process_exec,`bpf_get_current_task_btf()` + `BPF_CORE_READ` 读 pid/comm,含 `bpf_core_field_exists` 兼容分支)+`loader.c`(libbpf skeleton 全链)+`Makefile`(ch09 bootstrap 模式)+`README`;编译级全链零告警 | 编译级已验证;加载运行为预期输出示例(运行级待验证,需 root) | Task 4 落地 |
| `04-relo-slicer/` | 切片器(python) | `slice-relos.py` 五站:①ELF 段布局(.BTF/.BTF.ext/.rel.BTF.ext 尺寸)②btf_ext 头(magic/长度/偏移)③core_relo 记录解析(insn_off/type_id/access_str_off/kind)④kind 对照 uapi 枚举 ⑤access string 解码与源码字段对照;仅标准库 | 编译级已验证(实跑切片,记录数>0 断言) | Task 5 落地 |
| `05-kernel-relo/` | 切片器(bash) | `trace-kernel-relo.sh` 五站:①内核引擎入口 `bpf_core_apply` ②候选查找 `bpf_core_find_cands` 与兼容 `bpf_core_types_are_compat` ③verifier 接线 `check_core_relo` ④UAPI 载体(attr core_relos 三字段)⑤轻骨架提交 `gen_loader.c`——与 libbpf 引擎对照 | 编译级已验证(只读走读树切片) | Task 6 落地 |
| `scripts/` | 符号机检 | `verify-citations.sh`:机检树根 citations.txt,逐行验文件在场 + `grep -F` 符号命中,输出 TOTAL/PASS/FAIL 汇总 | 编译级(只读机检) | Task 7 落地 |
| `exercises/` | 课后扩展 | `01-relo-kind-detect/`(对预置 demo.bpf.o 用 04 切片器判读每条重定位记录的 kind 语义)、`02-core-compat/`(用 `bpf_core_field_exists`/`bpf_core_type_exists` 构造跨版本兼容分支),防剧透结构(任务与提示在前、参考思路独立成节后置) | 编译级(机检/编译断言) | Task 14 落地 |
| `citations.txt` | 锚点清单 | 树根,格式「文件\|符号\|说明」,五组约 40 行:①BTF 基础与解析 ②kind 体系 ③CO-RE 编译期(宏族七条)④libbpf 引擎 ⑤内核引擎+UAPI 载体 | 编译级(机检数据源) | Task 7 落地 |

## 学习路径

建议顺序 00→01→02→03→04→05:

1. `bash 00-env/check-env.sh` 自检:确认「编译级可跑」PASS(本机无 root 即可);加载级与 pahole 档位看末尾三档结论;
2. `01-btf-kinds/`:先 `make` 出 kinds.bpf.o,再按 README 用 `bpftool btf dump file` 逐一指认 kind(正文二节的实证面);
3. `02-vmlinux-h/`:生成 vmlinux.h 并按 README 导读其结构(正文一/三节的实证面;产物不入库,重复生成无副作用);
4. `03-core-read/`:第一个 CO-RE 程序——`make` 走通 clang→skeleton→gcc 全链(编译级);加载运行为预期档(需 root);
5. `04-relo-slicer/`:用纯标准库 Python 切片器解剖 03 产出的 core.bpf.o,亲眼看 .BTF.ext 里的重定位记录(正文三节的实证面);
6. `05-kernel-relo/`:bash 切片器走读内核态引擎,与 03/04 的 libbpf 用户态引擎两两对照(正文四节的实证面);
7. `scripts/` + `citations.txt`:符号机检全绿后对照正文走读;`exercises/` 先自行完成任务,再看各练习 README 后部的参考思路(防剧透)。

## 验证状态约定(本章三档口径)

- **编译级已验证**:本机无 root 可复现,全部以实跑输出为证——`bpftool btf dump` / `format c` 生成 vmlinux.h、clang `-g -target bpf` 产出 `.BTF`/`.BTF.ext`/`.rel.BTF.ext`、skeleton 生成、gcc 链接、切片器与机检实跑、只读 CONFIG/BTF/ELF 段读取;
- **预期输出示例·运行级待验证**:skeleton 加载、attach、CO-RE 字段修正后实际读取——本机无 root 且 `kernel.unprivileged_bpf_disabled=2`,一律标「预期输出示例 / 运行级待验证」,不是本地运行结果(03 的读取输出属此档);
- **方法论级(pahole)**:DWARF→BTF 转换只给流程与判读法,标「本机未装 pahole,转换级验证需自备工具链」——本章其余资产只消费现成 BTF,不受影响。

## 机检汇总

(占位:由 Task 13 回填——机检脚本 `scripts/verify-citations.sh` 落地后,此处回填 TOTAL/PASS/FAIL 终跑实测、负路径门禁行为与五组锚点分布。)

## 走读树版本声明

本目录全部机检与切片脚本默认针对 `/work/linux/linux-6.6.78` 实跑;正文所有内核路径/行号引用锚定该树,行号一律标注「(v6.6.78)」并注明随版本漂移。换用其他版本树时以 `WALK_TREE=<路径>` 覆盖默认值——机检按符号不按行号,不受行号漂移影响。涉及本机实测数值(BTF 文件数、类型 ID、结构尺寸等)均标「本机实况,其他内核数值不同」。

## 安全与只读声明

本章示例只读 BTF 与编译:**00/01/02/04/05 全程不加载任何 BPF 程序**(只读源码树、`/sys`、`/proc`、`/boot` 与编译产物);03 的加载运行为预期档、需 root,且仅使用 tracepoint 挂点,请在实验环境进行。构建产物(vmlinux.h、skeleton 头、.bpf.o)由 `.gitignore` 全局规则覆盖,不入库。

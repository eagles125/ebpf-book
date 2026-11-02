# Brainstorm Summary

- Change: generate-ebpf-book
- Date: 2026-11-02
- 主题:第 8 章《BTF 与 CO-RE 原理》教学设计(build 阶段内章级循环,同第 2–7 章模式;ch07 park 检查点见 git 历史 de78ef1)

## 已确认事实(本机实证)

- 走读树:`/work/linux/linux-6.6.78`(已确认决策沿用)。BTF 源文件:kernel/bpf/btf.c 8601 行;include/uapi/linux/btf.h 200 行;include/linux/btf.h 574 行;Documentation/bpf/btf.rst 1089 行(+ clang-notes.rst、llvm_reloc.rst)
- UAPI 锚点(btf.h):BTF_KIND_* 0–19 全表 :60-79;bpf.h:prog attr 的 `core_relo_cnt` :1463、`fd_array` :1464、`core_relos` :1465、`core_relo_rec_size` :1466;`enum bpf_core_relo_kind` :7262(12 种 0–11);`struct bpf_core_relo` :7326
- 内核侧重定位引擎(btf.c):`btf_member_is_reg_int` :864、`btf_parse_vmlinux` :5720、`bpf_core_types_are_compat` :8094、`bpf_core_find_cands` :8336、**`bpf_core_apply` :8423**;verifier.c:`check_core_relo` :15718、调用 `bpf_core_apply` :15778、bpf_check 接线 :15817——**内核态 CO-RE 重定位在 6.6 真实存在,入口是 BPF_PROG_LOAD 的 attr.core_relos**
- libbpf 侧重定位(relo_core.c 1687 行):`__bpf_core_types_are_compat` :146、`bpf_core_parse_spec` :263、`bpf_core_patch_insn` :1024、`bpf_core_format_spec` :1156、`bpf_core_calc_relo_insn` :1280、`__bpf_core_types_match` :1544;libbpf.c:`bpf_object__collect_prog_relos` :4236(解析 .BTF.ext,调用点 :6763)、**`bpf_object__relocate_core` :5783(用户态补丁入口,调用点 :6526)**、`record_relo_core` :5705;gen_loader.c:core_relo 记账 :856-898、`attr.core_relo_cnt = gen->core_relo_cnt` :970——**普通骨架=libbpf 用户态补丁,轻骨架=内核态补丁,两引擎共享 relo_core.c 逻辑(教学对比主线)**
- CO-RE 宏(bpf_core_read.h 485 行):`BPF_CORE_READ_BITFIELD_PROBED` :70、`BPF_CORE_READ_BITFIELD` :88、`bpf_core_field_exists` :132、`bpf_core_type_exists` :185、`bpf_core_enum_value_exists` :216、`bpf_core_read` :246、`___core_read` 展开家族 :336-343、`BPF_CORE_READ_INTO` :351;bpf_helpers.h 402 行
- 运行内核 BTF 实况:`CONFIG_DEBUG_INFO_BTF=y`、`CONFIG_DEBUG_INFO_BTF_MODULES=y`、`MODULE_ALLOW_BTF_MISMATCH` 未设(/boot/config-7.0.0-38-generic);`/sys/kernel/btf/` **110 个文件(vmlinux 7119843 字节 mode 444 全局可读 + 109 个模块 BTF)**;`bpftool btf dump file /sys/kernel/btf/vmlinux` 非特权实跑 OK(401978 行;task_struct id=156855 size=9920 vlen=274)
- **编译级真物双锚点(本章独有)**:①`bpftool btf dump file /sys/kernel/btf/vmlinux format c` 生成 vmlinux.h **3500327 字节**——回收 ch07/ch09 构建链「vmlinux.h 从哪来」;②clang 18 `-g -target bpf` 编译 CO-RE 探针程序产出 `.BTF`(0x17a)+`.BTF.ext`(0x9c)+`.rel.BTF.ext`(0x60),`bpftool btf dump file core.bpf.o` 可读本地对象 BTF(STRUCT 'my_type' size=24 vlen=3 带位偏移)——**重定位记录=编译期可实证**
- **pahole 不在 PATH**:DWARF→BTF 转换(pahole -J)本机不可复现→方法论级+缺失-BTF 回退策略同口径;python3 3.12.3 在场→**.BTF.ext 重定位记录切片器(python 解析 btf_ext 头+relo 记录,格式文档在 btf.rst)可行**
- 工具链:clang 18.1.3、llvm-objdump 18、bpftool 7.7.0、libbpf 1.3.0;无 root、unprivileged_bpf_disabled=2、perf_event_paranoid=4(沿用)
- 前向引用回收清单:ch02:95(CO-RE 注释前瞻)、ch02:851/853(排错表指第 8 章:固定偏移错位/字段存在性兼容分支);ch07:36(第 8 章后进入动手篇)、ch07:255(BTF 字段记录=运行期登记簿,CO-RE 正题留第 8 章)、ch07:603(vmlinux.h 从哪来/类型 ID 编址/加载期重定位/bpftool btf dump 入口)、ch07:611(交叉链接);ch09/ch07 示例构建链已用 vmlinux.h 与 bpftool gen skeleton
- 矩阵任务(tasks.md Chapter 8,8 条):①BTF 目标/DWARF→BTF/类型 ID/字段偏移/CO-RE 正文;②内核 BTF 文档+libbpf 重定位行为分析;③vmlinux.h 读内核结构字段的集成 CO-RE 示例;④chapter-08 独立示例资产;⑤CONFIG_DEBUG_INFO_BTF/pahole/bpftool btf dump/缺失-BTF 回退;⑥可视化(编译期 BTF→对象重定位记录→运行期目标 BTF→补丁后访问);⑦排错/总结/参考;⑧主审=CO-RE 边界条件+非绝对可移植措辞

## 已确认设计方案(2026-11-02 用户结构化问答拍板)

1. **章定位=机制章+CO-RE 全链实证**(同 ch06/07 模式);「可编译」等价物=机检(citations.txt 符号制)+ 编译级真物(vmlinux.h 生成、CO-RE 程序的 .BTF/.BTF.ext 产物、重定位记录切片);加载期补丁与 skeleton 对比=预期口径(运行级待验证)
2. **资产方案 A(用户选定)**:00-env(BTF 家族自检:110 文件/CONFIG 三项/pahole 缺席声明/clang -g 探针)/01-btf-kinds(自编 .bpf.o 的 BTF 剖析+kind 抽样+类型 ID 编址)/02-vmlinux-h(生成+规模统计+与 ch09 构建链互指)/03-core-read(BPF_CORE_READ 读 task_struct 字段示例,libbpf 全链编译级)/04-relo-slicer(python .BTF.ext 切片器,五站:头部/记录/kind 对照/access string/与指令对齐)/05-kernel-relo(内核引擎切片器五站+轻骨架对比)/scripts/verify-citations.sh/citations.txt/exercises×2
3. **篇幅 40–55k,正文五段(用户选定扩展口径)**:一(BTF 是什么:目标/DWARF→BTF 方法论/vmlinux+模块 BTF/CONFIG/pahole)、二(BTF kind 体系与类型 ID 编址,独立段)、三(CO-RE 编译期:intrinsics/宏族/BTF.ext 记录/12 种 kind/补丁算法 spec)、四(两台重定位引擎:libbpf 用户态 vs 内核态 bpf_core_apply+轻骨架)、五(实战五层:环境→vmlinux.h→CO-RE 程序→重定位记录→加载补丁[预期])

## 测试策略(确认)

- 机检+citations(同 ch05/06/07 C1/C2 接口);01/02/03 编译级实跑断言(生成物在场+bpftool 可读+尺寸统计);04 切片器 python 实跑断言(记录数>0、kind 合法);05 切片器同 ch05/06/07 C3 接口(走读树实跑);加载/attach/字段读取=预期输出示例(运行级待验证);主审⑧专项=逐条 CO-RE 边界(位域/数组/指针链/跨类型)与「可移植≠绝对」措辞审查

## Spec Patch

- 候选:无(矩阵 8 条边界清晰,同 ch04–ch07 先例)

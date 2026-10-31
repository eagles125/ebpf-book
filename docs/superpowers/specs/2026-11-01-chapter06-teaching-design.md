---
comet_change: generate-ebpf-book
role: technical-design
canonical_spec: openspec
---

# 第 6 章完整教学章节技术设计(JIT 编译与性能优化,机制章 + JIT 走读实战化)

> **确认状态**:结构化提问未获答复,按 ch03/ch05 会话先例采用推荐项(走读树=6.6.78 沿用、方案 A 走读+反汇编锚点、篇幅 35–50k、练习 2 项),**待用户复核,可推翻**;推翻任一项只需在 build 阶段前提出,按 brainstorming 流程迭代。

## 1. 背景与目标

### 1.1 矩阵任务(openspec tasks.md「Chapter 6: JIT Compilation and Performance Optimization」,7 条)

1. Write full chapter prose explaining interpreter mode, JIT mode, architecture backends, and why JIT matters.
2. Analyze x86_64 and arm64 JIT paths at a high level, including instruction lowering, prologue/epilogue, helper calls, and constant blinding where applicable.
3. Include commands for inspecting translated and JITed output with `bpftool prog dump xlated` and `bpftool prog dump jited`.
4. Include a performance comparison methodology for JIT enabled versus disabled, with clear safety notes.
5. Add text visualization comparing interpreter and JIT execution paths.
6. Add troubleshooting, summary, and references.
7. Main-agent review to distinguish verified results from expected methodology.

### 1.2 前章预告回收清单(正文必须兑现)

| 出处 | 预告内容 | 兑现位置 |
|---|---|---|
| ch02:131 | 「JIT 编译为本机指令,架构相关(第 6 章,源码地图见第 5 章)」 | 一节开头+二节 |
| ch04:805 | 第 6 章「接手生命周期第三步——验证通过的字节码变成本机指令」 | 导读 |
| ch05 二节 2.4 JIT 站 | 「深挖留第 6 章」 | 二节开头显式回收 |
| ch02 2.1 | 64 位 RISC 模型(寄存器与调用约定) | 二节 lowering 的前置回指 |

### 1.3 定位与本章独有的验证口径

机制章 + JIT 走读实战化。与第 5 章同构(机检符号制+切片器),但多一个**本章独有的编译级真实验证实物**:BPF 字节码反汇编(`clang -target bpf` → `.bpf.o` → `llvm-objdump -d`,无 root)。两个必须诚实交代的本机口径:

- 运行内核 `CONFIG_BPF_JIT_ALWAYS_ON=y`:**解释器在编译期被移除**,`bpf_jit_enable` 开关实际不可关——「JIT on/off 性能对比」只能给方法论与预期输出(矩阵④「clear safety notes」正落于此);解释器走读锚定 6.6.78 源码,标注「本机内核不含此路径」。
- `bpf_jit_harden/kallsyms/limit` root-only 可读(`bpf_jit_enable` 全局可读=1)、`perf_event_paranoid=4`(非特权 perf 全禁)——sysctl 权限差与基准方法论的分级素材。

## 2. 示例资产设计(方案 A:走读+反汇编锚点)

`book/examples/chapter-06/`:

| 目录 | 主题 | 说明 |
|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:走读树版本;`net.core.bpf_jit_*` 五个 sysctl 的**可读性分级诊断**(enable=1 可读/harden·kallsyms·limit root-only——把权限差打印成表);llvm-objdump 与 clang bpf 目标支持;`/boot/config-*` 的 CONFIG_BPF_JIT* 三项;perf_event_paranoid;BTF |
| `01-bpf-asm/` | 反汇编锚点 | 最小 XDP 程序(约 20 条指令:ctx 加载/边界检查/以太类型判断/返回码)+`Makefile`(clang→.bpf.o)+`disasm.sh`(llvm-objdump -d)+README(**逐行 .c↔bytecode 对照表**,编译级实跑);不用 vmlinux.h(用 uapi 头,反汇编可读性优先) |
| `02-jit-x86/` | x86 切片器 | `trace-jit-x86.sh` 六站:①select_runtime→int_jit_compile 调用链(core.c:2357/:2384,回指 ch05)②do_jit 入口与两趟(:1075;extra_pass :2763/:2803)③emit_prologue(:343;BPF→x86 寄存器映射/调用约定/栈帧)④指令 lowering 逐例(ALU/JMP/LDX/STX 分派段)⑤helper 调用与 tail-call(:520/:594;bpf_jit_get_func_addr 两趟原因)⑥常量致盲与映像分配(blind_constants core.c:1428;bpf_prog_pack :850/:904) |
| `03-jit-arm64/` | arm64 对照切片器 | `trace-jit-arm64.sh` 四站:①入口 bpf_int_jit_compile(:1518)②build_body 指令分派③bpf_jit.h 寄存器映射④与 x86 差异(flush_icache 等);每站带「与 x86 对照」职责注 |
| `scripts/` | 符号机检 | `verify-citations.sh`(接口同 ch05 C2;citations.txt 定位:`book/examples/chapter-06/citations.txt`(章级树根,01-bpf-asm 顶替了 ch05 的 01-source-map 地位),README 注明与 ch05 接口一致 |
| `exercises/01-asm-readback/` | 练习① | 读 BPF 汇编:给一段 .bpf.c,产出逐行 source↔bytecode 对照表;参考实现=对照表+`check-answer.sh`(对 01-bpf-asm 的 .bpf.o 反汇编断言特征指令序列) |
| `exercises/02-extend-jit/` | 练习② | 给 trace-jit-x86.sh 增加致盲细节站(bpf_jit_blind_insn core.c:1250 的改写示例);参考实现=完整脚本+check 脚本(接口同 ch05 C4) |

资产设计规则:同 ch05(中文注释四类要点、只读树、防剧透 README、验证状态三档);01-bpf-asm 是唯一「构建型」资产(只编译不加载,无挂载风险,头部声明)。

## 3. 章节正文设计

### 3.1 结构(完整模板十一小节)与四段主轴

```markdown
# 第 6 章 JIT 编译与性能优化

## 学习目标
## 本章导读
## 一、三种执行形态与那排开关          ← 矩阵①;ch02:131 兑现
## 二、x86_64 JIT:从字节码到机器码的六站 ← 矩阵②主重;ch05「深挖」回收
## 三、arm64:另一套后端的同构与差异     ← 矩阵②平行
## 四、实战:亲手摸到字节码与机器码(五层)  ← 矩阵③④
## 可视化:解释器与 JIT 的执行路径对比     ← 矩阵⑤
## 常见问题与排错
## 课后扩展任务
## 总结
## 本章参考来源
```

各段主轴末尾「**独立示例**」链接(一→00-env;二→02-jit-x86;三→03-jit-arm64;四→01-bpf-asm 与 scripts)。

### 3.2 各节要点

**一、三种执行形态与那排开关**(7–9k):解释器(___bpf_prog_run :1679 调度循环、栈长分发表 PROG_NAME_LIST :2220-2225 的尾调用技巧)、JIT(select_runtime→bpf_int_jit_compile,回指 ch05 二节)、offload 一句(网卡固件执行,驱动侧 bpf_offload);为什么 JIT 重要(每条指令一次解码 vs 一次编译;指令缓存;固定开销前移);`bpf_jit_enable/kallsyms/harden/limit` 四个 sysctl 的内核出处(core.c:562-565)与默认值来源(CONFIG_BPF_JIT_DEFAULT_ON);**本机实况:ALWAYS_ON=y 的含义**(解释器不在内核映像里、开关只剩安全语义)——教师视角素材;学生动手=读五个 sysctl 可读性分级表。

**二、x86_64 JIT 六站**(9–11k):每站「签名(标注 v6.6.78 行号)→职责→关键代码段(≤25 行,与树逐字一致)→失败出口」:
1. 选通与入口:`bpf_prog_select_runtime`(core.c:2357)→`bpf_int_jit_compile`(:2384 调用,x86 侧 arch/x86/net/bpf_jit_comp.c:2763 起);回指 ch05。
2. `do_jit`(:1075)与两趟编译:`extra_pass`(:2763/:2803)为什么存在(kallsyms 地址在第一趟未知,call 偏移需要重放)。
3. `emit_prologue`(:343):BPF→x86 寄存器映射(r0-r10 与 x86 调用约定)、栈帧布局(aux->stack_depth)、tail call 计数区;回指 ch02 2.1 RISC 模型。
4. 指令 lowering 逐例:switch 分派段挑 ALU64/JMP/LDX/STX 四例(每例 ≤15 行,配 BPF 指令↔x86 指令对照)。
5. helper 调用与 tail call:`bpf_jit_get_func_addr` 与两趟的关系;`emit_bpf_tail_call_indirect`(:520)/`direct`(:594);回指 ch04 程序类型 helper 白名单。
6. 常量致盲与映像分配:通用层 `bpf_jit_blind_constants`(core.c:1428)/`bpf_jit_blind_insn`(:1250)(为什么:跳板攻击面;harden 与特权的关系);`bpf_prog_pack`(:850/:904)(多程序共享 RWX→RX 映射)。

**三、arm64 高层对照**(4–6k):`bpf_int_jit_compile`(arch/arm64/net/bpf_jit_comp.c:1518)结构同构;`bpf_jit.h` 寄存器映射(BPF r0-r10→arm64 x0-x10 等);差异点:icache 显式 flush、立即数装载策略(MOVZ/MOVK 序列 vs x86 单 imm32)、LSE atomics 可选;**不做逐指令深挖**(矩阵②措辞「at a high level」);一节收尾给「第三套后端怎么读」的方法迁移。

**四、实战五层**(6–8k):
1. 依赖环境:走读树沿用声明;工具(clang bpf 目标、llvm-objdump、bpftool);sysctl 可读性分级表(实跑)。
2. 「编译」环境=机检+反汇编:verify-citations.sh 原理(回指 ch05 四节,一段带过不重复);**01-bpf-asm 全流程实跑**(clang→obj→llvm-objdump -d,逐行对照表节选)——本章编译级真实物。
3. 部署环境=定位:JIT 源文件地图(arch/*/net/ 布局、32 位兼容后端一句话);定位命令目录(同 ch05 命令表,一句回指)。
4. 运行环境:两切片器实跑。
5. 运行效果:`bpftool prog dump xlated`/`jited` 命令讲解(**预期输出示例/运行级待验证**——需加载,本机不可;`file` 输出与 `opcodes` 参数);性能对比方法论(JIT on/off:ALWAYS_ON 内核不可关——方法留给可关内核;`bpf jail`?否——用 perf stat 循环/bpftool prog run 计时;**安全注**:生产禁关 JIT、perf_event_paranoid=4 本机限制、预期输出全部标注)。

**可视化**(1.5–2k):①解释器 vs JIT 执行路径文本图(同一程序两条路径:dispatch 循环逐条 vs 一次编译后原生的执行,标注前移成本与每条开销)②x86 JIT 六站流水图;各配 ≥150 字解读段。

**排错**(2.5–3.5k):走读类(致盲后指令对不上原程序——预期,回指二节⑥;两趟的反汇编差异;jited dump 权限)、sysctl 类(harden/kallsyms 读不了=root-only 教学点回指)、方法论类(ALWAYS_ON 无法关 JIT 的对照实验设计迁移、perf 无权限的替代口径)、机检三类(回指 ch05 排错,一句带过)。

### 3.3 篇幅预算(总 35–50k,wc -m)

目标+导读 2–2.5k、一 7–9k、二 9–11k、三 4–6k、四 6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后+总结+参考 2.5–3.5k。(分节预算服从章级硬区间,同 ch05 裁决先例。)

### 3.4 课后练习设计(2 项,走读口径)

- **练习① asm-readback**:对 01-bpf-asm 的程序产出逐行 source↔bytecode 对照(训练:指令集编码落地,ch02 2.1 的实战回指);参考实现=对照表(markdown)+`check-answer.sh`(反汇编断言:特征指令序列,如 `r0 = 2`/`exit` 前的边界检查模式)。
- **练习② extend-jit**:给 trace-jit-x86.sh 增加「致盲细节」站(`bpf_jit_blind_insn` core.c:1250 的改写示例段+一份 before/after 指令对照说明);参考实现=完整脚本+check 脚本(接口同 ch05 C4)。

## 4. 验证策略

- 符号机检(接口同 ch05 C1/C2):citations.txt 约 45 锚点,x86/arm64/解释器/致盲/sysctl 出处全覆盖;按符号不按行号。
- 01-bpf-asm:clang 编译 + llvm-objdump 反汇编实跑(编译级真实);README 对照表节选与实跑一致。
- 切片器与练习脚本:bash -n + 实跑断言(同 ch05 C3/C4)。
- **主审⑦专项(矩阵独有)**:逐条区分「已验证(机检/编译/反汇编/只读 sysctl/CONFIG)」vs「预期方法论(xlated/jited dump/性能对比/harden 读取)」——审查 prompt 明文列出两条清单逐条归类。
- 运行级输出一律「预期输出示例/运行级待验证」;全文零「已验证通过」式运行级表述。

## 5. 技术风险与边界

| 风险 | 消化方式 |
|---|---|
| 6.6.78 的 JIT 代码与运行内核 7.0 有差异 | 同 ch05 口径:行号标注树版本、差异只作「版本演进」教学点;运行内核对接只用 CONFIG/只读 sysctl 实证 |
| ALWAYS_ON 使解释器/开关不可实测 | 诚实口径:解释器节标注「本机内核不含此路径」;对比方法论=可迁移实验设计+预期输出;矩阵④安全注正落于此 |
| lowering 逐例贴码超篇幅 | 每例 ≤15 行、只挑 4 例;完整分派给切片器切片 |
| arm64 无本机交叉验证 | 纯源码走读(6.6.78 树内一致即可);不写任何 arm64 实机行为断言 |
| x32 后端(bpf_jit_comp32.c)诱惑 | 一句话交代存在与适用场景,不展开 |

## 6. tasks.md 回写清单

矩阵 7 条由末任务统一勾选;「Batch 3」总项不动(第 7/8 章未完成)。

## 7. 测试策略汇总

1. 机检全绿 + 反汇编锚点编译级实跑 + 切片器/练习脚本 bash -n 与实跑断言;
2. 结构级:十一小节、五层、双视角(一/二/三节)、可视化两图带解读、排错/总结/参考、代码块与资产对应、citations 对应;
3. 保守措辞与三档验证标注 grep 复核;主审双清单(已验证/预期)逐条归类。

---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-11-01-chapter06-teaching-design.md
base-ref: ec35a98
---

# 第 6 章完整教学章节(build 阶段)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 6 章《JIT 编译与性能优化》从零写为 35–50k 字符(wc -m)的完整教学章节(机制章 + JIT 走读实战化),并落地 `book/examples/chapter-06/` 全部走读资产:00-env sysctl 分级自检、01-bpf-asm 反汇编锚点(本章独有的编译级真实物)、02-jit-x86 与 03-jit-arm64 两个切片器、scripts/verify-citations.sh 符号机检、2 项练习(含参考实现与机检答案)。

**架构:** 资产先行(同 ch05)——`book/examples/chapter-06/citations.txt` 是全章共享接口(机检脚本唯一数据源,定位在章级树根);01-bpf-asm 提供「源码→BPF 字节码」的编译级对照(clang→.bpf.o→llvm-objdump -d,无 root 真实可跑);两个切片器对走读树 `/work/linux/linux-6.6.78` 实跑提取 x86/arm64 JIT 路径各站;正文四段(三执行形态与开关→x86 六站→arm64 对照→实战五层)。本章唯一的构建型资产是 01-bpf-asm(只编译不加载)。

**技术栈:** bash(grep/sed/wc 核心工具)、C(单文件 BPF 锚点程序,uapi 头,无 vmlinux.h)、clang 18.1.3(-target bpf)+ llvm-objdump 18、Markdown(中文正文)。走读树:Linux 6.6.78。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md`「Chapter 6: JIT Compilation and Performance Optimization」7 条;第 7/8 章另立计划。
- delta spec 无需变更(无 Spec Patch),不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 不触碰 `book/chapters/01~05`、`09`、`17` 及对应 examples 目录。
- 只修改:`book/chapters/06-jit-compilation-and-performance.md`(新建)、`book/examples/chapter-06/**`(新建)、`openspec/changes/generate-ebpf-book/tasks.md`(末任务勾选)。

## 全局约束

1. **语言**:全部正文与脚本注释中文;术语遵循 `book/STYLE.md`(map=映射、helper=辅助函数、program type=程序类型);首现「英文(中文解释)」。
2. **走读树口径(硬性)**:内核路径/函数/宏引用锚定 `/work/linux/linux-6.6.78`;行号标注「(v6.6.78)」随版本漂移;机检按符号不按行号。运行内核(7.0.0-38)对接只用实证事实(CONFIG_BPF_JIT=y/ALWAYS_ON=y/DEFAULT_ON=y、`bpf_jit_enable=1` 可读、harden/kallsyms/limit root-only、perf_event_paranoid=4),差异保守措辞「随内核版本演进,以所用内核源码为准」。
3. **验证策略(硬性,矩阵⑦的主审重点)**:已验证=机检实跑、01-bpf-asm 编译+反汇编实跑、只读 sysctl 实跑、CONFIG 读取、切片器实跑;预期=bpftool prog dump xlated/jited(需加载)、JIT on/off 性能对比(ALWAYS_ON 不可关+harden 读取 root-only)、任何加载行为。全文零「已验证通过」式运行级表述;预期类一律「预期输出示例/运行级待验证」。
4. **篇幅**:35–50k(wc -m);分节预算:目标+导读 2–2.5k、一 7–9k、二 9–11k、三 4–6k、四 6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后+总结+参考 2.5–3.5k;分节服从章级(同 ch05 裁决先例)。
5. **叙事**:双视角落「一、二、三」三节(教师视角/学生动手/检查点 3 问);「四、实战」五层+四段节奏。各段主轴末「**独立示例**」链接。
6. **脚本注释**:bash 脚本中文注释四类要点(用途与输入/输出结构/依赖与降级/失败模式);头部「只读源码树,不构建、不加载」(01-bpf-asm 例外声明:「只编译不加载,无挂载风险」)。
7. **任务对应**:每个任务引用 tasks.md「Chapter 6」矩阵文本片段;Task 13 统一勾选。
8. **提交纪律**:每任务一次提交;`feat/chore/docs(ch06): ...`;只加当前任务 Files;`.comet/**` 不入提交。计划任务头下的任务级 checkbox 行(`- [ ] Task N: ...`)供 `comet state task-checkoff` 匹配,不得删除。

## 现状基线(编写计划时已在 6.6.78 树/本机实证)

| 事实 | 值 |
|---|---|
| HEAD | `ec35a98`(ch06 设计文档;工作区干净) |
| JIT 源文件 | `arch/x86/net/bpf_jit_comp.c` 3020 行、`arch/x86/net/bpf_jit_comp32.c`、`arch/arm64/net/bpf_jit_comp.c` 2303 行、`arch/arm64/net/bpf_jit.h` |
| core.c 锚点 | `bpf_jit_enable` 等四 sysctl 声明 :562-565;`bpf_jit_blind_insn` :1250;`bpf_jit_blind_constants` :1428;`bpf_jit_get_func_addr` :1210;`___bpf_prog_run` :1679;`PROG_NAME_LIST` 栈长分派 :2220-2225;`bpf_prog_pack` :850、`bpf_prog_pack_alloc` :904;`bpf_prog_select_runtime` :2357;`bpf_int_jit_compile` 调用点 :2384 |
| x86 锚点(comp.c) | `bpf_int_jit_compile` :2754;`emit_prologue` :343;`do_jit` :1075(`aux->stack_depth` :1096);`emit_bpf_tail_call_indirect` :520、`emit_bpf_tail_call_direct` :594;`extra_pass` :2763/:2803 |
| arm64 锚点 | `bpf_int_jit_compile` :1518;`build_body` :1442;寄存器映射表 `bpf2a64`(:47 起,`[BPF_REG_0] = A64_R(7)`);`emit_a64_mov_i64` :129;`bpf_jit.h` `A64_R` :13 |
| 本机 sysctl | `bpf_jit_enable`=1(可读);`bpf_jit_harden/kallsyms/limit` 权限不够(root-only);`perf_event_paranoid`=4 |
| 运行内核 CONFIG | CONFIG_BPF_JIT=y、CONFIG_BPF_JIT_ALWAYS_ON=y、CONFIG_BPF_JIT_DEFAULT_ON=y(/boot/config-7.0.0-38-generic) |
| 反汇编锚点(冒烟已过) | 01-bpf-asm 的 xdp_count 程序 14 条指令,clang 18.1.3 `-O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu` 编译通过;llvm-objdump -d 显示:ctx 加载(61)/边界检查(07+2d)/大端比较(0xdd86 立即数)/三返回常量(0/1/2)/exit(95) |
| 工具链 | clang 18.1.3、llvm-objdump(默认 18)、bpftool 7.7.0;无 root、unprivileged_bpf_disabled=2 |
| 可复用契约 | ch05 的 C1(citations 格式)/C2(verify-citations.sh)/C3(切片器)/C4(练习)/C7(标题固定)/C8(交叉链接)——本章同字母引用并给出替换与新增 |

## 共享接口契约(跨任务定义)

### C1. citations.txt(chapter-06 树根,Task 2 产出,Task 3 消费)

行格式 `<相对树根路径>|<符号>|<一句话中文说明>`,`#` 注释;机检=文件存在+`grep -F -q` 命中(同 ch05)。约 45 行,覆盖五组:①core.c 执行形态与开关(jit_enable 四行声明/___bpf_prog_run/PROG_NAME_LIST/select_runtime/int_jit_compile 调用点 `bpf_int_jit_compile(fp` 字面/blind_insn/blind_constants/get_func_addr/pack 两行)②x86(comp.c 的 bpf_int_jit_compile/emit_prologue/do_jit/两 tail_call/extra_pass)③arm64(comp.c 的 bpf_int_jit_compile/build_body/bpf2a64 映射行 `bpf2a64[BPF_REG_0]`/emit_a64_mov_i64;bpf_jit.h 的 `#define A64_R(x)`)④对象与辅助(struct bpf_binary_header filter.h/struct bpf_prog_aux 与 stack_depth include/linux/bpf.h/struct bpf_prog include/linux/bpf.h)⑤UAPI(include/uapi/linux/bpf.h 的 BPF_PROG_LOAD、enum xdp_action;linux/if_ether.h 的 ETH_P_IPV6)。Task 2 逐行实测修正留痕。

### C2. verify-citations.sh 接口(scripts/,Task 3)

同 ch05 C2 逐字,唯一差异:数据源为 `../citations.txt`(章级树根,非 01-source-map/)。汇总行 `TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78`;FAIL≠0 exit 1;头部「只读源码树,不构建、不加载」+失败三类原因;中文注释四类要点。

### C3. 切片器接口(02-jit-x86/trace-jit-x86.sh 六站、03-jit-arm64/trace-jit-arm64.sh 四站,Task 4/5)

同 ch05 C3:`bash trace-*.sh [WALK_TREE]`(默认 6.6.78),只读树,每站=`## 站名`+grep -n 定位(标注 v6.6.78)+sed 区间(≤25 行/段)+一行中文职责注;定位失败 `STATION n: NOT FOUND`+exit 1;汇总 `TREE_VERSION=v6.6.78 STATIONS=<n>`。x86 六站(设计 §3.2 二节六站逐一对应);arm64 四站(入口/build_body 分派/bpf2a64 映射/与 x86 差异——第四站取 bpf_jit.h 头部与 flush_icache 调用点,职责注必须写「与 x86 对照」)。

### C4. 练习接口(exercises/,Task 12)

- `01-asm-readback/`:README(防剧透:任务与提示在前/参考思路在后)+`check-answer.sh`(对 `../01-bpf-asm/xdp_count.bpf.o` 的 llvm-objdump -d 输出断言特征序列:`r0 = 0x2`、`r0 = 0x1`、`r0 = 0x0`、`if r1 == 0xdd86`、`exit`;WALK_TREE 不适用——断言对象是仓库内 .bpf.o,依赖 llvm-objdump 在 PATH;汇总 PASS/FAIL 语义同 C2)。参考实现=逐行 source↔bytecode 对照表 `answer.md`。
- `02-extend-jit/`:README(防剧透同构)+参考实现 `trace-blind-station.sh`(独立小脚本:对 core.c 切 bpf_jit_blind_insn :1250 与 blind_constants :1428 两站,接口同 C3 但 STATIONS=2)+`check-trace.sh`(bash -n+实跑断言两锚点)。

### C7. 章节标题固定

`# 第 6 章 JIT 编译与性能优化`;十一小节:学习目标/本章导读/一、三种执行形态与那排开关/二、x86_64 JIT:从字节码到机器码的六站/三、arm64:另一套后端的同构与差异/四、实战:亲手摸到字节码与机器码/可视化:解释器与 JIT 的执行路径对比/常见问题与排错/课后扩展任务/总结/本章参考来源。

### C8. 交叉链接

同 ch05 契约:章首示例目录链接、各段主轴末「独立示例」、附录 B/C ≥1;核对 `grep -c "](../examples/chapter-06/" ≥ 8`、`grep -c "](../appendix/" ≥ 1`。

### C9. 01-bpf-asm 锚点程序(逐字,冒烟已验证)

```c
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_count(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;

	if ((void *)(eth + 1) > data_end)
		return XDP_ABORTED;

	if (eth->h_proto == __builtin_bswap16(0x86dd))
		return XDP_DROP;

	return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

编译命令(README 逐字):`clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu -c xdp_count.bpf.c -o xdp_count.bpf.o`;反汇编:`llvm-objdump -d xdp_count.bpf.o`。Makefile 目标:`all: xdp_count.bpf.o` + `disasm: ; llvm-objdump -d xdp_count.bpf.o` + `clean`。

## Tasks

### Task 1: 00-env 自检 + 章级 README 骨架

- [ ] Task 1: 00-env 自检 + 章级 README 骨架

**tasks.md 条目**:「Include commands for inspecting …」的环境基座部分 + C5 总览骨架。

**Files:**
- Create: `book/examples/chapter-06/00-env/check-env.sh`(可执行)
- Create: `book/examples/chapter-06/README.md`

**Interfaces:**
- Produces: check-env.sh 的 sysctl 可读性分级表(enable 可读/harden·kallsyms·limit root-only)=正文一节引用;README 骨架(六行目录表/学习路径/三档验证状态约定/机检汇总占位「由 Task 11 回填」/走读树声明)。

- [ ] **Step 1: 写 check-env.sh**——①走读树版本(grep ^SUBLEVEL)+ JIT 三源文件在场(comp.c/bpf_jit_comp32.c/arm64 comp.c/bpf_jit.h)②sysctl 分级诊断:逐个 `cat` 五个 `net.core/bpf_jit_*`,可读的打印值、EACCES 的打印「root-only(600)」并解释(教学点);`perf_event_paranoid` 读数与解读③工具:clang(-target bpf 支持探针:编译一条最小指令?否——只查版本与 llvm-objdump 在场)、llvm-objdump、bpftool④CONFIG:读 /boot/config-$(uname -r) 的 CONFIG_BPF_JIT/ALWAYS_ON/DEFAULT_ON 并解读 ALWAYS_ON 含义⑤BTF。全部只读;无树 exit 2(同 ch05 惯例)。
- [ ] **Step 2: 实跑(硬性)**——`bash -n` + 实跑 exit=0;分级表与本机事实一致(enable=1/三个 EACCES/paranoid=4)。
- [ ] **Step 3: 章级 README 骨架**——C5 结构;标注「citations.txt 在本目录树根(Task 2 落地)」。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-06/
git commit -m "feat(ch06): 00-env sysctl 分级自检与章级示例 README 骨架"
```

---

### Task 2: 01-bpf-asm 反汇编锚点 + citations.txt

- [ ] Task 2: 01-bpf-asm 反汇编锚点 + citations.txt

**tasks.md 条目**:「explaining interpreter mode, JIT mode…」的资产基座(锚点程序+引用清单)。

**Files:**
- Create: `book/examples/chapter-06/01-bpf-asm/{xdp_count.bpf.c,Makefile,disasm.sh,README.md}`(disasm.sh 可执行)
- Create: `book/examples/chapter-06/citations.txt`

**Interfaces:**
- Consumes: C9 锚点程序(逐字)。
- Produces: xdp_count.bpf.o(构建产物,gitignore 不入库——**需在 Task 2 检查仓库 .gitignore 是否覆盖 `*.bpf.o`**(ch04 已有全局规则则不动);README 逐行 source↔bytecode 对照表(节选 ≥8 行,与实跑逐字一致);citations.txt(C1 全量清单实测修正)。

- [ ] **Step 1: 写锚点四件套**(C9 逐字;注释中文,头部「只编译不加载,无挂载风险」)。
- [ ] **Step 2: 编译+反汇编实跑(硬性)**——make 产 .bpf.o;`./disasm.sh` 输出 14 条指令;README 对照表节选与实跑逐字一致(把实跑全文贴进报告)。
- [ ] **Step 3: citations.txt 逐行实测**(C1 五组)——未命中修清单留痕;`grep -vc "^#\|^$" citations.txt` 计数。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-06/01-bpf-asm/ book/examples/chapter-06/citations.txt
git commit -m "feat(ch06): 反汇编锚点(编译级实跑)与全量引用锚点清单"
```

---

### Task 3: verify-citations.sh 符号机检

- [ ] Task 3: verify-citations.sh 符号机检

**Files:**
- Create: `book/examples/chapter-06/scripts/verify-citations.sh`(可执行)

**Interfaces:**
- Consumes: C1 citations.txt(路径差异:本任务先修改 C2 基底的数据源行,不读 ch05 的 01-source-map)。
- Produces: 汇总行口径(正文四节/README 回填用)。

- [ ] **Step 1: 写脚本**(C2 逐字;中文注释;`set -u`)。
- [ ] **Step 2: 首跑(硬性)**——全部 PASS,FAIL=0,exit=0;`WALK_TREE=/nonexistent` 负路径 exit=1。
- [ ] **Step 3: 提交**

```bash
git add book/examples/chapter-06/scripts/
git commit -m "feat(ch06): 符号机检脚本(引用锚点全绿)"
```

---

### Task 4: 02-jit-x86 切片器六站

- [ ] Task 4: 02-jit-x86 切片器六站

**Files:**
- Create: `book/examples/chapter-06/02-jit-x86/{trace-jit-x86.sh,README.md}`(脚本可执行)

**Interfaces:**
- Produces: C3 六站输出(与设计 §3.2 二节六站一一对应)——正文二节逐站引用;README 实跑摘录首尾各 10 行。

- [ ] **Step 1: 写脚本**(C3;基线行号以「现状基线」表为准、自查 grep 实测校准;每段 ≤25 行)。
- [ ] **Step 2: 实跑(硬性)**——`bash -n`+实跑 `STATIONS=6` exit=0 无 NOT FOUND;失败模式实测(树不存在 exit 1)。
- [ ] **Step 3: README**——用途/调用/预期输出示例(实跑摘录)/失败模式。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-06/02-jit-x86/
git commit -m "feat(ch06): x86 JIT 切片器(六站实跑通过)"
```

---

### Task 5: 03-jit-arm64 切片器四站

- [ ] Task 5: 03-jit-arm64 切片器四站

**Files:**
- Create: `book/examples/chapter-06/03-jit-arm64/{trace-jit-arm64.sh,README.md}`(脚本可执行)

**Interfaces:**
- Consumes: C3 同构;C4 无关。
- Produces: 四站输出——正文三节引用;第四站职责注必须含「与 x86 对照」。

- [ ] **Step 1: 写脚本**——四站:①bpf_int_jit_compile :1518②build_body :1442 分派③bpf2a64 映射(:47 区间+bpf_jit.h A64_R)④差异(flush_icache 调用点/emit_a64_mov_i64 立即数序列;与 x86 对照职责注)。
- [ ] **Step 2: 实跑(硬性)**——`STATIONS=4` exit=0。
- [ ] **Step 3: README**——同 Task 4 口径。
- [ ] **Step 4: 提交**

```bash
git add book/examples/chapter-06/03-jit-arm64/
git commit -m "feat(ch06): arm64 JIT 切片器(四站实跑通过)"
```

---

### Task 6: 正文——学习目标、导读与「一、三种执行形态与那排开关」

- [ ] Task 6: 正文——学习目标、导读与「一、三种执行形态与那排开关」

**Files:**
- Create: `book/chapters/06-jit-compilation-and-performance.md`

**Interfaces:**
- Consumes: Task 1 check-env 分级表(正文引用其输出节选)。
- Produces: C7 骨架(HTML 注释占位后续小节,任务级行不删)+目标/导读/一节成品(7–9k)。

- [ ] **Step 1: 目标+导读(2–2.5k)**——学习目标 4–5 条;导读兑现 ch04:805(生命周期第三步)+ ch02:131(架构相关)+ ch05「深挖留第 6 章」回收预告;章首示例目录链接。
- [ ] **Step 2: 一节(7–9k)**——三种执行形态(解释器 ___bpf_prog_run :1679 调度循环+PROG_NAME_LIST :2220-2225 栈长分派技巧;JIT select_runtime :2357 回指 ch05;offload 一句);为什么 JIT 重要;sysctl 四兄弟内核出处(:562-565)与 CONFIG 默认值;**本机 ALWAYS_ON=y 实况解读**(教师视角素材);学生动手=check-env 分级表三条命令+预期输出;检查点 3 问;节末链接 00-env。
- [ ] **Step 3: 篇幅自查**(wc -m)。
- [ ] **Step 4: 提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md
git commit -m "docs(ch06): 目标导读与一节(三执行形态与开关)"
```

---

### Task 7: 正文——「二、x86_64 JIT 六站」

- [ ] Task 7: 正文——「二、x86_64 JIT 六站」

**Files:**
- Modify: `book/chapters/06-jit-compilation-and-performance.md`

**Interfaces:**
- Consumes: Task 4 六站输出(行号/区间以切片器实跑校准为准)。
- Produces: 二节成品(9–11k),与切片器一一对应。

- [ ] **Step 1: 写二节**——六站(设计 §3.2):选通入口(回指 ch05 二节 2.4 显式回收「深挖」)/do_jit 与两趟(extra_pass 为什么)/emit_prologue(寄存器映射表:BPF r0-r10→x86、调用约定、栈帧;回指 ch02 2.1)/lowering 逐例 4 例(每例 ≤15 行,BPF↔x86 对照)/helper 与 tail call(get_func_addr core.c:1210、两趟关系;回指 ch04 helper 白名单)/致盲与映像分配(blind_constants core.c:1428、harden 关系、bpf_prog_pack)。每站签名+职责+贴码(≤25 行,与树逐字)+失败出口(致盲改写=预期行为教学点)。教师视角(两趟编译与一次解码的经济学)/学生动手(切片器 3 命令+预期输出)/检查点 3 问。
- [ ] **Step 2: 篇幅自查**(9–11k;新行号全部实测)。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md
git commit -m "docs(ch06): 二节(x86_64 JIT 六站)"
```

---

### Task 8: 正文——「三、arm64」

- [ ] Task 8: 正文——「三、arm64」

**Files:**
- Modify: `book/chapters/06-jit-compilation-and-performance.md`

**Interfaces:**
- Consumes: Task 5 四站输出。
- Produces: 三节成品(4–6k);不逐指令深挖(矩阵②「at a high level」)。

- [ ] **Step 1: 写三节**——入口同构(build_body :1442);bpf2a64 映射表对照 x86(:47 区间贴码,与 x86 映射对照表格);差异三点(icache flush/MOVZ-MOVK 立即数序列 vs x86 单 imm32/可选 LSE);「第三套后端怎么读」方法迁移收尾;教师视角(同构性=框架与后端分层)/学生动手(arm64 切片器 3 命令)/检查点 2–3 问;节末链接 03-jit-arm64。
- [ ] **Step 2: 篇幅自查**。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md
git commit -m "docs(ch06): 三节(arm64 同构与差异)"
```

---

### Task 9: 正文——「四、实战」五层

- [ ] Task 9: 正文——「四、实战」五层

**Files:**
- Modify: `book/chapters/06-jit-compilation-and-performance.md`

**Interfaces:**
- Consumes: Task 1 check-env、Task 3 机检汇总、Task 4/5 切片器、01-bpf-asm 实跑输出。
- Produces: 四节成品(6–8k);五层+四段节奏。

- [ ] **Step 1: 写五层**——①依赖(工具矩阵/走读树声明回指)②「编译」环境=机检+反汇编(verify-citations 一段回指 ch05;**01-bpf-asm 全流程实跑:make→disasm,对照表节选 ≥8 行与实跑逐字一致**——本章编译级真实物)③部署=定位(JIT 源文件地图:arch/*/net/ 布局+comp32 一句话;命令目录回指 ch05)④运行(两切片器实跑)⑤效果(`bpftool prog dump xlated [<id>] opcodes`/`jited` 命令讲解与预期输出(**运行级待验证**,需加载;`file`/`opcodes` 参数);性能对比方法论:ALWAYS_ON 本机不可关→方法迁移(可关内核步骤+`bpf_jit_enable` 0/1+安全注「生产禁关」);perf_event_paranoid=4 本机限制与替代口径;预期输出全部标注)。四段节奏齐。
- [ ] **Step 2: 篇幅自查**(≤8k)。
- [ ] **Step 3: 提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md
git commit -m "docs(ch06): 四节(实战五层:字节码、机器码与方法论)"
```

---

### Task 10: 正文——可视化、排错、课后、总结、参考

- [ ] Task 10: 正文——可视化、排错、课后、总结、参考

**Files:**
- Modify: `book/chapters/06-jit-compilation-and-performance.md`

**Interfaces:**
- Consumes: 章内既有事实(机检 43±锚点数、切片器站数、全章累计 wc -m)。
- Produces: 后五段成品(可视化 1.5–2k/排错 2.5–3.5k/课后 1–1.5k/总结+参考 1.5–2k);全章落 44–48k 目标区间(超 48k 压缩)。

- [ ] **Step 1: 可视化两图**——①解释器 vs JIT 执行路径对比文本图(同一程序两路:调度循环逐条 vs 一次编译原生执行;标注前移成本/每条开销/ALWAYS_ON 提示)②x86 JIT 六站流水图;各配 ≥150 字解读。
- [ ] **Step 2: 排错**——走读类(致盲后指令对不上=预期,回指二节⑥;两趟反汇编差异;jited dump 需特权)、sysctl 类(harden 等读不了=root-only 回指一节)、方法论类(ALWAYS_ON 无法关→迁移实验设计;perf 无权限替代口径)、机检三类(回指 ch05 一句带过)。
- [ ] **Step 3: 课后扩展任务节**——2 项练习描述(防剧透,不给答案;链接 exercises/ 两目录)。
- [ ] **Step 4: 总结+参考**——5 点内化呼应学习目标;参考来源(源码树 v6.6.78 x86/arm64 JIT 文件、Documentation/bpf/(index/clang-notes/classic_vs_extended)、include/uapi/linux/bpf.h、ch02 2.1/ch04/ch05 回指)。
- [ ] **Step 5: 篇幅自查+提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md
git commit -m "docs(ch06): 可视化/排错/课后/总结/参考收尾"
```

---

### Task 11: 交叉链接、机检终跑、篇幅终检与清扫

- [ ] Task 11: 交叉链接、机检终跑、篇幅终检与清扫

**Files:**
- Modify: `book/chapters/06-jit-compilation-and-performance.md`
- Modify: `book/examples/chapter-06/README.md`(回填机检实测与「Task N 落地」更新)

**Interfaces:**
- Consumes: 台账累积 minor 清单(派发时由控制器提供)。
- Produces: C8 核对值、机检终值、wc -m 终值回填。

- [ ] **Step 1: C8 核对与附录链接**(≥8/≥1;附录文件名 ls 实证)。
- [ ] **Step 2: 机检终跑(硬性)+README 回填**(FAIL=0;TOTAL/PASS/树版本回填;exercises/ 行更新落地状态)。
- [ ] **Step 3: 保守措辞复核(硬性)**——零「已验证通过」grep;主审⑦双清单预演:正文逐条可归入「已验证(机检/编译/反汇编/只读 sysctl/CONFIG)」或「预期(xlated/jited/性能对比/harden)」。
- [ ] **Step 4: 篇幅终检(硬性)**——35000–50000。
- [ ] **Step 5: 清扫**(控制提供的累积 minor 清单逐项处置留痕)。
- [ ] **Step 6: 提交**

```bash
git add book/chapters/06-jit-compilation-and-performance.md book/examples/chapter-06/
git commit -m "docs(ch06): 交叉链接、机检终跑回填与篇幅终检"
```

---

### Task 12: 两项课后练习参考实现

- [ ] Task 12: 两项课后练习参考实现

**Files:**
- Create: `book/examples/chapter-06/exercises/01-asm-readback/{README.md,answer.md,check-answer.sh}`(脚本可执行)
- Create: `book/examples/chapter-06/exercises/02-extend-jit/{README.md,trace-blind-station.sh,check-trace.sh}`(脚本可执行)
- Modify: `book/examples/chapter-06/README.md`(exercises/ 行落地)

**Interfaces:**
- Consumes: C4 接口;01-bpf-asm 的 .bpf.o 反汇编(Task 2 产物,脚本运行时现场 make/或要求先 make——**check-answer.sh 内部先 make 01-bpf-asm 保证自足**)。
- Produces: 两项完整参考实现,实跑全绿。

- [ ] **Step 1: 练习①**——README 防剧透(任务:对 xdp_count 产出逐行 source↔bytecode 对照;提示:从 .c 行号与 llvm-objdump -d 输出对照入手;参考思路在后)+`answer.md` 对照表(≥8 行,与实跑逐字)+`check-answer.sh`(make 01-bpf-asm→llvm-objdump→断言五特征:`r0 = 0x2`/`r0 = 0x1`/`r0 = 0x0`/`if r1 == 0xdd86`/`exit`;汇总同 C2)。
- [ ] **Step 2: 练习①实跑(硬性)**——正路径全 PASS exit=0;负路径(.o 不存在时脚本自动 make 的自足性验证)。
- [ ] **Step 3: 练习②**——README 防剧透(任务:给 trace-jit-x86.sh 加致盲细节站;提示:blind_insn 改写示例段在 core.c:1250 起;不给行号?——**给**:该锚点是正文二节⑥已公开内容,回指合规)+`trace-blind-station.sh`(两站:blind_insn :1250 改写示例区间/blind_constants :1428;STATIONS=2)+`check-trace.sh`(bash -n+实跑断言两锚点)。
- [ ] **Step 4: 练习②实跑(硬性)**——check-trace.sh 全绿 exit=0。
- [ ] **Step 5: 提交**

```bash
git add book/examples/chapter-06/exercises/ book/examples/chapter-06/README.md
git commit -m "feat(ch06): 两项 JIT 走读练习参考实现(机检全绿)"
```

---

### Task 13: 主审(验证双清单专项)+ tasks.md 勾选(收口)

- [ ] Task 13: 主审(验证双清单专项)+ tasks.md 勾选(收口)

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`(勾选)
- 产出:审查结论进最终回复

- [ ] **Step 1: 逐项审计**(教学完整性四条/内核路径准确性(行号抽 ≥15 处实测)/结构级/术语与保守措辞)+ **矩阵⑦专项:验证双清单**——把正文中全部效果类声明逐条归入「已验证(机检/编译/反汇编/只读 sysctl/CONFIG)」或「预期(xlated/jited/性能对比/harden/perf)」,任何一条归类错误的回对应任务修复。
- [ ] **Step 2: 勾选 tasks.md**——「Chapter 6」7 条置 [x]。
- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch06): 勾选 Chapter 6 矩阵任务"
```

---

## 自审记录

1. **Spec 覆盖**:矩阵 7 条 → ①T1+T6;②T4+T7+T5+T8;③T9;④T9+T10;⑤T10;⑥T10;⑦T11+T13。无缺口。
2. **占位符扫描**:C9 锚点程序逐字给定且冒烟通过;任务无 TBD。
3. **类型一致性**:citations 位置(C1=树根)与 C2 数据源行一致;切片器接口 C3 与 T4/T5/C4-练习②一致;汇总行口径 C2/T11 一致。

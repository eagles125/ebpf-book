# 第 6 章独立示例资产

本目录是《第 6 章 JIT 编译与性能优化》的配套走读资产,对应正文「一、三种执行形态与那排开关」「二、x86_64 JIT:从字节码到机器码的六站」「三、arm64:另一套后端的同构与差异」「四、实战:亲手摸到字节码与机器码」四节。本章资产围绕「JIT 走读」组织:一份全章共享的引用锚点清单(位于本目录树根)、一个反汇编锚点(01-bpf-asm,本章唯一的构建型资产:只编译不加载)、两个源码切片器(x86 六站 / arm64 四站)、一个符号机检脚本,把「摸到字节码与机器码」从口号变成可复现、可机检的动作。

## 演示目标

把「同一个 BPF 程序的三种执行形态(解释器 / JIT / offload)与那排开关」拆成四件可操作的事:

- **开关与形态**(`00-env/`):`check-env.sh` 做 sysctl 可读性分级诊断——同一排 `bpf_jit_*` 开关,`bpf_jit_enable` 文件权限 0644 人人可读,`bpf_jit_harden`/`bpf_jit_kallsyms`/`bpf_jit_limit` 0600 仅 root 可读(非特权读取返回 EACCES),外加 `perf_event_paranoid` 读数与运行内核 CONFIG 三项解读——「读不到」不等于「不存在」,这本身就是本章教学点;
- **编译级真实物**(`01-bpf-asm/`):clang `-target bpf` 编译锚点程序 xdp_count,再用 llvm-objdump 反汇编,得到 source↔bytecode 逐行对照——不加载、无挂载风险;
- **两条走读链**(`02-jit-x86/`、`03-jit-arm64/`):x86_64 JIT 从字节码到机器码的六站,与 arm64 后端的同构与差异(四站)各一个切片器,对走读树实跑,提取每站定位行、关键代码段与职责注;
- **可机检的引用**(`scripts/`):`verify-citations.sh` 对树根 `citations.txt` 逐条做「文件存在 + 符号 grep」机检——源码走读章的「编译级」等价物。

## 目录结构

| 目录 | 类别 | 内容 | 落地状态 |
|---|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:走读树与 JIT 四源文件 / sysctl 可读性分级 / 工具链(只查版本) / CONFIG 三项 / BTF,末尾三档结论 | 已落地:本机实跑五检查区+三档结论通过;分级表与本机事实一致(enable=1 可读、harden/kallsyms/limit root-only(600)、perf_event_paranoid=4) |
| `01-bpf-asm/` | 编译级锚点 | `xdp_count.bpf.c` + `Makefile` + `disasm.sh`:clang -target bpf 编译 + llvm-objdump -d 反汇编,只编译不加载 | Task 2 落地 |
| `02-jit-x86/` | 源码切片器 | `trace-jit-x86.sh`:x86_64 JIT 六站(选通入口/do_jit 两趟/emit_prologue/逐例 lowering/helper 与 tail call/致盲与映像分配) | Task 4 落地 |
| `03-jit-arm64/` | 源码切片器 | `trace-jit-arm64.sh`:arm64 JIT 四站(入口/build_body 分派/bpf2a64 映射/与 x86 对照) | Task 5 落地 |
| `scripts/` | 符号机检 | `verify-citations.sh`:机检树根 citations.txt 逐条「文件存在 + 符号 grep」,输出 TOTAL/PASS/FAIL 汇总 | Task 3 落地 |
| `exercises/` | 课后扩展 | `01-asm-readback/`(反汇编回读对照)、`02-extend-jit/`(常量致盲站扩展),均含防剧透参考实现 | Task 12 落地 |

> **citations.txt 在本目录树根**(即本 README 旁,不在子目录):它是全章共享接口、机检脚本唯一数据源,Task 2 落地。

## 学习路径

1. `bash 00-env/check-env.sh` 自检:无 root 也可运行,按三档口径给出结论;重点看②的 sysctl 可读性分级表——哪些开关读得到、哪些 root-only,以及④的 ALWAYS_ON 解读(本机 JIT 为什么关不掉);走读树不在场时按提示从 kernel.org 获取。
2. 进入 `01-bpf-asm/`:`make` 编译锚点程序,`./disasm.sh` 反汇编,得到 source↔bytecode 对照(Task 2 落地)。
3. 用 `scripts/verify-citations.sh` 机检树根 `citations.txt`:汇总行 `FAIL=0` 即全绿,再开始走链(Task 3 落地)。
4. 对照正文二节实跑 `02-jit-x86/trace-jit-x86.sh`,对照正文三节实跑 `03-jit-arm64/trace-jit-arm64.sh`:每站 = 定位行(标注 v6.6.78)+ 关键代码段 + 职责注(Task 4/5 落地)。
5. 进入 `exercises/`:先自行完成走读/反汇编任务,再看各练习 README 后部的参考实现思路(防剧透结构)(Task 12 落地)。

## 验证状态约定(三档口径)

- **编译级与机检:已验证可复现**——本章唯一构建型资产 01-bpf-asm 只编译不加载(clang -target bpf + llvm-objdump -d),机检与切片脚本对走读树实跑(`bash -n` 语法检查 + 实跑断言)——均无 root 可真实验证。
- **加载级与 bpftool dump(bpftool prog dump xlated/jited 等)**:需要加载 BPF 程序(root 档;本机 `kernel.unprivileged_bpf_disabled=2` 且无 root)——一律标注「预期输出示例 / 运行级待验证」。
- **性能对比方法论:预期**——本机 `CONFIG_BPF_JIT_ALWAYS_ON=y`(解释器编译期移除,`bpf_jit_enable` 被锁成 1),JIT 开/关对比无法在本机真实验证,只给方法论(可在关闭 ALWAYS_ON 的内核上按步骤复现,安全注「生产禁关」);`perf_event_paranoid=4` 进一步收紧非特权 perf 取证。

## 机检汇总

本机终跑实测(脚本 `scripts/verify-citations.sh`,数据源为本目录树根 `citations.txt`):

```
TOTAL=32 PASS=32 FAIL=0 WALK_TREE=/work/linux/linux-6.6.78 TREE_VERSION=v6.6.78
```

退出码 0;负路径 `WALK_TREE=/nonexistent` 时逐条 `FAIL 文件不在树`、汇总 `FAIL=32`、退出码 1(门禁行为符合设计)。32 条锚点分五组:①`kernel/bpf/core.c` 执行形态与开关 13 条;②x86 后端 `arch/x86/net/bpf_jit_comp.c` 7 条;③arm64 后端 5 条(含 `bpf_jit.h`);④核心对象与辅助结构 4 条;⑤UAPI 3 条。逐条说明见 `citations.txt` 行尾注释。

## 走读树版本声明

本目录全部机检与切片脚本默认针对 `/work/linux/linux-6.6.78`(Linux v6.6.78 完整源码树,非 git)实跑;正文所有内核路径/行号引用锚定该树,行号一律标注「(v6.6.78)」并注明随版本漂移。换用其他版本树时以 `WALK_TREE=<路径>` 覆盖默认值,行号需重新校准——机检按符号不按行号,不受行号漂移影响。

## 安全与只读声明

全部脚本只读源码树与系统文件(`/sys`、`/proc`、`/boot`),不加载任何 eBPF 程序,无生产系统风险;唯一例外 `01-bpf-asm/` 只编译不加载(生成 `.bpf.o` 并反汇编,无挂载、无运行行为)。

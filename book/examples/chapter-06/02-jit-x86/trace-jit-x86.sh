#!/usr/bin/env bash
# trace-jit-x86.sh —— 第 6 章《JIT 编译与性能优化》x86_64 JIT 六站切片器
#
# 【脚本用途与输入】
# 沿「解释器→JIT 选通→逐条 lowering→helper/tail call→致盲与映像分配」的
# 编译路径,在 eBPF 走读源码树上按「站」切出六段关键代码,供正文《二、
# x86_64 JIT:从字节码到机器码的六站》逐站引用其行号与代码段。
#   - 用法:bash trace-jit-x86.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/core.c、arch/x86/net/bpf_jit_comp.c,
#     不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各段注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 六站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78 校准)
#     + sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注;
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间
#     (偏移按 v6.6.78 校准),树小幅漂移时区间随锚点自动平移;
#   - 末尾固定汇总:TREE_VERSION=v6.6.78 STATIONS=6
#     (TREE_VERSION 是书稿锁定版本常量,非自动探测);
#   - 退出码:六站全成 → 0;任一站提取失败 → 该站打印「STATION n: NOT FOUND」,
#     其余站继续,退出码 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F/-n 定位)、sed(-n 'A,Bp')、awk(前缀剥离与版本
#     软校验),无其他外部命令;
#   - 依赖缺失:启动即报错退出(1),绝不产出「假切片」;
#   - 树目录或关键文件缺失:启动即报错退出(1),不逐站刷 NOT FOUND;
#   - 树版本 ≠ 6.6.78:stderr 打印 WARN 后继续(行号仅供参考,以该树实测为准)。
#
# 【失败模式】
#   1. 树不在:WALK_TREE 指错目录或源码未解压 —— 启动报错,退出码 1;
#      处理:把 WALK_TREE 指向正确的 6.6.78 树后重跑;
#   2. 锚点漂移:树在,但 grep 定位为空(内核改名/移动/内联符号)—— 该站打印
#      「STATION n: NOT FOUND」,退出码 1;
#      处理:回到树里 grep 新位置,改本脚本该站的锚点模式与偏移(同步更新校准
#      注释),再重跑;
#   3. 版本不符:换了树版本 —— 看 stderr 的 WARN;行号漂移属预期,正文引用
#      以 v6.6.78 实测为准。
set -u

TREE_VERSION="v6.6.78"
DEFAULT_TREE="/work/linux/linux-6.6.78"

# ---------- 入参与依赖 ----------

if [[ $# -gt 1 ]]; then
    printf '用法:bash %s [WALK_TREE](默认 %s)\n' "$0" "$DEFAULT_TREE" >&2
    exit 1
fi
WALK_TREE="${1:-${WALK_TREE:-$DEFAULT_TREE}}"
TREE="$WALK_TREE"   # 路径拼接用简称(与 WALK_TREE 同值)

for cmd in grep sed awk; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf '缺少依赖命令 %s,无法切片\n' "$cmd" >&2
        exit 1
    fi
done

if [[ ! -d $WALK_TREE ]]; then
    printf '走读树目录不存在:%s\n' "$WALK_TREE" >&2
    exit 1
fi
for rel in kernel/bpf/core.c arch/x86/net/bpf_jit_comp.c; do
    if [[ ! -f $WALK_TREE/$rel ]]; then
        printf '树内关键文件缺失:%s(WALK_TREE 指错目录或树版本不符?)\n' "$rel" >&2
        exit 1
    fi
done

# 版本软校验:Makefile 在场但版本不是 6.6.78 时警告(stderr),不中断
if [[ -r $WALK_TREE/Makefile ]]; then
    ver=$(awk '/^VERSION = /{v=$3} /^PATCHLEVEL = /{p=$3} /^SUBLEVEL = /{s=$3} \
        END{print v"."p"."s}' "$WALK_TREE/Makefile")
    if [[ $ver != 6.6.78 ]]; then
        printf 'WARN: 树版本 %s 与书稿锁定版本 %s 不符,行号仅供参考\n' \
            "$ver" "$TREE_VERSION" >&2
    fi
fi

# ---------- 通用小工具 ----------

# 定位:grep -nF 找锚点,取第 $3 个命中的行号;未命中输出空串
locate() { # $1 绝对文件 $2 模式 $3 第几个命中
    grep -nF -- "$2" "$1" | sed -n "${3}p" | cut -d: -f1
}

# 定位块:展示 grep -n 实测输出(去掉树前缀),未命中时给一行提示
show_locate() { # $1 树内相对路径 $2 -F|-E $3 模式
    local out st
    out=$(grep -n "$2" -- "$3" "$WALK_TREE/$1" 2>/dev/null)
    st=$?
    if [[ $st -ne 0 ]]; then
        printf '  (定位未命中:grep -n %s %s %s)\n' "$2" "$3" "$1"
        return 1
    fi
    printf '%s\n' "$out" | awk -v p="$TREE/" \
        'index($0,p)==1{print substr($0,length(p)+1);next}{print}'
    return 0
}

# 代码段:按「锚点行 + 偏移」切出区间并展示实际 sed 命令(两步法的第二步)
seg() { # $1 绝对文件 $2 锚点行 $3 起偏移 $4 止偏移 $5 相对路径 $6 说明
    local a=$(( $2 + $3 )) b=$(( $2 + $4 ))
    (( a < 1 )) && a=1
    (( b < a )) && b=$a
    printf '### 代码段:sed -n '"'"'%d,%dp'"'"' %s —— %s\n' "$a" "$b" "$5" "$6"
    sed -n "${a},${b}p" "$1"
}

station_fail() { # $1 站号
    printf 'STATION %d: NOT FOUND\n' "$1"
    printf '  (锚点未命中,见上方定位块;回树重新 grep 校准锚点与偏移后重跑)\n'
}

# ---------- 六站 ----------

# 站 1:选通与入口(解释器→JIT)
# 校准(v6.6.78 实测):select_runtime 定义=core.c:2357;bpf_int_jit_compile
# 调用点=core.c:2384;x86 入口定义=comp.c:2754
station_1() {
    local cf=kernel/bpf/core.c xf=arch/x86/net/bpf_jit_comp.c
    local ac="$TREE/$cf" ax="$TREE/$xf"
    local miss=0 l_def l_call l_entry
    printf '\n## 站 1:选通与入口(解释器→JIT)\n'
    l_def=$(locate "$ac" 'struct bpf_prog *bpf_prog_select_runtime(struct bpf_prog *fp, int *err)' 1)
    l_call=$(locate "$ac" 'fp = bpf_int_jit_compile(fp);' 1)
    l_entry=$(locate "$ax" 'struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)' 1)
    [[ -n $l_def && -n $l_call && -n $l_entry ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$cf" -F 'struct bpf_prog *bpf_prog_select_runtime(struct bpf_prog *fp, int *err)'
    show_locate "$cf" -F 'fp = bpf_int_jit_compile(fp);'
    show_locate "$xf" -F 'struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = select_runtime 定义锚点 +0..+19 → v6.6.78 实际 2357..2376(20 行)
    seg "$ac" "$l_def" 0 19 "$cf" '选通入口:jit_needed 判定(ALWAYS_ON/kfunc)+ bpf_prog_select_func'
    # 区间 = 调用点锚点 +0..+5 → v6.6.78 实际 2384..2389(6 行)
    seg "$ac" "$l_call" 0 5 "$cf" '调架构 JIT;未 JIT 且必须 JIT → -ENOTSUPP(回指 ch05 站 4)'
    # 区间 = x86 入口定义锚点 +0..+19 → v6.6.78 实际 2754..2773(20 行)
    seg "$ax" "$l_entry" 0 19 "$xf" 'x86 编译入口:jit_requested 门 + 常量致盲预处理(bpf_jit_blind_constants)'
    printf '职责:验证通过后选定执行引擎——core.c 把程序交给架构后端;x86 入口先做 jit_requested 门与致盲预处理。\n'
    return 0
}

# 站 2:do_jit 与两趟编译
# 校准(v6.6.78 实测):do_jit 定义=comp.c:1075(aux->stack_depth 读取=1096);
# extra_pass = true=2803(decl=2763);pass 循环=2830
station_2() {
    local xf=arch/x86/net/bpf_jit_comp.c
    local ax="$TREE/$xf"
    local miss=0 l_dojit l_extra l_pass
    printf '\n## 站 2:do_jit 与两趟编译\n'
    l_dojit=$(locate "$ax" 'static int do_jit(struct bpf_prog *bpf_prog' 1)
    l_extra=$(locate "$ax" 'extra_pass = true;' 1)
    l_pass=$(locate "$ax" 'for (pass = 0; pass < MAX_PASSES || image; pass++) {' 1)
    [[ -n $l_dojit && -n $l_extra && -n $l_pass ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$xf" -F 'static int do_jit(struct bpf_prog *bpf_prog'
    show_locate "$xf" -F 'bool extra_pass = false;'
    show_locate "$xf" -F 'extra_pass = true;'
    show_locate "$xf" -F 'for (pass = 0; pass < MAX_PASSES || image; pass++) {'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = do_jit 定义锚点 +0..+23 → v6.6.78 实际 1075..1098(24 行)
    seg "$ax" "$l_dojit" 0 23 "$xf" 'do_jit 头:逐条 lowering 主循环;寄存器使用分析后调 emit_prologue(读 aux->stack_depth)'
    # 区间 = extra_pass=true 锚点 -8..+3 → v6.6.78 实际 2795..2806(12 行)
    seg "$ax" "$l_extra" -8 3 "$xf" '两趟的由来:上一趟的 jit_data(addrs/image)还在 → extra_pass=true 接着编'
    # 区间 = pass 循环锚点 +0..+14 → v6.6.78 实际 2830..2844(15 行)
    seg "$ax" "$l_pass" 0 14 "$xf" '编译循环:反复调 do_jit 直到 proglen 收敛;失败即回退解释器'
    printf '职责:do_jit 是把 BPF 指令逐条翻译成 x86 的主循环;「两趟」来自第一趟指令地址未知——第二趟(extra_pass)用已记录的 addrs 重放。\n'
    return 0
}

# 站 3:emit_prologue 与寄存器映射
# 校准(v6.6.78 实测):映射注释=comp.c:162;reg2hex=171..186;
# reg2pt_regs=188..199;emit_prologue 定义=343(函数体止于 372)
station_3() {
    local xf=arch/x86/net/bpf_jit_comp.c
    local ax="$TREE/$xf"
    local miss=0 l_map l_pt l_pro
    printf '\n## 站 3:emit_prologue 与寄存器映射\n'
    l_map=$(locate "$ax" 'The following table maps BPF registers to x86-64 registers.' 1)
    l_pt=$(locate "$ax" 'static const int reg2pt_regs[] = {' 1)
    l_pro=$(locate "$ax" 'static void emit_prologue(u8 **pprog, u32 stack_depth, bool ebpf_from_cbpf,' 1)
    [[ -n $l_map && -n $l_pt && -n $l_pro ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$xf" -F 'The following table maps BPF registers to x86-64 registers.'
    show_locate "$xf" -F 'static const int reg2pt_regs[] = {'
    show_locate "$xf" -F 'static void emit_prologue(u8 **pprog, u32 stack_depth, bool ebpf_from_cbpf,'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = 映射注释锚点 -1..+8 → v6.6.78 实际 161..170(10 行)
    seg "$ax" "$l_map" -1 8 "$xf" '映射表设计说明:R12 弃用、R9 留给 trampoline、R10 供致盲用'
    # 区间 = 映射注释锚点 +9..+24 → v6.6.78 实际 171..186(16 行)
    seg "$ax" "$l_map" 9 24 "$xf" 'reg2hex:BPF r0-r10 → x86 寄存器(调用约定在此落地;回指 ch02 2.1)'
    # 区间 = reg2pt_regs 锚点 +0..+11 → v6.6.78 实际 188..199(12 行)
    seg "$ax" "$l_pt" 0 11 "$xf" 'reg2pt_regs:BPF 寄存器在 pt_regs 里的偏移(cbpf 兼容/上下文读取用)'
    # 区间 = emit_prologue 定义锚点 +0..+10 → v6.6.78 实际 343..353(11 行)
    seg "$ax" "$l_pro" 0 10 "$xf" '序言(上):END_BR 与 5 字节 nop 补丁位(蹦床/热补丁预留)'
    # 区间 = emit_prologue 定义锚点 +11..+29 → v6.6.78 实际 354..372(19 行)
    seg "$ax" "$l_pro" 11 29 "$xf" '序言(下):push rbp/搭栈帧(round_up(stack_depth,8))/X86_TAIL_CALL_OFFSET 跳板位'
    printf '职责:寄存器映射表是「BPF 虚拟机 ABI→x86 ABI」的翻译底座;序言负责栈帧布局并给 tail call 留好跳板位置。\n'
    return 0
}

# 站 4:指令 lowering 逐例(switch 分派)
# 校准(v6.6.78 实测):ALU MOV K=1206;STX MEM=1483;LDX MEM=1491;
# JMP JEQ X=1703(至 1727 共 25 行)
station_4() {
    local xf=arch/x86/net/bpf_jit_comp.c
    local ax="$TREE/$xf"
    local miss=0 l_alu l_stx l_ldx l_jmp
    printf '\n## 站 4:指令 lowering 逐例(switch 分派)\n'
    l_alu=$(locate "$ax" 'case BPF_ALU64 | BPF_MOV | BPF_K:' 1)
    l_stx=$(locate "$ax" 'case BPF_STX | BPF_MEM | BPF_B:' 1)
    l_ldx=$(locate "$ax" 'case BPF_LDX | BPF_MEM | BPF_B:' 1)
    l_jmp=$(locate "$ax" 'case BPF_JMP | BPF_JEQ | BPF_X:' 1)
    [[ -n $l_alu && -n $l_stx && -n $l_ldx && -n $l_jmp ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$xf" -F 'case BPF_ALU64 | BPF_MOV | BPF_K:'
    show_locate "$xf" -F 'case BPF_STX | BPF_MEM | BPF_B:'
    show_locate "$xf" -F 'case BPF_LDX | BPF_MEM | BPF_B:'
    show_locate "$xf" -F 'case BPF_JMP | BPF_JEQ | BPF_X:'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = ALU MOV K 锚点 +0..+9 → v6.6.78 实际 1206..1215(10 行)
    seg "$ax" "$l_alu" 0 9 "$xf" '例① 取数:MOV 立即数(lower 到 emit_mov_imm32)与 LD IMM64(emit_mov_imm64)'
    # 区间 = STX MEM 锚点 +0..+7 → v6.6.78 实际 1483..1490(8 行)
    seg "$ax" "$l_stx" 0 7 "$xf" '例② 存内存:STX 四种宽度统一 lower 到 emit_stx'
    # 区间 = LDX MEM 锚点 +0..+7 → v6.6.78 实际 1491..1498(8 行)
    seg "$ax" "$l_ldx" 0 7 "$xf" '例③ 取内存:LDX 的 MEM 与 PROBE_MEM 双形态(探针版带内核地址防护)'
    # 区间 = JMP JEQ X 锚点 +0..+24 → v6.6.78 实际 1703..1727(25 行)
    seg "$ax" "$l_jmp" 0 24 "$xf" '例④ 条件跳转:20 种 JMP/JMP32 比较共用 cmp + emit_cond_jmp 出口'
    printf '职责:do_jit 的大 switch 把每条 BPF 指令分派给对应的 x86 生成例程;四例覆盖取数/存/取/跳转四类基本动作。\n'
    return 0
}

# 站 5:helper 调用与 tail call
# 校准(v6.6.78 实测):bpf_jit_get_func_addr=core.c:1210(至 1244);
# emit_bpf_tail_call_indirect=comp.c:520;emit_bpf_tail_call_direct=comp.c:594;
# TAIL_CALL 分派=comp.c:1687
station_5() {
    local cf=kernel/bpf/core.c xf=arch/x86/net/bpf_jit_comp.c
    local ac="$TREE/$cf" ax="$TREE/$xf"
    local miss=0 l_func l_ind l_dir l_disp
    printf '\n## 站 5:helper 调用与 tail call\n'
    l_func=$(locate "$ac" 'int bpf_jit_get_func_addr(const struct bpf_prog *prog,' 1)
    l_ind=$(locate "$ax" 'static void emit_bpf_tail_call_indirect(u8 **pprog, bool *callee_regs_used,' 1)
    l_dir=$(locate "$ax" 'static void emit_bpf_tail_call_direct(struct bpf_jit_poke_descriptor *poke,' 1)
    l_disp=$(locate "$ax" 'case BPF_JMP | BPF_TAIL_CALL:' 1)
    [[ -n $l_func && -n $l_ind && -n $l_dir && -n $l_disp ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$cf" -F 'int bpf_jit_get_func_addr(const struct bpf_prog *prog,'
    show_locate "$xf" -F 'static void emit_bpf_tail_call_indirect(u8 **pprog, bool *callee_regs_used,'
    show_locate "$xf" -F 'static void emit_bpf_tail_call_direct(struct bpf_jit_poke_descriptor *poke,'
    show_locate "$xf" -F 'case BPF_JMP | BPF_TAIL_CALL:'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = get_func_addr 定义锚点 +0..+24 → v6.6.78 实际 1210..1234(25 行)
    seg "$ac" "$l_func" 0 24 "$cf" 'CALL 地址解析(上):PSEUDO_CALL 第一趟只给占位——extra_pass 才取真地址'
    # 区间 = get_func_addr 定义锚点 +25..+34 → v6.6.78 实际 1235..1244(10 行)
    seg "$ac" "$l_func" 25 34 "$cf" 'CALL 地址解析(下):helper 固定在 __bpf_call_base + imm(核心内核地址恒定)'
    # 区间 = indirect 定义锚点 +0..+24 → v6.6.78 实际 520..544(25 行)
    seg "$ax" "$l_ind" 0 24 "$xf" '间接尾调用:rdi/rsi/rdx 传参 + max_entries 越界检查(运行时查表)'
    # 区间 = direct 定义锚点 +0..+23 → v6.6.78 实际 594..617(24 行)
    seg "$ax" "$l_dir" 0 23 "$xf" '直接尾调用:MAX_TAIL_CALL_CNT 计数检查 + poke 描述符直跳目标'
    # 区间 = TAIL_CALL 分派锚点 +0..+13 → v6.6.78 实际 1687..1700(14 行)
    seg "$ax" "$l_disp" 0 13 "$xf" '分派点:imm32 非零走 direct(poke_tab),否则走 indirect(运行时查 bpf_array)'
    printf '职责:CALL 的目标地址第一趟占位、第二趟补真值(与站 2 呼应);tail call 有 33 层上限计数,direct/indirect 两种 lower 由 poke_tab 决定。\n'
    return 0
}

# 站 6:常量致盲与映像分配
# 校准(v6.6.78 实测):bpf_jit_blind_insn=core.c:1250;bpf_jit_blind_constants
# =1428;struct bpf_prog_pack=850;bpf_prog_pack_alloc=904(至 926)
station_6() {
    local cf=kernel/bpf/core.c
    local ac="$TREE/$cf"
    local miss=0 l_blind l_bc l_pack l_alloc
    printf '\n## 站 6:常量致盲与映像分配\n'
    l_blind=$(locate "$ac" 'static int bpf_jit_blind_insn(const struct bpf_insn *from,' 1)
    l_bc=$(locate "$ac" 'struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)' 1)
    l_pack=$(locate "$ac" 'struct bpf_prog_pack {' 1)
    l_alloc=$(locate "$ac" 'void *bpf_prog_pack_alloc(u32 size, bpf_jit_fill_hole_t bpf_fill_ill_insns)' 1)
    [[ -n $l_blind && -n $l_bc && -n $l_pack && -n $l_alloc ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$cf" -F 'static int bpf_jit_blind_insn(const struct bpf_insn *from,'
    show_locate "$cf" -F 'struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)'
    show_locate "$cf" -F 'struct bpf_prog_pack {'
    show_locate "$cf" -F 'void *bpf_prog_pack_alloc(u32 size, bpf_jit_fill_hole_t bpf_fill_ill_insns)'
    if [[ $miss -ne 0 ]]; then station_fail 6; return 1; fi
    # 区间 = blind_insn 定义锚点 +0..+24 → v6.6.78 实际 1250..1274(25 行)
    seg "$ac" "$l_blind" 0 24 "$cf" '单条致盲:AX 临时寄存器约束(用户不可见;致盲只发生在 JIT 侧)'
    # 区间 = blind_constants 定义锚点 +0..+24 → v6.6.78 实际 1428..1452(25 行)
    seg "$ac" "$l_bc" 0 24 "$cf" '遍历致盲:blinding_requested 门 + 克隆程序逐条改写(伪函数地址不随机)'
    # 区间 = pack 结构锚点 +0..+13 → v6.6.78 实际 850..863(14 行)
    seg "$ac" "$l_pack" 0 13 "$cf" 'bpf_prog_pack 结构:多程序共享大页 + 位图分块管理'
    # 区间 = pack_alloc 定义锚点 +0..+22 → v6.6.78 实际 904..926(23 行)
    seg "$ac" "$l_alloc" 0 22 "$cf" '打包分配:大块独占(ROX)/小块进 pack 位图——映像最终只读可执行'
    printf '职责:致盲把用户可控常量拆成随机异或对(缩跳板攻击面,与 bpf_jit_harden 联动);pack 把多个程序的映像集中成只读大页。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-jit-x86.sh —— x86_64 JIT 六站切片(第 6 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(core.c/bpf_jit_comp.c),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1
station_3 || rc=1
station_4 || rc=1
station_5 || rc=1
station_6 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=6\n' "$TREE_VERSION"
exit "$rc"

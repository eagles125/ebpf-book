#!/usr/bin/env bash
# trace-blind-station.sh —— 练习②参考实现:常量致盲两站切片(独立小脚本)
#
# 【脚本用途与输入】
# 对走读树的 kernel/bpf/core.c 切出常量致盲(constant blinding)的两段关键
# 代码,与正文二节站六呼应,接口与 02-jit-x86/trace-jit-x86.sh 同款:
#   - 站 1:bpf_jit_blind_insn(定义 core.c:1250)——签名头 + 改写示例区间
#     (AX 保护 / 零值特例 / ALU 立即数随机异或对,:1279-1300);
#   - 站 2:bpf_jit_blind_constants(定义 core.c:1428)——遍历入口(请求门、
#     克隆、循环改写)。
#   - 用法:bash trace-blind-station.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树,不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各段注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 两站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78 校准)
#     + sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注;
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间
#     (偏移按 v6.6.78 校准),树小幅漂移时区间随锚点自动平移;
#   - 末尾固定汇总:TREE_VERSION=v6.6.78 STATIONS=2;
#   - 退出码:两站全成 → 0;任一站提取失败 → 该站打印「STATION n: NOT FOUND」,
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
#   2. 锚点漂移:grep 定位为空(内核改名/移动/内联)—— 该站打印
#      「STATION n: NOT FOUND」;回树重新 grep 校准锚点与偏移后重跑;
#   3. 版本不符:换了树版本 —— 看 stderr 的 WARN;行号漂移属预期,以 v6.6.78
#      实测为准。
set -u

TREE_VERSION="v6.6.78"
DEFAULT_TREE="/work/linux/linux-6.6.78"

# ---------- 入参与依赖 ----------

if [[ $# -gt 1 ]]; then
    printf '用法:bash %s [WALK_TREE](默认 %s)\n' "$0" "$DEFAULT_TREE" >&2
    exit 1
fi
WALK_TREE="${1:-${WALK_TREE:-$DEFAULT_TREE}}"
TREE="$WALK_TREE"

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
if [[ ! -f $WALK_TREE/kernel/bpf/core.c ]]; then
    printf '树内关键文件缺失:kernel/bpf/core.c(WALK_TREE 指错目录或树版本不符?)\n' >&2
    exit 1
fi

# 版本软校验:Makefile 在场但版本不是 6.6.78 时警告(stderr),不中断
if [[ -r $WALK_TREE/Makefile ]]; then
    ver=$(awk '/^VERSION = /{v=$3} /^PATCHLEVEL = /{p=$3} /^SUBLEVEL = /{s=$3} \
        END{print v"."p"."s}' "$WALK_TREE/Makefile")
    if [[ $ver != 6.6.78 ]]; then
        printf 'WARN: 树版本 %s 与书稿锁定版本 %s 不符,行号仅供参考\n' \
            "$ver" "$TREE_VERSION" >&2
    fi
fi

# ---------- 通用小工具(与 02-jit-x86 同款) ----------

locate() { # $1 绝对文件 $2 模式 $3 第几个命中
    grep -nF -- "$2" "$1" | sed -n "${3}p" | cut -d: -f1
}

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

# ---------- 两站 ----------

# 站 1:bpf_jit_blind_insn——单条改写
# 校准(v6.6.78 实测):定义=core.c:1250;改写示例区=1279..1300(锚点 +29..+50)
station_1() {
    local f=kernel/bpf/core.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 1:bpf_jit_blind_insn——单条改写\n'
    l_def=$(locate "$af" 'static int bpf_jit_blind_insn(const struct bpf_insn *from,' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int bpf_jit_blind_insn(const struct bpf_insn *from,'
    show_locate "$f" -F 'if (from->dst_reg == BPF_REG_AX || from->src_reg == BPF_REG_AX)'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 定义锚点 +0..+9 → v6.6.78 实际 1250..1259(10 行)
    seg "$af" "$l_def" 0 9 "$f" '签名与局部量:from/aux/to_buff 三份指令与随机数 imm_rnd'
    # 区间 = 定义锚点 +29..+50 → v6.6.78 实际 1279..1300(22 行)
    seg "$af" "$l_def" 29 50 "$f" '改写示例:AX 保护 → 零值特例转 xor → ALU 族随机异或对(mov AX, imm_rnd^imm; xor AX, imm_rnd; op dst, AX)'
    printf '职责:单条指令的致盲改写——AX(用户不可见的临时寄存器)被占用则跳过;零值 MOV 直接改 xor;其余立即数拆成「随机异或对」,明文常量从此不在指令流里(与正文二节站六呼应)。\n'
    return 0
}

# 站 2:bpf_jit_blind_constants——遍历入口
# 校准(v6.6.78 实测):定义=core.c:1428(门/克隆/循环头至 1452)
station_2() {
    local f=kernel/bpf/core.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 2:bpf_jit_blind_constants——遍历入口\n'
    l_def=$(locate "$af" 'struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = 定义锚点 +0..+24 → v6.6.78 实际 1428..1452(25 行)
    seg "$af" "$l_def" 0 24 "$f" '遍历入口:blinding_requested 门(未请求/已致盲原样返回)→ 克隆程序 → 逐条改写循环'
    printf '职责:致盲流程的调度台——原程序不动,克隆上改写;克隆失败返回 ERR_PTR(-ENOMEM),由 JIT 入口(core.c:2763 一带)的 IS_ERR 接住回退解释器。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-blind-station.sh —— 常量致盲两站切片(练习②参考实现)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(kernel/bpf/core.c),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=2\n' "$TREE_VERSION"
exit "$rc"

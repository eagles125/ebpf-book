#!/usr/bin/env bash
# trace-helpers.sh —— 第 7 章《辅助函数与映射的内部实现》helper 分发五站切片器
#
# 【脚本用途与输入】
# 沿「call 指令→基址→公共兜底→验证期解析→类型白名单→原型结构」的 helper 分发
# 路径,在 eBPF 走读源码树上按「站」切出五段关键代码,供正文《一、辅助函数:
# 从 call 指令到白名单》逐站引用其行号与代码段(本输出的行号即引用事实)。
#   - 用法:bash trace-helpers.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/core.c、kernel/bpf/helpers.c、
#     kernel/bpf/verifier.c、kernel/trace/bpf_trace.c、include/linux/bpf.h,
#     不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各段注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 五站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78 校准)
#     + sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注;
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间
#     (偏移按 v6.6.78 校准),树小幅漂移时区间随锚点自动平移;
#   - 末尾固定汇总:TREE_VERSION=v6.6.78 STATIONS=5
#     (TREE_VERSION 是书稿锁定版本常量,非自动探测);
#   - 退出码:五站全成 → 0;任一站提取失败 → 该站打印「STATION n: NOT FOUND」,
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
for rel in kernel/bpf/core.c kernel/bpf/helpers.c kernel/bpf/verifier.c \
           kernel/trace/bpf_trace.c include/linux/bpf.h; do
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

# ---------- 五站 ----------

# 站 1:基址与解释器出口
# 校准(v6.6.78 实测):__bpf_call_base 定义=core.c:1500;解释器 CALL_ARGS
# 标签=1975(调用行 1976);bpf_patch_call_args=2240
station_1() {
    local f=kernel/bpf/core.c
    local af="$TREE/$f"
    local miss=0 l_base l_jmp l_patch
    printf '\n## 站 1:基址与解释器出口(__bpf_call_base)\n'
    l_base=$(locate "$af" 'noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)' 1)
    l_jmp=$(locate "$af" 'JMP_CALL_ARGS:' 1)
    l_patch=$(locate "$af" 'void bpf_patch_call_args(struct bpf_insn *insn, u32 stack_depth)' 1)
    [[ -n $l_base && -n $l_jmp && -n $l_patch ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)'
    show_locate "$f" -F 'BPF_R0 = (__bpf_call_base_args + insn->imm)(BPF_R1, BPF_R2,'
    show_locate "$f" -F 'void bpf_patch_call_args(struct bpf_insn *insn, u32 stack_depth)'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 基址定义锚点 +0..+5 → v6.6.78 实际 1500..1505(6 行)
    seg "$af" "$l_base" 0 5 "$f" '__bpf_call_base:恒返回 0 的兜底——helper imm 相对它寻址,「固定地址」的锚'
    # 区间 = JMP_CALL_ARGS 标签锚点 +0..+5 → v6.6.78 实际 1975..1980(6 行)
    seg "$af" "$l_jmp" 0 5 "$f" '解释器带参出口:BPF_R0 = (__bpf_call_base_args + imm)(r1..r5, next_insn)'
    # 区间 = patch_call_args 锚点 +0..+8 → v6.6.78 实际 2240..2248(9 行)
    seg "$af" "$l_patch" 0 8 "$f" 'bpf_patch_call_args:把带栈长的解释器入口折算成相对 __bpf_call_base_args 的 imm'
    printf '职责:所有 helper 的地址都以 __bpf_call_base 为基址相对计算——JIT 的 call imm(第 6 章站五 get_func_addr)与解释器的出口在这里会合。\n'
    return 0
}

# 站 2:公共兜底白名单
# 校准(v6.6.78 实测):bpf_base_func_proto 定义=helpers.c:1757
station_2() {
    local f=kernel/bpf/helpers.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 2:公共兜底白名单 bpf_base_func_proto\n'
    l_def=$(locate "$af" 'bpf_base_func_proto(enum bpf_func_id func_id)' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_base_func_proto(enum bpf_func_id func_id)'
    show_locate "$f" -F 'const struct bpf_func_proto bpf_map_lookup_elem_proto = {'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = 定义锚点 +0..+23 → v6.6.78 实际 1757..1780(24 行)
    seg "$af" "$l_def" 0 23 "$f" 'switch(func_id) 返回各族通用 proto:映射五件套/随机数/CPU 号/tail call…'
    printf '职责:所有程序类型共享的「最低白名单」——映射操作与无害信息读取;类型专属 helper 由各 ops 的 get_func_proto 在它之上追加。\n'
    return 0
}

# 站 3:验证期解析链与失败出口
# 校准(v6.6.78 实测):get_func_proto 调用=verifier.c:9965-9966(unknown 出口 9968 起)
station_3() {
    local f=kernel/bpf/verifier.c
    local af="$TREE/$f"
    local miss=0 l_call
    printf '\n## 站 3:验证期解析链 check_helper_call\n'
    l_call=$(locate "$af" 'if (env->ops->get_func_proto)' 1)
    [[ -n $l_call ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'if (env->ops->get_func_proto)'
    show_locate "$f" -F 'fn = env->ops->get_func_proto(func_id, env->prog)'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = 调用锚点 -5..+10 → v6.6.78 实际 9960..9975(16 行)
    seg "$af" "$l_call" -5 10 "$f" '解析链:ops->get_func_proto 查 proto;查不到 unknown func -EINVAL;GPL 检查紧随'
    printf '职责:白名单机制的中枢——「这个程序类型能不能用这个 helper」由 env->ops->get_func_proto 一次性回答;失败即加载终止(第 3 章失败画廊的出处之一)。\n'
    return 0
}

# 站 4:类型白名单实例(tracing 族)
# 校准(v6.6.78 实测):tracing_prog_func_proto 定义=bpf_trace.c:1948
station_4() {
    local f=kernel/trace/bpf_trace.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 4:类型白名单实例 tracing_prog_func_proto\n'
    l_def=$(locate "$af" 'tracing_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$af" -F 'tracing_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)'
    show_locate "$af" -F '.get_func_proto  = tracing_prog_func_proto'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = 定义锚点 +0..+22 → v6.6.78 实际 1948..1970(23 行)
    seg "$af" "$l_def" 0 22 "$f" 'tracing 族白名单:先按 prog 类型过滤(tp_btf/fentry 走不同分支),再叠 CONFIG 开关'
    printf '职责:白名单「随程序类型滚动」的实证——同一个 helper id 在 kprobe/tracing/xdp 各族的可见性由各自的 get_func_proto 决定(第 4 章「helper 面跟着执行现场走」的机制出处)。\n'
    return 0
}

# 站 5:原型结构 bpf_func_proto
# 校准(v6.6.78 实测):struct bpf_func_proto=include/linux/bpf.h:797(至 812)
station_5() {
    local f=include/linux/bpf.h
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 5:原型结构 struct bpf_func_proto\n'
    l_def=$(locate "$af" 'struct bpf_func_proto {' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'struct bpf_func_proto {'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = 定义锚点 +0..+15 → v6.6.78 实际 797..812(16 行)
    seg "$af" "$l_def" 0 15 "$f" '五要素:func 指针/gpl_only/pkt_access/might_sleep + 返回类型 + 五参数类型'
    printf '职责:helper 的「说明书」——验证器按 arg*_type 逐参检查(第 3 章参数校验的依据),pkt_access 决定能否直接摸包,might_sleep 决定能否在原子上下文调用。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-helpers.sh —— helper 分发五站切片(第 7 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(core.c/helpers.c/verifier.c/bpf_trace.c/bpf.h),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1
station_3 || rc=1
station_4 || rc=1
station_5 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=5\n' "$TREE_VERSION"
exit "$rc"

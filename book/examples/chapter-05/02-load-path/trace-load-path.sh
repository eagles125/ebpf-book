#!/usr/bin/env bash
# trace-load-path.sh —— 第 5 章《eBPF 子系统内核代码结构》加载路径切片器
#
# 【脚本用途与输入】
# 沿 BPF_PROG_LOAD 的加载路径,在 eBPF 走读源码树上按「站」切出六段关键代码,
# 供正文《二、加载路径》逐站引用其行号与代码段(本输出的行号即引用事实)。
#   - 用法:bash trace-load-path.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/syscall.c、core.c、verifier.c,
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
#   - 依赖:bash、grep(-F/-E/-n 定位)、sed(-n 'A,Bp')、awk(前缀剥离与版本
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
for rel in kernel/bpf/syscall.c kernel/bpf/core.c kernel/bpf/verifier.c; do
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

# 站 1:系统调用入口与分发
# 校准(v6.6.78 实测):SYSCALL_DEFINE3(bpf,=5567;__sys_bpf=5426;
# case BPF_PROG_LOAD:=5464(第二处 5592 在 BPF_CALL_3(bpf_sys_bpf) 白名单里)
station_1() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_entry l_sys l_case
    printf '\n## 站 1:系统调用入口与分发\n'
    l_entry=$(locate "$af" 'SYSCALL_DEFINE3(bpf,' 1)
    l_sys=$(locate "$af" '__sys_bpf' 1)
    l_case=$(locate "$af" 'case BPF_PROG_LOAD:' 1)
    [[ -n $l_entry && -n $l_sys && -n $l_case ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'SYSCALL_DEFINE3(bpf,'
    show_locate "$f" -F '__sys_bpf'
    show_locate "$f" -F 'case BPF_PROG_LOAD:'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = __sys_bpf 锚点 +0..+17 → v6.6.78 实际 5426..5443(18 行)
    seg "$af" "$l_sys" 0 17 "$f" '__sys_bpf:uarg 尾零检查、union bpf_attr 拷贝与 security_bpf 钩子'
    # 区间 = case BPF_PROG_LOAD 锚点 -19..+2 → v6.6.78 实际 5445..5466(22 行)
    seg "$af" "$l_case" -19 2 "$f" 'switch(cmd) 命令分发:BPF_PROG_LOAD 分支转入 bpf_prog_load()'
    # 区间 = SYSCALL_DEFINE3 锚点 +0..+3 → v6.6.78 实际 5567..5570(4 行)
    seg "$af" "$l_entry" 0 3 "$f" 'SYSCALL_DEFINE3(bpf):系统调用入口,USER_BPFPTR 包一层转 __sys_bpf'
    printf '职责:接收 bpf(2) 系统调用并按 cmd 分发,BPF_PROG_LOAD 分支由此进入加载路径。\n'
    return 0
}

# 站 2:bpf_prog_load 签名与属性处理
# 校准(v6.6.78 实测):bpf_prog_load( 定义=2586、分发调用=5465;
# fixup_attach_type=2413/2662;check_attach=2433/2663
station_2() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 2:bpf_prog_load 签名与属性处理\n'
    l_def=$(locate "$af" 'bpf_prog_load(' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_prog_load('
    show_locate "$f" -F 'bpf_prog_load_fixup_attach_type('
    show_locate "$f" -F 'bpf_prog_load_check_attach('
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = bpf_prog_load( 定义锚点 +0..+23 → v6.6.78 实际 2586..2609(24 行)
    seg "$af" "$l_def" 0 23 "$f" 'bpf_prog_load 头部:签名、CHECK_ATTR 边界校验与 prog_flags 合法性'
    printf '职责:加载路径业务入口——校验用户属性并搭起 struct bpf_prog,后续各站都在它的函数体内。\n'
    return 0
}

# 站 3:验证调用点
# 校准(v6.6.78 实测):bpf_check(&prog 调用点=syscall.c:2743;
# 验证器定义=verifier.c:20222
station_3() {
    local f=kernel/bpf/syscall.c vf=kernel/bpf/verifier.c
    local af="$TREE/$f"
    local miss=0 l_call
    printf '\n## 站 3:验证调用点 bpf_check\n'
    l_call=$(locate "$af" 'bpf_check(&prog' 1)
    [[ -n $l_call ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_check(&prog'
    show_locate "$vf" -F 'int bpf_check(struct bpf_prog **prog'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = bpf_check(&prog 锚点 -7..+6 → v6.6.78 实际 2736..2749(14 行)
    seg "$af" "$l_call" -7 6 "$f" 'bpf_check(&prog,...):验证器调用点,失败即 goto free_used_maps;下一行即 JIT 选择'
    printf '职责:验证器入口的唯一调用点——静态安全检查在这里发生,验证失败加载即终止。\n'
    return 0
}

# 站 4:JIT 选择
# 校准(v6.6.78 实测):调用点=syscall.c:2747;core.c 定义头=2357(文档=2347、
# EXPORT=2408);内部 bpf_int_jit_compile 调用=2384;__weak 回退桩=2884
station_4() {
    local f=kernel/bpf/syscall.c cf=kernel/bpf/core.c
    local af="$TREE/$f" ac="$TREE/$cf"
    local miss=0 l_call l_def l_jit l_weak
    printf '\n## 站 4:JIT 选择 bpf_prog_select_runtime\n'
    l_call=$(locate "$af" 'bpf_prog_select_runtime' 1)
    l_def=$(locate "$ac" 'bpf_prog_select_runtime' 2)   # 第 2 命中=定义行
    l_jit=$(locate "$ac" 'fp = bpf_int_jit_compile(fp);' 1)
    l_weak=$(locate "$ac" '__weak bpf_int_jit_compile' 1)
    [[ -n $l_call && -n $l_def && -n $l_jit && -n $l_weak ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_prog_select_runtime'
    show_locate "$cf" -F 'bpf_prog_select_runtime'
    show_locate "$cf" -F 'fp = bpf_int_jit_compile(fp);'
    show_locate "$cf" -F '__weak bpf_int_jit_compile'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = 调用点锚点 +0..+2 → v6.6.78 实际 2747..2749(3 行)
    seg "$af" "$l_call" 0 2 "$f" '调用点:验证通过后立即选定执行引擎'
    # 区间 = core.c 定义锚点 -11..+12 → v6.6.78 实际 2346..2369(24 行)
    seg "$ac" "$l_def" -11 12 "$cf" 'core.c 定义头:文档注释、签名与 jit_needed 判定(JIT_ALWAYS_ON/kfunc)'
    # 区间 = bpf_int_jit_compile 调用锚点 +0..+5 → v6.6.78 实际 2384..2389(6 行)
    seg "$ac" "$l_jit" 0 5 "$cf" '内部:调架构 JIT bpf_int_jit_compile;未 JIT 且必须 JIT → -ENOTSUPP'
    # 区间 = __weak 桩锚点 -4..+3 → v6.6.78 实际 2880..2887(8 行)
    seg "$ac" "$l_weak" -4 3 "$cf" '__weak 回退桩:无架构 JIT 时原样返回,程序走解释器'
    printf '职责:验证通过后选定执行引擎——优先 JIT,无 JIT 回退解释器,并把程序内存锁定为只读。\n'
    return 0
}

# 站 5:fd 分配
# 校准(v6.6.78 实测):prog_idr 声明=53;idr_alloc_cyclic=2105(bpf_prog_alloc_id
# 定义=2099、调用=2751);bpf_prog_new_fd( 定义=2293、load 内调用=2773、
# PROG_GET_FD_BY_ID=4228
station_5() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_idr l_alloc l_fd l_callfd
    printf '\n## 站 5:fd 分配 bpf_prog_new_fd 与 prog_idr\n'
    l_idr=$(locate "$af" 'static DEFINE_IDR(prog_idr);' 1)
    l_alloc=$(locate "$af" 'bpf_prog_alloc_id(' 1)
    l_fd=$(locate "$af" 'bpf_prog_new_fd(' 1)
    l_callfd=$(locate "$af" 'err = bpf_prog_new_fd(prog);' 1)
    [[ -n $l_idr && -n $l_alloc && -n $l_fd && -n $l_callfd ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static DEFINE_IDR(prog_idr);'
    show_locate "$f" -F 'bpf_prog_alloc_id('
    show_locate "$f" -F 'bpf_prog_new_fd('
    show_locate "$f" -F 'err = bpf_prog_new_fd(prog);'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = prog_idr 声明锚点 -1..+1 → v6.6.78 实际 52..54(3 行)
    seg "$af" "$l_idr" -1 1 "$f" 'prog_idr:程序全局 id 的 IDR 与自旋锁声明'
    # 区间 = bpf_prog_alloc_id( 锚点 +0..+17 → v6.6.78 实际 2099..2116(18 行)
    seg "$af" "$l_alloc" 0 17 "$f" 'bpf_prog_alloc_id:idr_alloc_cyclic 发号,写入 prog->aux->id'
    # 区间 = bpf_prog_new_fd( 定义锚点 +0..+10 → v6.6.78 实际 2293..2303(11 行)
    seg "$af" "$l_fd" 0 10 "$f" 'bpf_prog_new_fd:security_bpf_prog 钩子 + anon_inode_getfd 发放 fd'
    # 区间 = load 内调用锚点 -4..+3 → v6.6.78 实际 2769..2776(8 行)
    seg "$af" "$l_callfd" -4 3 "$f" 'bpf_prog_load 尾声:kallsyms/perf/audit 通告后发 fd;失败须 bpf_prog_put'
    printf '职责:程序先领全局 id、再领 fd(anon_inode),自此对用户态可见;fd 失败要走释放链。\n'
    return 0
}

# 站 6:释放链
# 校准(v6.6.78 实测):__bpf_prog_put_rcu=2136;__bpf_prog_put_noref=2147;
# bpf_prog_put_deferred=2168;__bpf_prog_put=2181;bpf_prog_put=2195;
# bpf_prog_release(fd close 入口)=2201
station_6() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_put l_deferred l_noref l_rel
    printf '\n## 站 6:释放链 __bpf_prog_put / bpf_prog_put\n'
    l_put=$(locate "$af" 'static void __bpf_prog_put(struct bpf_prog *prog)' 1)
    l_deferred=$(locate "$af" 'static void bpf_prog_put_deferred(struct work_struct *work)' 1)
    l_noref=$(locate "$af" 'static void __bpf_prog_put_noref(struct bpf_prog *prog, bool deferred)' 1)
    l_rel=$(locate "$af" 'static int bpf_prog_release(struct inode *inode, struct file *filp)' 1)
    [[ -n $l_put && -n $l_deferred && -n $l_noref && -n $l_rel ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -E '^static void (__bpf_prog_put_rcu|__bpf_prog_put_noref|bpf_prog_put_deferred|__bpf_prog_put)\(|^void bpf_prog_put\(|^static int bpf_prog_release\('
    if [[ $miss -ne 0 ]]; then station_fail 6; return 1; fi
    # 区间 = __bpf_prog_put 锚点 +0..+18 → v6.6.78 实际 2181..2199(19 行)
    seg "$af" "$l_put" 0 18 "$f" 'bpf_prog_put(导出接口)→ __bpf_prog_put:引用计数归零后转 deferred'
    # 区间 = bpf_prog_put_deferred 锚点 +0..+11 → v6.6.78 实际 2168..2179(12 行)
    seg "$af" "$l_deferred" 0 11 "$f" 'bpf_prog_put_deferred:unload 通告、free_id、转 __bpf_prog_put_noref'
    # 区间 = __bpf_prog_put_noref 锚点 +0..+19 → v6.6.78 实际 2147..2166(20 行)
    seg "$af" "$l_noref" 0 19 "$f" '__bpf_prog_put_noref:注销 kallsyms/BTF/模块,再按 sleepable 走 RCU 回收'
    # 区间 = bpf_prog_release 锚点 +0..+6 → v6.6.78 实际 2201..2207(7 行)
    seg "$af" "$l_rel" 0 6 "$f" 'bpf_prog_release:fd 关闭(->release)即 bpf_prog_put,释放链的常态入口'
    printf '职责:引用计数归零后的分级释放——put → deferred → noref → RCU;fd 关闭即触发 put。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-load-path.sh —— BPF_PROG_LOAD 加载路径六站切片(第 5 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(syscall.c/core.c/verifier.c),不构建、不加载,对源码树零写入。\n'
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

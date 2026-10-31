#!/usr/bin/env bash
# trace-btf-load.sh —— 练习②参考实现:BPF_BTF_LOAD 三站切片器
#
# 【脚本用途与输入】
# 沿 BPF_BTF_LOAD(加载一份 BTF 对象)命令链,在 eBPF 走读源码树上按「站」
# 切出三段关键代码。本脚本是练习②(给 trace-map-ops.sh 新增 BPF_BTF_LOAD
# 站)的参考实现,与 ../../03-map-ops/trace-map-ops.sh 同构——每站就是
# 「锚点模式 + sed 区间 + 职责注」三件套,可整站并入映射链切片器作第七站。
#   - 用法:bash trace-btf-load.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/syscall.c、kernel/bpf/btf.c,
#     不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各站注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 三站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78
#     校准)+ sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注;
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间
#     (偏移按 v6.6.78 校准),树小幅漂移时区间随锚点自动平移;
#   - 末尾固定汇总:TREE_VERSION=v6.6.78 STATIONS=3
#     (TREE_VERSION 是书稿锁定版本常量,非自动探测);
#   - 退出码:三站全成 → 0;任一站提取失败 → 该站打印「STATION n: NOT
#     FOUND」,其余站继续,退出码 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F/-E/-n 定位)、sed(-n 'A,Bp')、awk(前缀剥离与
#     版本软校验),无其他外部命令;
#   - 依赖缺失:启动即报错退出(1),绝不产出「假切片」;
#   - 树目录或关键文件缺失:启动即报错退出(1),不逐站刷 NOT FOUND;
#   - 树版本 ≠ 6.6.78:stderr 打印 WARN 后继续(行号仅供参考,以该树
#     实测为准)。
#
# 【失败模式】
#   1. 树不在:WALK_TREE 指错目录或源码未解压 —— 启动报错,退出码 1;
#      处理:把 WALK_TREE 指向正确的 6.6.78 树后重跑;
#   2. 锚点漂移:树在,但 grep 定位为空(内核改名/移动/内联符号)—— 该站
#      打印「STATION n: NOT FOUND」,退出码 1;
#      处理:回到树里 grep 新位置,改本脚本该站的锚点模式与偏移(同步更新
#      校准注释),再重跑;
#   3. 版本不符:换了树版本 —— 看 stderr 的 WARN;行号漂移属预期,换树
#      实测行为记录见本目录 README 失败模式表补条。
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
for rel in kernel/bpf/syscall.c kernel/bpf/btf.c; do
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

# ---------- 三站 ----------

# 站 1:入口分发 case BPF_BTF_LOAD
# 校准(v6.6.78 实测):case BPF_BTF_LOAD=5509(第二处 5593 在
# BPF_CALL_3(bpf_sys_bpf) 白名单里,切片取第一处正身)
station_1() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_case
    printf '\n## 站 1:入口分发 case BPF_BTF_LOAD\n'
    l_case=$(locate "$af" 'case BPF_BTF_LOAD:' 1)
    [[ -n $l_case ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'case BPF_BTF_LOAD:'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = case BPF_BTF_LOAD 锚点 -1..+3 → v6.6.78 实际 5508..5513(6 行)
    seg "$af" "$l_case" -1 3 "$f" 'switch(cmd) 分发:BPF_BTF_LOAD 与 BPF_MAP_CREATE 同一条 switch 的平级命令,转入 bpf_btf_load()'
    printf '职责:命令入口——BPF_BTF_LOAD 挂在 __sys_bpf 的 switch(cmd) 里(v6.6.78 上 :5445 起),与映射命令平级;定位块若出现第二处命中,那是 BPF 程序经 bpf_sys_bpf 调用的白名单(v6.6.78 上为 :5593),非 bpf(2) 正身。\n'
    return 0
}

# 站 2:命令实体 bpf_btf_load
# 校准(v6.6.78 实测):bpf_btf_load( 定义=4808
station_2() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_def
    printf '\n## 站 2:命令实体 bpf_btf_load\n'
    l_def=$(locate "$af" 'static int bpf_btf_load(const union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)' 1)
    [[ -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int bpf_btf_load(const union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = bpf_btf_load( 定义锚点 +0..+9 → v6.6.78 实际 4808..4817(10 行)
    seg "$af" "$l_def" 0 9 "$f" 'bpf_btf_load 全函数:CHECK_ATTR 边界 → bpf_capable 特权闸(-EPERM)→ btf_new_fd 交棒 btf.c'
    printf '职责:BTF 加载的命令实体——与映射命令的三层分工不同,它只有三行:边界检查、特权检查(bpf_capable,1.3 的权限开关)、然后把全部解析工作整包交给 btf.c 的 btf_new_fd。\n'
    return 0
}

# 站 3:跨文件边界 btf_new_fd → btf_parse(kernel/bpf/btf.c,1.1 区 5)
# 校准(v6.6.78 实测,btf.c):btf_new_fd=7183;btf_parse 调用=7188;
# btf_parse( 定义=5474(btf_parse_ 前缀同名一族:kptr/graph_root/
# list_head/rb_root/fields/type_sec/str_sec/hdr,锚点须带完整形态防撞车)
station_3() {
    local f=kernel/bpf/btf.c
    local af="$TREE/$f"
    local miss=0 l_new l_call l_def
    printf '\n## 站 3:跨文件边界 btf_new_fd → btf_parse(btf.c)\n'
    l_new=$(locate "$af" 'int btf_new_fd(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)' 1)
    l_call=$(locate "$af" 'btf = btf_parse(attr, uattr, uattr_size);' 1)
    l_def=$(locate "$af" 'static struct btf *btf_parse(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)' 1)
    [[ -n $l_new && -n $l_call && -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'int btf_new_fd(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)'
    show_locate "$f" -F 'btf = btf_parse(attr, uattr, uattr_size);'
    show_locate "$f" -F 'static struct btf *btf_parse(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = btf_new_fd 定义锚点 +0..+9 → v6.6.78 实际 7183..7192(10 行)
    seg "$af" "$l_new" 0 9 "$f" 'btf_new_fd 头部:syscall.c 的命令实体在此跨入 btf.c——btf_parse 调用(:7188)就是跨文件边界'
    # 区间 = btf_parse( 定义锚点 +0..+11 → v6.6.78 实际 5474..5485(12 行)
    seg "$af" "$l_def" 0 11 "$f" 'btf_parse 头部:尺寸上限(BTF_MAX_SIZE,-E2BIG)后建验证器环境,解析头/串/类型三节'
    printf '职责:边界站——btf_new_fd 是 btf.c 对 syscall.c 的接门人,调 btf_parse 解析 BTF 二进制后领编号(btf_alloc_id);1.1 区 5 的入口由此站标注。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-btf-load.sh —— BPF_BTF_LOAD 三站切片(第 5 章练习②参考实现)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(syscall.c/btf.c),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1
station_3 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=3\n' "$TREE_VERSION"
exit "$rc"

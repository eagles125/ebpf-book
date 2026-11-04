#!/usr/bin/env bash
# trace-kernel-relo.sh —— 第 8 章《BTF 与 CO-RE 原理》内核态重定位引擎五站切片器
#
# 【脚本用途与输入】
# 沿「引擎入口→候选查找与兼容判定→verifier 接线→UAPI 载体→轻骨架提交」的路径,
# 在 eBPF 走读源码树上按「站」切出五段关键代码,供正文《四、两台重定位引擎》
# 与《五、实战五层:一条 BPF_CORE_READ 的完整旅程》逐站引用其行号与代码段。
# 04 号示例(../04-relo-slicer)从编译产物里切出「重定位记录」(relocation
# record:记下哪条指令的立即数要按目标内核改写、记哪个类型哪个字段、记什么量);
# 本切片器补上另一半——这些记录进入内核后,由谁接手、按什么规则改写指令。
#   - 用法:bash trace-kernel-relo.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/btf.c、kernel/bpf/verifier.c、
#     include/uapi/linux/bpf.h、tools/lib/bpf/gen_loader.c,不构建、不加载,
#     对源码树零写入;结果全部输出到 stdout;
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
#   - 依赖:bash、grep(-F/-n 定位)、sed(-n 'A,Bp')、awk(树版本软校验),无其他外部命令;
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
for rel in kernel/bpf/btf.c kernel/bpf/verifier.c include/uapi/linux/bpf.h tools/lib/bpf/gen_loader.c; do
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

# 站 1:内核引擎入口
# 校准(v6.6.78 实测):bpf_core_apply=btf.c:8423(函数体至 8499);
# 函数内调 bpf_core_find_cands 的调用点=8453;算结果 bpf_core_calc_relo_insn=8482
station_1() {
    local f=kernel/bpf/btf.c
    local af="$TREE/$f"
    local miss=0 l_apply l_calc
    printf '\n## 站 1:内核引擎入口 bpf_core_apply\n'
    l_apply=$(locate "$af" 'int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,' 1)
    l_calc=$(locate "$af" 'err = bpf_core_calc_relo_insn((void *)ctx->log,' 1)
    [[ -n $l_apply && -n $l_calc ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,'
    show_locate "$f" -F 'cc = bpf_core_find_cands(ctx, relo->type_id);'
    show_locate "$f" -F 'err = bpf_core_calc_relo_insn((void *)ctx->log,'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 入口锚点 +0..+23 → v6.6.78 实际 8423..8446(24 行)
    seg "$af" "$l_apply" 0 23 "$f" '入口头部:除 TYPE_ID_LOCAL 外都要候选→按 3 份 bpf_core_spec 开 ~4k 工作区→先校验记录里的类型 ID'
    # 区间 = 算结果锚点 +0..+17 → v6.6.78 实际 8482..8499(18 行)
    seg "$af" "$l_calc" 0 17 "$f" '收尾三步:bpf_core_calc_relo_insn 算目标值→bpf_core_patch_insn 改写指令立即数→释放工作区并解锁候选缓存'
    printf '职责:内核态 CO-RE 引擎的总入口——一条重定位记录在此「查候选→算结果→改指令」三步走完;从查候选到改指令全程持 cand_cache_mutex,防模块 BTF 半路卸载。\n'
    return 0
}

# 站 2:候选查找与兼容判定
# 校准(v6.6.78 实测):bpf_core_find_cands=btf.c:8336(头部至 8360);
# bpf_core_types_are_compat=btf.c:8094(至 8108,含 bpf_core_types_match);
# MAX_TYPES_ARE_COMPAT_DEPTH=2(:8073)
station_2() {
    local f=kernel/bpf/btf.c
    local af="$TREE/$f"
    local miss=0 l_cands l_compat
    printf '\n## 站 2:候选查找 bpf_core_find_cands 与兼容判定 bpf_core_types_are_compat\n'
    l_cands=$(locate "$af" 'bpf_core_find_cands(struct bpf_core_ctx *ctx, u32 local_type_id)' 1)
    l_compat=$(locate "$af" 'int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,' 1)
    [[ -n $l_cands && -n $l_compat ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_core_find_cands(struct bpf_core_ctx *ctx, u32 local_type_id)'
    show_locate "$f" -F 'int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = 候选锚点 +0..+24 → v6.6.78 实际 8336..8360(25 行)
    seg "$af" "$l_cands" 0 24 "$f" '候选头部:目标=内核 vmlinux BTF→取本机类型名→按「本质名长度」剥掉 ___flavor 后缀再拿去匹配'
    # 区间 = 兼容锚点 +0..+14 → v6.6.78 实际 8094..8108(15 行)
    seg "$af" "$l_compat" 0 14 "$f" '两个递归比对包装(深度上限均为 2):compat 供候选判定逐成员同布局比对,match 是按名匹配的宽口径——真正比对在实现侧的双下划线函数,两台引擎各自维护、接口对齐 uapi'
    printf '职责:目标类型的来路——按本机类型名在 vmlinux/模块 BTF 里找同名候选;「同一类型」靠递归逐成员比对认定(名称里的 ___flavor 后缀不参与),候选数组随后交给站 1 的计算器。\n'
    return 0
}

# 站 3:verifier 接线
# 校准(v6.6.78 实测):check_core_relo=verifier.c:15718(头部至 15747);
# 递给引擎的调用点 err = bpf_core_apply=15778;check_btf_info 内接线点=15817
station_3() {
    local f=kernel/bpf/verifier.c
    local af="$TREE/$f"
    local miss=0 l_check l_rec l_apply l_wire
    printf '\n## 站 3:verifier 接线 check_core_relo\n'
    l_check=$(locate "$af" 'static int check_core_relo(struct bpf_verifier_env *env,' 1)
    l_rec=$(locate "$af" 'rec_size = attr->core_relo_rec_size;' 1)
    l_apply=$(locate "$af" 'err = bpf_core_apply(&ctx, &core_relo, i,' 1)
    l_wire=$(locate "$af" 'err = check_core_relo(env, attr, uattr);' 1)
    [[ -n $l_check && -n $l_rec && -n $l_apply && -n $l_wire ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int check_core_relo(struct bpf_verifier_env *env,'
    show_locate "$f" -F 'rec_size = attr->core_relo_rec_size;'
    show_locate "$f" -F 'err = bpf_core_apply(&ctx, &core_relo, i,'
    show_locate "$f" -F 'err = check_core_relo(env, attr, uattr);'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = 接线锚点 +0..+19 → v6.6.78 实际 15718..15737(20 行)
    seg "$af" "$l_check" 0 19 "$f" '接线头部:从 prog->aux->btf 组 bpf_core_ctx(verifier 日志+程序 BTF)→计数为 0 直接放行→计数上限 INT_MAX/记录大小'
    # 区间 = rec_size 锚点 +0..+8 → v6.6.78 实际 15739..15747(9 行)
    seg "$af" "$l_rec" 0 8 "$f" 'rec_size 容错:下限=sizeof(bpf_core_relo)、上限=MAX_FUNCINFO_REC_SIZE、须 4 字节对齐——记录可带尾部扩充字段'
    # 区间 = 引擎调用点 +0..+0 → v6.6.78 实际 15778(1 行)
    seg "$af" "$l_apply" 0 0 "$f" '循环体内:每拷贝并校验一条记录,立即递给站 1 的 bpf_core_apply'
    # 区间 = 接线点 +0..+0 → v6.6.78 实际 15817(1 行)
    seg "$af" "$l_wire" 0 0 "$f" 'check_btf_info 内:CO-RE 重定位与 func_info/line_info 同一入口接线'
    printf '职责:系统调用侧的交接班——verifier 从 attr 里逐条拷贝记录、逐条改写指令;与 func_info/line_info「先整块拷再处理」不同,CO-RE 记录是「拷一条、补一条」(见 15749 起注释)。\n'
    return 0
}

# 站 4:UAPI 载体
# 校准(v6.6.78 实测):core_relo_cnt=bpf.h:1463、core_relos=:1465、
# core_relo_rec_size=:1466;前邻 union{attach_prog_fd/attach_btf_obj_fd} 至 1462
station_4() {
    local f=include/uapi/linux/bpf.h
    local af="$TREE/$f"
    local miss=0 l_cnt
    printf '\n## 站 4:UAPI 载体 union bpf_attr 的 core_relo 三字段\n'
    l_cnt=$(locate "$af" 'core_relo_cnt;' 1)
    [[ -n $l_cnt ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'core_relo_cnt;'
    show_locate "$f" -F 'core_relos;'
    show_locate "$f" -F 'core_relo_rec_size;'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = cnt 锚点 -6..+3 → v6.6.78 实际 1457..1466(10 行)
    seg "$af" "$l_cnt" -6 3 "$f" 'BPF_PROG_LOAD 命令的 attr 尾部:attach 目标 union 之后,core_relo 计数/数组指针/单条大小与 fd_array 相邻摆放——与 func_info/line_info 同款「计数+指针+单条大小」三字段组'
    printf '职责:记录的快递包装——用户态把 core_relo 数组连同 rec_size 一起塞进 BPF_PROG_LOAD 的 attr(04 号示例从 .BTF.ext 切出的记录,经 libbpf 组装从这里进内核);单条大小自报,内核按它逐条读取以向前兼容。\n'
    return 0
}

# 站 5:轻骨架提交
# 校准(v6.6.78 实测):attr.core_relo_cnt = gen->core_relo_cnt=gen_loader.c:970
# (三连提交 969..972);emit_rel_store(core_relos)=990;记录入口
# bpf_gen__record_relo_core=:851
station_5() {
    local f=tools/lib/bpf/gen_loader.c
    local af="$TREE/$f"
    local miss=0 l_cnt l_store
    printf '\n## 站 5:轻骨架提交 bpf_gen__prog_load\n'
    l_cnt=$(locate "$af" 'attr.core_relo_cnt = gen->core_relo_cnt;' 1)
    l_store=$(locate "$af" 'emit_rel_store(gen, attr_field(prog_load_attr, core_relos), core_relos);' 1)
    [[ -n $l_cnt && -n $l_store ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'void bpf_gen__record_relo_core(struct bpf_gen *gen,'
    show_locate "$f" -F 'attr.core_relo_cnt = gen->core_relo_cnt;'
    show_locate "$f" -F 'emit_rel_store(gen, attr_field(prog_load_attr, core_relos), core_relos);'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = cnt 锚点 -6..+5 → v6.6.78 实际 964..975(12 行)
    seg "$af" "$l_cnt" -6 5 "$f" '生成加载器程序时:line_info 三连之后紧跟 core_relo 三连(rec_size/rec_cnt/数组复制进 blob)→整个 attr 也作为数据进 blob'
    # 区间 = 存指针锚点 -1..+0 → v6.6.78 实际 989..990(2 行)
    seg "$af" "$l_store" -1 0 "$f" 'attr 里存的不是内核指针而是 blob 内偏移:emit_rel_store 往生成的加载器程序里编入存储指令,运行时把偏移写进 prog_load_attr'
    printf '职责:轻骨架(light skeleton)的同款提交——gen_loader 生成的加载器程序在运行时自己组装 BPF_PROG_LOAD 的 attr,CO-RE 记录走同一条 UAPI 通道(站 4);记录先经 bpf_gen__record_relo_core(:851)逐条登记。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-kernel-relo.sh —— 内核态重定位引擎五站切片(第 8 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(btf.c/verifier.c/bpf.h/gen_loader.c),不构建、不加载,对源码树零写入。\n'
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

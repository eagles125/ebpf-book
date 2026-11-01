#!/usr/bin/env bash
# trace-jit-arm64.sh —— 第 6 章《JIT 编译与性能优化》arm64 JIT 四站切片器
#
# 【脚本用途与输入】
# 沿「编译入口→build_body 指令分派→bpf2a64 寄存器映射→与 x86 差异点」的路径,
# 在 eBPF 走读源码树上按「站」切出四段关键代码,供正文《三、arm64:另一套后端
# 的同构与差异》逐站引用其行号与代码段。
#   - 用法:bash trace-jit-arm64.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 arch/arm64/net/bpf_jit_comp.c、bpf_jit.h,
#     不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各段注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 四站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78 校准)
#     + sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注
#     (涉及与 x86 后端对照的站,职责注必含「与 x86 对照」);
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间
#     (偏移按 v6.6.78 校准),树小幅漂移时区间随锚点自动平移;
#   - 末尾固定汇总:TREE_VERSION=v6.6.78 STATIONS=4
#     (TREE_VERSION 是书稿锁定版本常量,非自动探测);
#   - 退出码:四站全成 → 0;任一站提取失败 → 该站打印「STATION n: NOT FOUND」,
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
for rel in arch/arm64/net/bpf_jit_comp.c arch/arm64/net/bpf_jit.h; do
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

# ---------- 四站 ----------

# 站 1:编译入口 bpf_int_jit_compile
# 校准(v6.6.78 实测):定义=comp.c:1518
station_1() {
    local f=arch/arm64/net/bpf_jit_comp.c
    local af="$TREE/$f"
    local miss=0 l_entry
    printf '\n## 站 1:编译入口 bpf_int_jit_compile\n'
    l_entry=$(locate "$af" 'struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)' 1)
    [[ -n $l_entry ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 入口定义锚点 +0..+22 → v6.6.78 实际 1518..1540(23 行)
    seg "$af" "$l_entry" 0 22 "$f" '编译入口:jit_requested 门 + 常量致盲预处理(与 x86 站 1③ 同构)'
    printf '职责:与 x86 对照——入口结构完全同构(签名一致、先问 jit_requested、再做 bpf_jit_blind_constants 预处理),差异只在后端数据结构(arm64_jit_data/jit_ctx)。\n'
    return 0
}

# 站 2:build_body 指令分派
# 校准(v6.6.78 实测):build_body 定义=comp.c:1442(循环体至 1471)
station_2() {
    local f=arch/arm64/net/bpf_jit_comp.c
    local af="$TREE/$f"
    local miss=0 l_body
    printf '\n## 站 2:build_body 指令分派\n'
    l_body=$(locate "$af" 'static int build_body(struct jit_ctx *ctx, bool extra_pass)' 1)
    [[ -n $l_body ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int build_body(struct jit_ctx *ctx, bool extra_pass)'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = build_body 定义锚点 +0..+13 → v6.6.78 实际 1442..1455(14 行)
    seg "$af" "$l_body" 0 13 "$f" '函数头:offset[] 数组语义(每条 BPF 指令在机器码里的起止)'
    # 区间 = build_body 定义锚点 +14..+29 → v6.6.78 实际 1456..1471(16 行)
    seg "$af" "$l_body" 14 29 "$f" '逐条分派:for 循环调 build_insn(相当于 x86 的 do_jit);ret>0 表示双宽指令'
    printf '职责:与 x86 对照——arm64 把「逐条翻译」拆成 build_body(循环)+ build_insn(单条 lower)两层,对应 x86 的 do_jit 主循环;extra_pass 两趟语义与 x86 一致。\n'
    return 0
}

# 站 3:bpf2a64 寄存器映射
# 校准(v6.6.78 实测):映射注释与表=comp.c:44..70([BPF_REG_0] = A64_R(7)=47);
# bpf_jit.h A64_R 宏=13
station_3() {
    local f=arch/arm64/net/bpf_jit_comp.c hf=arch/arm64/net/bpf_jit.h
    local af="$TREE/$f" ah="$TREE/$hf"
    local miss=0 l_map l_a64r
    printf '\n## 站 3:bpf2a64 寄存器映射\n'
    l_map=$(locate "$af" 'static const int bpf2a64[] = {' 1)
    l_a64r=$(locate "$ah" '#define A64_R(x)' 1)
    [[ -n $l_map && -n $l_a64r ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static const int bpf2a64[] = {'
    show_locate "$f" -F '[BPF_REG_0] = A64_R(7),'
    show_locate "$hf" -F '#define A64_R(x)'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = bpf2a64 表锚点 -1..+21 → v6.6.78 实际 44..66(23 行)
    seg "$af" "$l_map" -1 21 "$f" 'bpf2a64 主体:BPF r0→x7、参数 r1-r5→x0-x4、被调用者保存 r6-r9→x19-x22'
    # 区间 = bpf2a64 表锚点 +22..+25 → v6.6.78 实际 67..70(4 行)
    seg "$af" "$l_map" 22 25 "$f" '表尾:致盲临时寄存器 AX→x9 与 FP_BOTTOM→x27'
    # 区间 = bpf_jit.h A64_R 宏锚点 +0..+0 → v6.6.78 实际 13..13(1 行)
    seg "$ah" "$l_a64r" 0 0 "$hf" 'A64_R(x):把寄存器编号展开成 AARCH64_INSN_REG_x'
    printf '职责:与 x86 对照——x86 的 reg2hex 让 BPF 参数寄存器对齐 System V 调用约定(r1-r5→rdi..r8),arm64 的 bpf2a64 则直接对齐 AAPCS(r1-r5→x0-x4);思路同构,映射表各异。\n'
    return 0
}

# 站 4:与 x86 的差异点(立即数装载与 icache)
# 校准(v6.6.78 实测):emit_a64_mov_i64=comp.c:129(函数体至 152);
# bpf_flush_icache 定义=1507..1510;调用点=1639
station_4() {
    local f=arch/arm64/net/bpf_jit_comp.c
    local af="$TREE/$f"
    local miss=0 l_mov l_flush l_call
    printf '\n## 站 4:与 x86 的差异点(立即数装载与 icache)\n'
    l_mov=$(locate "$af" 'static inline void emit_a64_mov_i64(const int reg, const u64 val,' 1)
    l_flush=$(locate "$af" 'static inline void bpf_flush_icache(void *start, void *end)' 1)
    l_call=$(locate "$af" 'bpf_flush_icache(header, ctx.image + ctx.idx);' 1)
    [[ -n $l_mov && -n $l_flush && -n $l_call ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static inline void emit_a64_mov_i64(const int reg, const u64 val,'
    show_locate "$f" -F 'static inline void bpf_flush_icache(void *start, void *end)'
    show_locate "$f" -F 'bpf_flush_icache(header, ctx.image + ctx.idx);'
    show_locate "$f" -F 'if (bpf_jit_enable > 1)'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = emit_a64_mov_i64 定义锚点 +0..+23 → v6.6.78 实际 129..152(24 行)
    seg "$af" "$l_mov" 0 23 "$f" '64 位立即数装载:MOVN/MOVZ 打头 + MOVK 按 16 位分块补齐(可取反压缩块数)'
    # 区间 = bpf_flush_icache 定义锚点 +0..+3 → v6.6.78 实际 1507..1510(4 行)
    seg "$af" "$l_flush" 0 3 "$f" 'icache 刷新封装:flush_icache_range(指令缓存与数据缓存不一致的架构必须显式刷)'
    # 区间 = 调用点锚点 -3..+0 → v6.6.78 实际 1636..1639(4 行)
    seg "$af" "$l_call" -3 0 "$f" '编译收尾:bpf_jit_enable>1 时先 dump 再刷 icache(sysctl 联动点)'
    printf '职责:与 x86 对照——x86 的 64 位立即数靠 emit_mov_imm64(单条 movabs imm32 拼 32/64 位),arm64 必须用 MOVZ/MOVK 序列按 16 位分块;arm64 写完映像必须显式 flush icache,x86 是一致的缓存体系无此步。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-jit-arm64.sh —— arm64 JIT 四站切片(第 6 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(bpf_jit_comp.c/bpf_jit.h),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1
station_3 || rc=1
station_4 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=4\n' "$TREE_VERSION"
exit "$rc"

#!/usr/bin/env bash
# trace-ringbuf.sh —— 练习②参考实现:环形缓冲区两站切片(独立小脚本)
#
# 【脚本用途与输入】
# 对走读树的 kernel/bpf/ringbuf.c 切出环形缓冲区两段式提交的两段关键代码,
# 与正文四节 4.2 呼应,接口与 05-map-internals/trace-map-internals.sh 同款:
#   - 站 1:__bpf_ringbuf_reserve(定义 :408)——预留路径(单条上限、NMI
#     trylock、pending 扫描,:408-432);
#   - 站 2:bpf_ringbuf_commit(:489)+ 就绪唤醒 bpf_ringbuf_notify(irq_work,:150)。
#   - 用法:bash trace-ringbuf.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树,不构建、不加载,对源码树零写入;结果全部输出到 stdout;
#   - 行号按 v6.6.78 校准(校准值写在各段注释),换树时以实测 grep 为准。
#
# 【输出结构】
#   - 两站,每站 = 「## 站 N:站名」+ grep -n 定位行(实测,标注 v6.6.78 校准)
#     + sed -n 'A,Bp' 关键代码段(每段 ≤25 行)+ 一行「职责:」中文注;
#   - 行号两步法:先 grep -n 定位锚点行,再用「锚点行 + 偏移」推出 sed 区间;
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
if [[ ! -f $WALK_TREE/kernel/bpf/ringbuf.c ]]; then
    printf '树内关键文件缺失:kernel/bpf/ringbuf.c(WALK_TREE 指错目录或树版本不符?)\n' >&2
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

# ---------- 通用小工具(与 05-map-internals 同款) ----------

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

# 站 1:预留 __bpf_ringbuf_reserve
# 校准(v6.6.78 实测):定义=ringbuf.c:408(至 432,与正文四节 4.2 贴码一致)
station_1() {
    local f=kernel/bpf/ringbuf.c
    local af="$TREE/$f"
    local miss=0 l_res
    printf '\n## 站 1:预留 __bpf_ringbuf_reserve\n'
    l_res=$(locate "$af" 'static void *__bpf_ringbuf_reserve(struct bpf_ringbuf *rb, u64 size)' 1)
    [[ -n $l_res ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static void *__bpf_ringbuf_reserve(struct bpf_ringbuf *rb, u64 size)'
    show_locate "$f" -F 'if (in_nmi()) {'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 预留锚点 +0..+24 → v6.6.78 实际 408..432(25 行)
    seg "$af" "$l_res" 0 24 "$f" '预留:单条上限/空间检查(cons_pos 决定可用)/NMI trylock 宁丢不等/pending 扫描跳过未提交'
    printf '职责:占位但不发布——reserve 拿到的是私有空间,填完由 commit 发布;缓冲满或抢锁失败返回 NULL(BPF 侧静默丢弃,正文四节 4.2)。\n'
    return 0
}

# 站 2:提交与唤醒
# 校准(v6.6.78 实测):bpf_ringbuf_commit=ringbuf.c:489;bpf_ringbuf_notify=150
station_2() {
    local f=kernel/bpf/ringbuf.c
    local af="$TREE/$f"
    local miss=0 l_com l_not
    printf '\n## 站 2:提交与唤醒(bpf_ringbuf_commit / bpf_ringbuf_notify)\n'
    l_com=$(locate "$af" 'static void bpf_ringbuf_commit(void *sample, u64 flags, bool discard)' 1)
    l_not=$(locate "$af" 'static void bpf_ringbuf_notify(struct irq_work *work)' 1)
    [[ -n $l_com && -n $l_not ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static void bpf_ringbuf_commit(void *sample, u64 flags, bool discard)'
    show_locate "$f" -F 'static void bpf_ringbuf_notify(struct irq_work *work)'
    show_locate "$f" -F 'atomic_xchg(&rb->busy, 0);'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = commit 锚点 +0..+8 → v6.6.78 实际 489..497(9 行)
    seg "$af" "$l_com" 0 8 "$f" '提交/丢弃:discard 标记进 hdr;推进 producer_pos 原子发布(消费者自此可见)'
    # 区间 = notify 锚点 +0..+7 → v6.6.78 实际 150..157(8 行)
    seg "$af" "$l_not" 0 7 "$f" '就绪唤醒:irq_work 回调——把「有新事件」递到安全上下文,唤醒轮询者'
    printf '职责:两段式的后半——commit 只做「推进 producer_pos」一个原子动作(发布语义);notify 经 irq_work 在安全上下文触发唤醒,轮询之外的即时性由此而来(与示例 03 的 ring_buffer__poll 对读)。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-ringbuf.sh —— 环形缓冲区两站切片(练习②参考实现)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(kernel/bpf/ringbuf.c),不构建、不加载,对源码树零写入。\n'
printf '# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。\n'

rc=0
station_1 || rc=1
station_2 || rc=1

printf '\n== 汇总 ==\n'
printf 'WALK_TREE=%s\n' "$WALK_TREE"
printf 'TREE_VERSION=%s STATIONS=2\n' "$TREE_VERSION"
exit "$rc"

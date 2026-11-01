#!/usr/bin/env bash
# trace-map-internals.sh —— 第 7 章《辅助函数与映射的内部实现》映射内部六站切片器
#
# 【脚本用途与输入】
# 沿「类型注册表→ops 接口→桶锁→更新→删除与回收→内存记账」的路径,在 eBPF
# 走读源码树上按「站」切出六段关键代码,供正文《二、hash 映射内部:并发与内存》
# 与《三、per-CPU、LRU 与内存记账》逐站引用其行号与代码段。
#   - 用法:bash trace-map-internals.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/syscall.c、include/linux/bpf.h、
#     kernel/bpf/hashtab.c,不构建、不加载,对源码树零写入;结果全部输出到 stdout;
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
for rel in kernel/bpf/syscall.c include/linux/bpf.h kernel/bpf/hashtab.c; do
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

# ---------- 六站 ----------

# 站 1:映射类型注册表
# 校准(v6.6.78 实测):bpf_map_types[] 定义=syscall.c:63(宏三定义+include 至 72)
station_1() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_types
    printf '\n## 站 1:映射类型注册表 bpf_map_types[]\n'
    l_types=$(locate "$af" 'static const struct bpf_map_ops * const bpf_map_types[] = {' 1)
    [[ -n $l_types ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static const struct bpf_map_ops * const bpf_map_types[] = {'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = 注册表锚点 +0..+9 → v6.6.78 实际 63..72(10 行)
    seg "$af" "$l_types" 0 9 "$f" '第三次展开:bpf_types.h 一份清单按 BPF_MAP_TYPE 宏展开成类型→ops 映射(第 5 章双展开的映射侧)'
    printf '职责:map_type → bpf_map_ops 的唯一映射点——BPF_MAP_CREATE 按 attr->map_type 在此查表(第 5 章三节生命周期入口的查表处)。\n'
    return 0
}

# 站 2:ops 接口表
# 校准(v6.6.78 实测):struct bpf_map_ops=include/linux/bpf.h:75
station_2() {
    local f=include/linux/bpf.h
    local af="$TREE/$f"
    local miss=0 l_ops
    printf '\n## 站 2:ops 接口表 struct bpf_map_ops\n'
    l_ops=$(locate "$af" 'struct bpf_map_ops {' 1)
    [[ -n $l_ops ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'struct bpf_map_ops {'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = ops 锚点 +0..+13 → v6.6.78 实际 75..88(14 行)
    seg "$af" "$l_ops" 0 13 "$f" '用户态可达的一组 ops:alloc_check/alloc/release/free/get_next_key/lookup 系/batch 系(程序侧 ops 随后)'
    printf '职责:每种映射类型一张函数表——正文二节的 lookup/update/delete 与三节的 per-CPU 聚合,全是这张表里某个函数指针的实现(第 5 章对回的「实现文件」在此点名)。\n'
    return 0
}

# 站 3:per-CPU 桶锁
# 校准(v6.6.78 实测):htab_lock_bucket=hashtab.c:149(至 170 raw_spin_lock)
station_3() {
    local f=kernel/bpf/hashtab.c
    local af="$TREE/$f"
    local miss=0 l_lock
    printf '\n## 站 3:per-CPU 桶锁 htab_lock_bucket\n'
    l_lock=$(locate "$af" 'static inline int htab_lock_bucket(const struct bpf_htab *htab,' 1)
    [[ -n $l_lock ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static inline int htab_lock_bucket(const struct bpf_htab *htab,'
    show_locate "$f" -F 'raw_spin_lock(&b->raw_lock);'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = 桶锁锚点 +0..+21 → v6.6.78 实际 149..170(22 行)
    seg "$af" "$l_lock" 0 21 "$f" '两级锁:先 per-CPU map_locked 判重入(-EBUSY),再抢桶级 raw_spin_lock(local_irq_save 关中断,RT 安全)'
    printf '职责:hash 更新/删除的写侧并发控制——锁粒度是「桶」,且先经 per-CPU 计数挡住同 CPU 重入;读侧完全无锁(站 4 的 RCU)。\n'
    return 0
}

# 站 4:更新路径
# 校准(v6.6.78 实测):htab_map_update_elem=hashtab.c:1098(头部至 hash 计算 1120)
station_4() {
    local f=kernel/bpf/hashtab.c
    local af="$TREE/$f"
    local miss=0 l_upd
    printf '\n## 站 4:更新路径 htab_map_update_elem\n'
    l_upd=$(locate "$af" 'static long htab_map_update_elem(struct bpf_map *map, void *key, void *value,' 1)
    [[ -n $l_upd ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static long htab_map_update_elem(struct bpf_map *map, void *key, void *value,'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = 更新锚点 +0..+22 → v6.6.78 实际 1098..1120(23 行)
    seg "$af" "$l_upd" 0 22 "$f" '更新头部:未知 flags 拒绝(-EINVAL)、RCU 读者断言、算哈希——NOEXIST/EXIST/ANY 三语义的起点'
    printf '职责:BPF_NOEXIST/EXIST/BPF_ANY 三种更新语义与并发窗口的实现现场(示例 01 的 lookup→update 窗口在这里对上源码);prealloc 与 NO_PREALLOC 两路在函数体后半分岔。\n'
    return 0
}

# 站 5:删除与元素回收(RCU)
# 校准(v6.6.78 实测):htab_map_delete_elem=hashtab.c:1424(至 1444);
# htab_elem_free(bpf_mem_cache_free)=892..901
station_5() {
    local f=kernel/bpf/hashtab.c
    local af="$TREE/$f"
    local miss=0 l_del l_free
    printf '\n## 站 5:删除与元素回收(RCU)\n'
    l_del=$(locate "$af" 'static long htab_map_delete_elem(struct bpf_map *map, void *key)' 1)
    l_free=$(locate "$af" 'static void htab_elem_free(struct bpf_htab *htab, struct htab_elem *l)' 1)
    [[ -n $l_del && -n $l_free ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static long htab_map_delete_elem(struct bpf_map *map, void *key)'
    show_locate "$f" -F 'static void htab_elem_free(struct bpf_htab *htab, struct htab_elem *l)'
    show_locate "$f" -F 'bpf_mem_cache_free(&htab->ma, l);'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = 删除锚点 +0..+20 → v6.6.78 实际 1424..1444(21 行)
    seg "$af" "$l_del" 0 20 "$f" '删除头部:RCU 读者断言 → 锁桶 → 原位查元素——摘链后延迟回收'
    # 区间 = 元素回收锚点 +0..+9 → v6.6.78 实际 892..901(10 行)
    seg "$af" "$l_free" 0 9 "$f" '回收:per-CPU 元素走 pcpu_ma,普通元素走 ma——bpf_mem_cache_free 内部按 RCU 时代延迟释放'
    printf '职责:「读侧无锁」的另一半——删除只在桶锁内摘链,内存经 bpf_mem_alloc 的 RCU 时代机制延迟归还,正在无锁读的读者永远看不到悬空指针。\n'
    return 0
}

# 站 6:memcg 内存记账
# 校准(v6.6.78 实测):bpf_map_save_memcg=syscall.c:389(至 398)
station_6() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_save
    printf '\n## 站 6:memcg 内存记账 bpf_map_save_memcg\n'
    l_save=$(locate "$af" 'static void bpf_map_save_memcg(struct bpf_map *map)' 1)
    [[ -n $l_save ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static void bpf_map_save_memcg(struct bpf_map *map)'
    show_locate "$f" -F 'static void bpf_map_release_memcg(struct bpf_map *map)'
    if [[ $miss -ne 0 ]]; then station_fail 6; return 1; fi
    # 区间 = save_memcg 锚点 +0..+9 → v6.6.78 实际 389..398(10 行)
    seg "$af" "$l_save" 0 9 "$f" '创建时挂靠:map->objcg = 创建者所在 cgroup(root cgroup 为 NULL,使用时逐处检查)'
    printf '职责:映射内存记账的 6.x 形态——全部映射内存记在创建者 memcg 名下(配额随 cgroup 走,不再用 memlock rlimit);JIT 侧另有 bpf_jit_charge/uncharge_modmem 受 bpf_jit_limit 约束(第 6 章一节的 limit)。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-map-internals.sh —— 映射内部六站切片(第 7 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(syscall.c/bpf.h/hashtab.c),不构建、不加载,对源码树零写入。\n'
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

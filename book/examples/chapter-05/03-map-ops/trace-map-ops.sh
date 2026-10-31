#!/usr/bin/env bash
# trace-map-ops.sh —— 第 5 章《eBPF 子系统内核代码结构》映射生命周期切片器
#
# 【脚本用途与输入】
# 沿映射(map)从创建到释放的完整生命周期,在 eBPF 走读源码树上按「站」切出
# 六段关键代码,供正文《映射:创建、查找/更新/删除、freeze/pin 与释放》逐站
# 引用其行号与代码段(本输出的行号即引用事实)。
#   - 用法:bash trace-map-ops.sh [WALK_TREE]
#     WALK_TREE 为可选位置参数,默认 /work/linux/linux-6.6.78;
#     也兼容环境变量 WALK_TREE(位置参数优先);
#   - 只读源码树:仅 grep/sed 读取 kernel/bpf/syscall.c、arraymap.c,
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
for rel in kernel/bpf/syscall.c kernel/bpf/arraymap.c; do
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

# 站 1:入口分发与 bpf_map_types[] 注册表
# 校准(v6.6.78 实测):bpf_map_types[] =63;__sys_bpf=5426;
# case BPF_MAP_CREATE:=5446(第二处 5587 在 BPF_CALL_3(bpf_sys_bpf) 白名单里)
station_1() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_types l_sys l_case
    printf '\n## 站 1:入口分发与 bpf_map_types[] 注册表\n'
    l_types=$(locate "$af" 'bpf_map_types[] = {' 1)
    l_sys=$(locate "$af" '__sys_bpf' 1)
    l_case=$(locate "$af" 'case BPF_MAP_CREATE:' 1)
    [[ -n $l_types && -n $l_sys && -n $l_case ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bpf_map_types[] = {'
    show_locate "$f" -F '__sys_bpf'
    show_locate "$f" -F 'case BPF_MAP_CREATE:'
    if [[ $miss -ne 0 ]]; then station_fail 1; return 1; fi
    # 区间 = bpf_map_types[] 锚点 +0..+9 → v6.6.78 实际 63..72(10 行)
    seg "$af" "$l_types" 0 9 "$f" 'bpf_map_types[]:按 BPF_MAP_TYPE 宏在编译期展开的 ops 注册表,下标即 map_type'
    # 区间 = __sys_bpf 锚点 +0..+17 → v6.6.78 实际 5426..5443(18 行)
    seg "$af" "$l_sys" 0 17 "$f" '__sys_bpf:uarg 尾零检查、union bpf_attr 拷贝与 security_bpf 钩子'
    # 区间 = case BPF_MAP_CREATE 锚点 -1..+3 → v6.6.78 实际 5445..5449(5 行)
    seg "$af" "$l_case" -1 3 "$f" 'switch(cmd) 分发:BPF_MAP_CREATE 是第一个 case,转入 map_create()'
    printf '职责:命令入口——bpf(2) 经 __sys_bpf 按 cmd 分发,BPF_MAP_CREATE 转入 map_create;可用映射类型由 bpf_map_types[] 注册表在编译期展开。\n'
    return 0
}

# 站 2:map_create 与类型查找
# 校准(v6.6.78 实测):map_create( 定义=1127;map 侧 array_index_nospec=1165
# (另一处 2056 在 prog 侧);sysctl 判定=1186(声明=60、prog 侧第二判定=2618)
station_2() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_def l_nospec l_sysctl
    printf '\n## 站 2:map_create 与类型查找(array_index_nospec)\n'
    l_def=$(locate "$af" 'static int map_create(union bpf_attr *attr)' 1)
    l_nospec=$(locate "$af" 'map_type = array_index_nospec(map_type, ARRAY_SIZE(bpf_map_types));' 1)
    l_sysctl=$(locate "$af" 'sysctl_unprivileged_bpf_disabled && !bpf_capable()' 1)
    [[ -n $l_def && -n $l_nospec && -n $l_sysctl ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int map_create(union bpf_attr *attr)'
    show_locate "$f" -F 'array_index_nospec'
    show_locate "$f" -F 'sysctl_unprivileged_bpf_disabled'
    if [[ $miss -ne 0 ]]; then station_fail 2; return 1; fi
    # 区间 = map_create( 定义锚点 +0..+19 → v6.6.78 实际 1127..1146(20 行)
    seg "$af" "$l_def" 0 19 "$f" 'map_create 头部:签名、CHECK_ATTR 边界与 BTF key/value 类型约束'
    # 区间 = nospec 锚点 -4..+11 → v6.6.78 实际 1161..1176(16 行)
    seg "$af" "$l_nospec" -4 11 "$f" '类型查找:先查 bpf_map_types[] 上界,array_index_nospec 堵住侧信道后才取 ops;随后 ops->map_alloc_check 预检'
    # 区间 = sysctl 判定锚点 -6..+1 → v6.6.78 实际 1180..1187(8 行)
    seg "$af" "$l_sysctl" -6 1 "$f" '非特权门槛:sysctl_unprivileged_bpf_disabled 置位时 map 创建一律要 CAP_BPF'
    printf '职责:map_create 是映射生命周期起点——array_index_nospec 防越界侧信道后查 bpf_map_types[] 得 ops,过 alloc_check 与非特权门槛再进入分配。\n'
    return 0
}

# 站 3:通用层 bpf_map_update_value / bpf_map_copy_value 与 array_map_ops 对照
# 校准(v6.6.78 实测):bpf_map_update_value=153;通用 ops->map_update_elem
# 调用=201;ops->map_lookup_elem 调用=247;array_map_ops 定义=arraymap.c:750
station_3() {
    local f=kernel/bpf/syscall.c af=kernel/bpf/arraymap.c
    local asc="$TREE/$f" aam="$TREE/$af"
    local miss=0 l_upd l_callu l_calll l_ops
    printf '\n## 站 3:通用层 bpf_map_update_value / bpf_map_copy_value 与 array_map_ops 对照\n'
    l_upd=$(locate "$asc" 'static int bpf_map_update_value' 1)
    l_callu=$(locate "$asc" 'err = map->ops->map_update_elem(map, key, value, flags);' 1)
    l_calll=$(locate "$asc" 'ptr = map->ops->map_lookup_elem(map, key);' 1)
    l_ops=$(locate "$aam" 'const struct bpf_map_ops array_map_ops = {' 1)
    [[ -n $l_upd && -n $l_callu && -n $l_calll && -n $l_ops ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'static int bpf_map_update_value'
    show_locate "$f" -F 'err = map->ops->map_update_elem(map, key, value, flags);'
    show_locate "$f" -F 'ptr = map->ops->map_lookup_elem(map, key);'
    show_locate "$af" -F 'const struct bpf_map_ops array_map_ops = {'
    if [[ $miss -ne 0 ]]; then station_fail 3; return 1; fi
    # 区间 = bpf_map_update_value 锚点 +0..+17 → v6.6.78 实际 153..170(18 行)
    seg "$asc" "$l_upd" 0 17 "$f" 'bpf_map_update_value 头部:offload/CPUMAP/sockmap/prog_array 等特殊类型先行分流'
    # 区间 = ops->map_update_elem 调用锚点 -1..+7 → v6.6.78 实际 200..208(9 行)
    seg "$asc" "$l_callu" -1 7 "$f" '通用尾:常规类型在 rcu_read_lock 下转入 map->ops->map_update_elem'
    # 区间 = ops->map_lookup_elem 调用锚点 -3..+16 → v6.6.78 实际 244..263(20 行)
    seg "$asc" "$l_calll" -3 16 "$f" 'bpf_map_copy_value 通用尾:ops->map_lookup_elem 取指针后 copy_map_value 回用户缓冲'
    # 区间 = array_map_ops 锚点 +0..+23 → v6.6.78 实际 750..773(24 行)
    seg "$aam" "$l_ops" 0 23 "$af" 'array_map_ops:BPF_MAP_TYPE_ARRAY 的回调填法,lookup/update/delete 各就各位'
    printf '职责:通用层按 map 类型分流——特殊类型各自直呼,常规类型最终落到 ops->map_update_elem / ops->map_lookup_elem;array_map_ops 是这组回调的数组实现。\n'
    return 0
}

# 站 4:命令包装 map_lookup_elem / map_update_elem / map_delete_elem
# 校准(v6.6.78 实测):__bpf_map_get=1322;map_lookup_elem(=1434、copy_value
# 主调用=1487(第一处 1483 在 bloom 分支);map_update_elem(=1509、
# update_value 调用=1552;map_delete_elem(=1565、rcu 下 ops 调用=1605
# (第一处 1599 在 sleepable 分支、第三处 1725 在 map_delete_batch,取第二处)
station_4() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_get l_look l_copy l_upd l_callv l_del l_calld
    printf '\n## 站 4:命令包装 map_lookup_elem / map_update_elem / map_delete_elem\n'
    l_get=$(locate "$af" 'struct bpf_map *__bpf_map_get(struct fd f)' 1)
    l_look=$(locate "$af" 'static int map_lookup_elem(union bpf_attr *attr)' 1)
    l_copy=$(locate "$af" 'err = bpf_map_copy_value(map, key, value, attr->flags);' 2)
    l_upd=$(locate "$af" 'static int map_update_elem(union bpf_attr *attr, bpfptr_t uattr)' 1)
    l_callv=$(locate "$af" 'err = bpf_map_update_value(map, f.file, key, value, attr->flags);' 1)
    l_del=$(locate "$af" 'static int map_delete_elem(union bpf_attr *attr, bpfptr_t uattr)' 1)
    l_calld=$(locate "$af" 'err = map->ops->map_delete_elem(map, key);' 2)
    [[ -n $l_get && -n $l_look && -n $l_copy && -n $l_upd && -n $l_callv && -n $l_del && -n $l_calld ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'struct bpf_map *__bpf_map_get(struct fd f)'
    show_locate "$f" -F 'static int map_lookup_elem(union bpf_attr *attr)'
    show_locate "$f" -F 'err = bpf_map_copy_value(map, key, value, attr->flags);'
    show_locate "$f" -F 'static int map_update_elem(union bpf_attr *attr, bpfptr_t uattr)'
    show_locate "$f" -F 'err = bpf_map_update_value(map, f.file, key, value, attr->flags);'
    show_locate "$f" -F 'static int map_delete_elem(union bpf_attr *attr, bpfptr_t uattr)'
    show_locate "$f" -F 'err = map->ops->map_delete_elem(map, key);'
    if [[ $miss -ne 0 ]]; then station_fail 4; return 1; fi
    # 区间 = __bpf_map_get 锚点 +0..+10 → v6.6.78 实际 1322..1332(11 行)
    seg "$af" "$l_get" 0 10 "$f" '__bpf_map_get:校验 f_op 确是 bpf_map_fops 后交出 private_data,三个命令共用'
    # 区间 = map_lookup_elem( 锚点 +17..+24 → v6.6.78 实际 1451..1458(8 行)
    seg "$af" "$l_look" 17 24 "$f" 'map_lookup_elem:fdget → __bpf_map_get → FMODE_CAN_READ 权限位'
    # 区间 = copy_value 主调用锚点 +0..+6 → v6.6.78 实际 1487..1493(7 行)
    seg "$af" "$l_copy" 0 6 "$f" 'lookup 尾声:调通用层 bpf_map_copy_value,再 copy_to_user 回拷'
    # 区间 = map_update_elem( 锚点 +14..+22 → v6.6.78 实际 1523..1531(9 行)
    seg "$af" "$l_upd" 14 22 "$f" 'map_update_elem:fdget → __bpf_map_get → 写活动计数 + FMODE_CAN_WRITE'
    # 区间 = update_value 调用锚点 +0..+8 → v6.6.78 实际 1552..1560(9 行)
    seg "$af" "$l_callv" 0 8 "$f" 'update 尾声:调通用层 bpf_map_update_value;err_put 里写活动计数递减、fdput'
    # 区间 = rcu 下 ops 调用锚点 -2..+3 → v6.6.78 实际 1603..1608(6 行)
    seg "$af" "$l_calld" -2 3 "$f" 'delete:无独立通用层,关抢占后 rcu_read_lock 内直呼 ops->map_delete_elem'
    printf '职责:三个命令包装同构——fdget→__bpf_map_get 验 fd→权限位检查→拷入 key/value→调通用层;update/delete 还以 bpf_map_write_active_inc 标记写活动,供 freeze 互斥。\n'
    return 0
}

# 站 5:freeze 与 pin(map_freeze / bpf_obj_pin)
# 校准(v6.6.78 实测):bpf_map_write_active=124;map_freeze(=1998、
# write_active 判定=2023;bpf_obj_pin(=2797
station_5() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_wa l_frz l_chk l_pin
    printf '\n## 站 5:freeze 与 pin(map_freeze / bpf_obj_pin)\n'
    l_wa=$(locate "$af" 'bool bpf_map_write_active(const struct bpf_map *map)' 1)
    l_frz=$(locate "$af" 'static int map_freeze(const union bpf_attr *attr)' 1)
    l_chk=$(locate "$af" 'if (bpf_map_write_active(map)) {' 1)
    l_pin=$(locate "$af" 'static int bpf_obj_pin(const union bpf_attr *attr)' 1)
    [[ -n $l_wa && -n $l_frz && -n $l_chk && -n $l_pin ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -F 'bool bpf_map_write_active(const struct bpf_map *map)'
    show_locate "$f" -F 'static int map_freeze(const union bpf_attr *attr)'
    show_locate "$f" -F 'if (bpf_map_write_active(map)) {'
    show_locate "$f" -F 'static int bpf_obj_pin(const union bpf_attr *attr)'
    if [[ $miss -ne 0 ]]; then station_fail 5; return 1; fi
    # 区间 = bpf_map_write_active 锚点 +0..+3 → v6.6.78 实际 124..127(4 行)
    seg "$af" "$l_wa" 0 3 "$f" 'bpf_map_write_active:读 writecnt 判断是否有并发写(站 4 的 inc/dec 在此汇总)'
    # 区间 = map_freeze( 锚点 +9..+22 → v6.6.78 实际 2007..2020(14 行)
    seg "$af" "$l_frz" 9 22 "$f" 'map_freeze 前半:fdget → __bpf_map_get → STRUCT_OPS/带 record 拒绝 → FMODE_CAN_WRITE'
    # 区间 = write_active 判定锚点 -1..+9 → v6.6.78 实际 2022..2032(11 行)
    seg "$af" "$l_chk" -1 9 "$f" 'freeze 主体:freeze_mutex 下查写活动(-EBUSY)后翻转 map->frozen——纯标志位,无 ops 回调'
    # 区间 = bpf_obj_pin( 锚点 +0..+14 → v6.6.78 实际 2797..2811(15 行)
    seg "$af" "$l_pin" 0 14 "$f" 'bpf_obj_pin:校验后转 bpf_obj_pin_user,把 fd 对象挂到 bpffs 路径'
    printf '职责:BPF_MAP_FREEZE 只在 freeze_mutex 下把 map->frozen 置真——自包含、无 ops 回调;BPF_OBJ_PIN 把 map fd 绑到 bpffs 路径,使其脱离进程 fd 生命周期。\n'
    return 0
}

# 站 6:释放链 bpf_map_put / bpf_map_free_deferred / bpf_map_free_rcu_gp
# 校准(v6.6.78 实测):bpf_map_release=773;bpf_map_put_with_uref=767;
# bpf_map_put_uref=717;bpf_map_put=750;bpf_map_free_rcu_gp=734(call_rcu
# 调用=744/760);bpf_map_free_deferred=691
station_6() {
    local f=kernel/bpf/syscall.c
    local af="$TREE/$f"
    local miss=0 l_rel l_with l_uref l_put l_rcu l_def
    printf '\n## 站 6:释放链 bpf_map_put / bpf_map_free_deferred / bpf_map_free_rcu_gp\n'
    l_rel=$(locate "$af" 'static int bpf_map_release(struct inode *inode, struct file *filp)' 1)
    l_with=$(locate "$af" 'void bpf_map_put_with_uref(struct bpf_map *map)' 1)
    l_uref=$(locate "$af" 'static void bpf_map_put_uref(struct bpf_map *map)' 1)
    l_put=$(locate "$af" 'void bpf_map_put(struct bpf_map *map)' 1)
    l_rcu=$(locate "$af" 'static void bpf_map_free_rcu_gp(struct rcu_head *rcu)' 1)
    l_def=$(locate "$af" 'static void bpf_map_free_deferred(struct work_struct *work)' 1)
    [[ -n $l_rel && -n $l_with && -n $l_uref && -n $l_put && -n $l_rcu && -n $l_def ]] || miss=1
    printf '### 定位(grep -n 实测;v6.6.78 校准)\n'
    show_locate "$f" -E '^static void (bpf_map_free_deferred|bpf_map_free_in_work|bpf_map_put_uref|bpf_map_free_rcu_gp|bpf_map_free_mult_rcu_gp)\(|^void bpf_map_put(_with_uref)?\(|^static int bpf_map_release\('
    if [[ $miss -ne 0 ]]; then station_fail 6; return 1; fi
    # 区间 = put_with_uref 锚点 +0..+15 → v6.6.78 实际 767..782(16 行)
    seg "$af" "$l_with" 0 15 "$f" 'bpf_map_put_with_uref = put_uref + put;bpf_map_release:fd 关闭(->release)即触发'
    # 区间 = put_uref 锚点 +0..+6 → v6.6.78 实际 717..723(7 行)
    seg "$af" "$l_uref" 0 6 "$f" 'bpf_map_put_uref:用户可见引用 usercnt 归零时回调 ops->map_release_uref'
    # 区间 = bpf_map_put 锚点 -3..+15 → v6.6.78 实际 747..765(19 行)
    seg "$af" "$l_put" -3 15 "$f" 'bpf_map_put:refcnt 归零先注销 id,再按 free_after_* 标志走 RCU/工作队列回收'
    # 区间 = free_rcu_gp 锚点 -9..+11 → v6.6.78 实际 725..745(21 行)
    seg "$af" "$l_rcu" -9 11 "$f" 'RCU 宽限期到 → bpf_map_free_rcu_gp → bpf_map_free_in_work 排入工作队列'
    # 区间 = free_deferred 锚点 +0..+24 → v6.6.78 实际 691..715(25 行)
    seg "$af" "$l_def" 0 24 "$f" 'bpf_map_free_deferred:工作队列里 security/ memcg 清理后 ops->map_free,再放 btf_record 与 btf'
    printf '职责:引用计数两级归零——usercnt(uref)与 refcnt;refcnt 归零先注销 id,再经 RCU 宽限期转工作队列,最终在 bpf_map_free_deferred 里 ops->map_free 释放实现私有数据。\n'
    return 0
}

# ---------- 主流程 ----------

printf '# trace-map-ops.sh —— 映射生命周期六站切片(第 5 章源码走读)\n'
printf '# 走读树:WALK_TREE=%s\n' "$WALK_TREE"
printf '# 声明:只读源码树(syscall.c/arraymap.c),不构建、不加载,对源码树零写入。\n'
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

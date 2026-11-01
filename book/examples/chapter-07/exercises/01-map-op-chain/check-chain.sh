#!/usr/bin/env bash
# check-chain.sh —— 练习①「映射操作链查证」机检(对走读树逐锚点断言)
#
# 【脚本用途与输入】
# 以 answer.md 调用链表中的 6 个关键锚点为断言对象,逐个到走读树对应文件做
# grep -F 定长核对——链上每一跳「文件在场 + 符号命中」才算过。
#   - 走读树:环境变量 WALK_TREE,默认 /work/linux/linux-6.6.78;
#   - 断言清单:本脚本内置(与 answer.md 的链表一致),不读外部数据源;
#   - 只读源码树,不构建、不加载;对源码树零写入。
#
# 【输出结构】
#   - 逐锚点一行:PASS <描述> 或 FAIL <描述>(附原因);
#   - 末尾固定汇总:TOTAL=6 PASS=<n> FAIL=<n> WALK_TREE=<路径>(口径同 verify-citations.sh);
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 定长)、dirname;
#   - grep 缺失:启动即报错退出(1);WALK_TREE 无效:逐锚点 FAIL,汇总后 exit 1。
#
# 【失败模式】
#   1. 锚点未命中:内核改名/移动/内联——回树 grep 新位置,同步更新本脚本与
#      answer.md(两者必须一致);
#   2. WALK_TREE 指错:全部 FAIL 且附「文件不在树」——先指对树再重跑;
#   3. 退出码非 0 恒为 1,可作 CI 门禁。
set -u

if ! command -v grep >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 grep,无法机检\n' >&2
    exit 1
fi

WALK_TREE="${WALK_TREE:-/work/linux/linux-6.6.78}"

total=0
pass=0
fail=0

check() { # $1 树内相对文件 $2 断言符号 $3 描述
    total=$((total + 1))
    local f="$WALK_TREE/$1"
    if [[ ! -f $f ]]; then
        printf 'FAIL %s(文件不在树:%s)\n' "$3" "$1"
        fail=$((fail + 1))
    elif grep -F -q -- "$2" "$f"; then
        printf 'PASS %s\n' "$3"
        pass=$((pass + 1))
    else
        printf 'FAIL %s(符号未命中:%s)\n' "$3" "$2"
        fail=$((fail + 1))
    fi
}

# 链上 6 锚点(与 answer.md 的行号表逐条对应;按符号断言,不按行号)
check kernel/bpf/syscall.c 'case BPF_MAP_UPDATE_ELEM:' '跳1 命令分发 case BPF_MAP_UPDATE_ELEM'
check kernel/bpf/syscall.c 'static int map_update_elem(union bpf_attr *attr, bpfptr_t uattr)' '跳2 业务入口 map_update_elem'
check kernel/bpf/syscall.c 'err = bpf_map_update_value(map, f.file, key, value, attr->flags);' '跳3 类型分派包装调用'
check kernel/bpf/syscall.c 'err = map->ops->map_update_elem(map, key, value, flags);' '跳4 ops 直调(RCU 读侧内)'
check kernel/bpf/hashtab.c 'static long htab_map_update_elem(struct bpf_map *map, void *key, void *value,' '跳5 hash 实现入口'
check kernel/bpf/hashtab.c 'static inline int htab_lock_bucket(const struct bpf_htab *htab,' '跳6 桶锁(先锁后判)'

printf 'TOTAL=%d PASS=%d FAIL=%d WALK_TREE=%s\n' "$total" "$pass" "$fail" "$WALK_TREE"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

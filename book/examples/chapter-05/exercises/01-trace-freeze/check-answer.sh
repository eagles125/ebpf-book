#!/usr/bin/env bash
# check-answer.sh —— 练习①「BPF_MAP_FREEZE 全链走读卡」参考答案符号机检
#
# 【脚本用途与输入】
# 以本文件内嵌的参考答案符号表(REF_SYMBOLS,10 条锚点)为数据源,逐条到
# eBPF 走读源码树做只读核对:先确认 kernel/bpf/syscall.c 在树中存在,再用
# grep -F 定长匹配符号。练习①的完整调用链(SYSCALL_DEFINE3(bpf, →
# __sys_bpf → case BPF_MAP_FREEZE → map_freeze)全部收在 syscall.c 一个
# 文件里——机检按符号不按行号(4.1 两步法),行号漂移不影响判定。
#   - 走读树:环境变量 WALK_TREE,默认 /work/linux/linux-6.6.78;
#   - 数据源:内嵌于本脚本的参考答案符号表(剧透——先自行走读并整理
#     自己的锚点清单,再跑本脚本对照);
#   - 只读源码树,不构建、不加载;脚本对源码树零写入。
#
# 【输出结构】
#   - 每条锚点一行:PASS kernel/bpf/syscall.c|<符号> 或 FAIL <原因>;
#   - 末尾固定汇总一行:
#       TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78
#     其中 TREE_VERSION 是书稿锁定的走读树版本常量(非自动探测);若实际
#     树的版本与之不符,机检结果不可信,应先把 WALK_TREE 指对;
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1,可直接作门禁——练习任务要求的
#     「机检 FAIL=0 收口」即此行。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 定长匹配),无其他外部命令;
#   - grep 缺失:启动即报错退出(1),绝不产出「假 PASS」;
#   - WALK_TREE 指向不存在/错误的目录:syscall.c 不在场,每条锚点按
#     「文件不在树」逐行记 FAIL,汇总 FAIL=TOTAL,退出码 1。
#
# 【失败模式(两类;任何 FAIL 都回修自己的锚点清单,不改脚本放水)】
#   1. 文件不在树:kernel/bpf/syscall.c 在 $WALK_TREE 下不存在——树版本
#      不符或 WALK_TREE 指错目录;
#   2. 符号未命中:文件在,但 grep -F 找不到该符号——内核把符号改名、
#      内联或搬移,或你的走读卡锚点写错形态:回树 grep -n 重定位,更新
#      自己的清单后重跑(处置链同正文排错③)。
set -u

TREE_VERSION="v6.6.78"
DEFAULT_TREE="/work/linux/linux-6.6.78"
CFILE="kernel/bpf/syscall.c"

WALK_TREE="${WALK_TREE:-$DEFAULT_TREE}"

# 依赖检查:缺 grep 时不做任何核对,直接失败(降级行为见头部注释)
if ! command -v grep >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 grep,无法机检\n' >&2
    exit 1
fi

# ---------- 参考答案符号表(剧透分界:先自行走读,再对照) ----------
# 十条锚点按调用链顺序排列,「|」后是给走读卡对照用的一句话说明;
# grep -F 全部限定在 syscall.c 内命中——全链不经其他文件,这正是
# 「map_freeze 自包含、无 ops 回调」的机检证据。
REF_SYMBOLS=(
    'SYSCALL_DEFINE3(bpf,|bpf(2) 系统调用入口,包 USER_BPFPTR 转交 __sys_bpf'
    '__sys_bpf|实际分发载体:三道前置工序后 switch(cmd)'
    'case BPF_MAP_FREEZE|分发支路:冻结命令转 map_freeze(第一处;第二处在 bpf_sys_bpf 白名单)'
    'static int map_freeze(|冻结命令实体:fdget→__bpf_map_get→权限→并发闸→置位'
    '__bpf_map_get|fd 校验:f_op 确是 bpf_map_fops 才交出 private_data'
    'map_get_sys_perms|权限位合成:f_mode 叠加全局 frozen 状态'
    'FMODE_CAN_WRITE|可写权限位:冻结后 map_get_sys_perms 抹掉它'
    'freeze_mutex|冻结互斥锁:冻结与 mmap/释放之间的串行化'
    'bpf_map_write_active|写活动判定:writecnt 原子计数非零即有并发写'
    'frozen|冻结标志位:freeze 的最终落点,纯标志、无 ops 回调'
)

total=0
pass=0
fail=0

# 检查 0:文件必须在走读树中存在(只读测试);缺席则逐行记 FAIL
if [[ ! -f $WALK_TREE/$CFILE ]]; then
    for entry in "${REF_SYMBOLS[@]}"; do
        sym=${entry%%|*}
        printf 'FAIL 文件不在树:%s|%s\n' "$CFILE" "$sym"
        total=$((total + 1))
        fail=$((fail + 1))
    done
else
    for entry in "${REF_SYMBOLS[@]}"; do
        sym=${entry%%|*}
        total=$((total + 1))
        # 检查 1:grep -F 定长匹配符号(-- 防止以 - 开头的符号被当选项)
        if grep -F -q -- "$sym" "$WALK_TREE/$CFILE"; then
            printf 'PASS %s|%s\n' "$CFILE" "$sym"
            pass=$((pass + 1))
        else
            printf 'FAIL 符号未命中:%s|%s\n' "$CFILE" "$sym"
            fail=$((fail + 1))
        fi
    done
fi

printf 'TOTAL=%d PASS=%d FAIL=%d WALK_TREE=%s TREE_VERSION=%s\n' \
    "$total" "$pass" "$fail" "$WALK_TREE" "$TREE_VERSION"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

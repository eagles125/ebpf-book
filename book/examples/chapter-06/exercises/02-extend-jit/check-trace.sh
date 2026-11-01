#!/usr/bin/env bash
# check-trace.sh —— 练习②验收机检(语法检查 + 实跑断言致盲两站)
#
# 【脚本用途与输入】
# 对同目录 trace-blind-station.sh 做两级验收:
#   1. bash -n 语法检查(脚本必须可解析);
#   2. 实跑切片器,断言输出含两站标题(站 1/站 2)、两个锚点符号
#      (bpf_jit_blind_insn / bpf_jit_blind_constants)与 STATIONS=2 汇总,
#      且退出码为 0。
# 无参数;走读树用 trace-blind-station.sh 的默认值(可用环境变量
# WALK_TREE 透传)。
#
# 【输出结构】
#   - 逐项断言一行:PASS <项> 或 FAIL <项>(附原因);
#   - 末尾固定汇总:TOTAL=<n> PASS=<n> FAIL=<n>(口径同 verify-citations.sh);
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 断言)、dirname;
#   - 依赖缺失:启动即报错退出(1),绝不产出「假 PASS」;
#   - 被测脚本缺失或语法错:直接 FAIL 计数,汇总后 exit 1。
#
# 【失败模式】
#   1. 语法错:bash -n 报错——回练习脚本修语法;
#   2. 锚点未命中:切片器实跑 STATION n: NOT FOUND 或输出缺符号——回树
#      grep 校准锚点与偏移(提示见练习 README);
#   3. 汇总不符:STATIONS=2 缺失或退出码非 0——对照 02-jit-x86 同款接口
#      逐项排查(只读声明/汇总行/退出码)。
set -u

total=0
pass=0
fail=0

check() { # $1 项目名 $2 是否成功(0=成功) $3 失败说明
    total=$((total + 1))
    if [[ $2 -eq 0 ]]; then
        printf 'PASS %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL %s(%s)\n' "$1" "$3"
        fail=$((fail + 1))
    fi
}

here="$(cd "$(dirname "$0")" && pwd)" || exit 1
target="$here/trace-blind-station.sh"

if ! command -v bash >/dev/null 2>&1 || ! command -v grep >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 bash/grep\n' >&2
    exit 1
fi

# 断言 1:被测脚本存在
[[ -f $target ]]
check '脚本存在 trace-blind-station.sh' $? '路径解析失败或文件缺失'

# 断言 2:bash -n 语法检查
bash -n "$target" 2>/dev/null
check 'bash -n 语法检查' $? '语法错误,见 bash -n 输出'

# 断言 3:实跑(退出码 0)
out="$(bash "$target" 2>&1)"
rc=$?
check '实跑退出码为 0' "$rc" "exit=$rc,输出尾部见 $(printf '%s' "$out" | tail -n 2 | tr '\n' ' ')"

# 断言 4:两站标题在输出中
printf '%s' "$out" | grep -F -q '## 站 1'
check '输出含「## 站 1」' $? '站 1 未输出(锚点漂移或结构缺失)'
printf '%s' "$out" | grep -F -q '## 站 2'
check '输出含「## 站 2」' $? '站 2 未输出(锚点漂移或结构缺失)'

# 断言 5:两个锚点符号在输出中(定位行或代码段内出现即可)
printf '%s' "$out" | grep -F -q 'bpf_jit_blind_insn'
check '输出含锚点 bpf_jit_blind_insn' $? '未命中站 1 锚点'
printf '%s' "$out" | grep -F -q 'bpf_jit_blind_constants'
check '输出含锚点 bpf_jit_blind_constants' $? '未命中站 2 锚点'

# 断言 6:汇总行
printf '%s' "$out" | grep -F -q 'STATIONS=2'
check '汇总含 STATIONS=2' $? '汇总行缺失或站数不符'

printf 'TOTAL=%d PASS=%d FAIL=%d\n' "$total" "$pass" "$fail"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

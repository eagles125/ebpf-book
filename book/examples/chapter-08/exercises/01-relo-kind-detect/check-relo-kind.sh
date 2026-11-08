#!/usr/bin/env bash
# check-relo-kind.sh —— 练习①「重定位记录判读」机检(实跑 04 切片器逐项断言)
#
# 【脚本用途与输入】
# 对本练习产出的 demo.bpf.o 实跑 ../../04-relo-slicer/slice-relos.py,
# 按练习① 的验收口径逐项断言:切片器五站走通(退出码 0)、记录数 ≥3、
# 三种目标 kind(0 字段字节偏移 / 2 字段存在 / 9 类型大小)的 uapi 枚举名
# 逐个在切片输出里在场。
#   - 切片器:环境变量 SLICER,默认 ../../04-relo-slicer/slice-relos.py;
#   - 判读对象:环境变量 OBJ,默认 demo.bpf.o(不在场先 make);
#   - 只编译只切片:不加载 BPF 程序、不写源码树、对内核零触碰。
#
# 【输出结构】
#   - 逐项一行:PASS <描述> 或 FAIL <描述>(附原因);
#   - 末尾固定汇总:TOTAL=8 PASS=<n> FAIL=<n> OBJ=<对象> SLICER=<切片器>
#     (TOTAL/PASS/FAIL 口径同 scripts/verify-citations.sh);
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 定长)、sed、python3(切片器本体);
#   - python3 缺失:启动即报错退出(1);切片器/对象指错或缺失:逐项 FAIL,
#     汇总后 exit 1(即负路径:不存在对象 / 缺切片器 → exit 1)。
#
# 【失败模式】
#   1. 对象不在场:先在练习目录跑 make(两步链见 Makefile 头注);
#   2. 切片器退出码非 0:按切片器自己的 FAIL 行修(缺 .BTF.ext 多半是漏 -g);
#   3. kind 名未命中:relo-demo.bpf.c 被改动导致 kind 集合变化——对照
#      uapi bpf.h:7262-7276(13 种)同步更新本脚本与 README 判读表。
set -u
cd "$(dirname "$0")"    # 相对路径一律以脚本所在目录为基准

SLICER="${SLICER:-../../04-relo-slicer/slice-relos.py}"
OBJ="${OBJ:-demo.bpf.o}"

total=0
pass=0
fail=0

pass_row() { printf 'PASS %s\n' "$1"; pass=$((pass + 1)); }
fail_row() { printf 'FAIL %s(%s)\n' "$1" "$2"; fail=$((fail + 1)); }

# ---- 第 1 项:python3 依赖 ----
total=$((total + 1))
if command -v python3 >/dev/null 2>&1; then
    pass_row "依赖 python3 在场"
else
    fail_row "依赖 python3 在场" "command -v python3 未命中"
fi

# ---- 第 2 项:切片器在场(负路径:SLICER 指向不存在的路径 → FAIL → exit 1)----
total=$((total + 1))
if [[ -f $SLICER ]]; then
    pass_row "切片器在场($SLICER)"
else
    fail_row "切片器在场" "文件不存在:$SLICER"
fi

# ---- 第 3 项:判读对象在场(负路径:OBJ 指向不存在的对象 → FAIL → exit 1)----
total=$((total + 1))
if [[ -f $OBJ ]]; then
    pass_row "判读对象在场($OBJ)"
else
    fail_row "判读对象在场" "文件不存在:$OBJ(先在本目录跑 make)"
fi

# ---- 第 4-8 项:实跑切片器并逐项断言(前置项有 FAIL 则跳过实跑)----
out=''
rc=0
if [[ -f $SLICER && -f $OBJ ]] && command -v python3 >/dev/null 2>&1; then
    out=$(python3 "$SLICER" "$OBJ" 2>&1)
    rc=$?
fi

total=$((total + 1))
if [[ $rc -eq 0 && -n $out ]]; then
    pass_row "切片器实跑退出码 0(五站走完)"
elif [[ -z $out ]]; then
    fail_row "切片器实跑退出码 0" "前置项有 FAIL,切片器未实跑"
else
    fail_row "切片器实跑退出码 0" "退出码 $rc(FAIL 行见上方切片器输出)"
fi

total=$((total + 1))
relos=$(printf '%s\n' "$out" | sed -n 's/^RELOS=//p' | head -1)
if [[ $relos =~ ^[0-9]+$ ]] && (( relos >= 3 )); then
    pass_row "记录数 ≥3(实测 RELOS=$relos)"
else
    fail_row "记录数 ≥3" "RELOS 行缺失或数值不足(实测:${relos:-无})"
fi

# 三种目标 kind 的 uapi 枚举名逐个在场(与 relo-demo.bpf.c 的 kind 注释对应;
# 名称表权威来源:uapi bpf.h:7262-7276,13 种)
total=$((total + 1))
if printf '%s\n' "$out" | grep -F -q 'BPF_CORE_FIELD_BYTE_OFFSET'; then
    pass_row "kind 0(BPF_CORE_FIELD_BYTE_OFFSET,字段字节偏移)在场"
else
    fail_row "kind 0(BPF_CORE_FIELD_BYTE_OFFSET)在场" "切片输出未含该枚举名"
fi
total=$((total + 1))
if printf '%s\n' "$out" | grep -F -q 'BPF_CORE_FIELD_EXISTS'; then
    pass_row "kind 2(BPF_CORE_FIELD_EXISTS,字段存在)在场"
else
    fail_row "kind 2(BPF_CORE_FIELD_EXISTS)在场" "切片输出未含该枚举名"
fi
total=$((total + 1))
if printf '%s\n' "$out" | grep -F -q 'BPF_CORE_TYPE_SIZE'; then
    pass_row "kind 9(BPF_CORE_TYPE_SIZE,类型大小)在场"
else
    fail_row "kind 9(BPF_CORE_TYPE_SIZE)在场" "切片输出未含该枚举名"
fi

printf 'TOTAL=%d PASS=%d FAIL=%d OBJ=%s SLICER=%s\n' \
    "$total" "$pass" "$fail" "$OBJ" "$SLICER"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

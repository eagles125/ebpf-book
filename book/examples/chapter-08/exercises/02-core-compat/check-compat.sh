#!/usr/bin/env bash
# check-compat.sh —— 练习②「跨版本兼容分支」机检(编译级 + 锚点两级断言)
#
# 【脚本用途与输入】
# 两级断言本练习的参考实现:
#   ①编译级:实跑 make(两步链见 Makefile 头注),clang -g -target bpf
#     零告警产出 compat.bpf.o;
#   ②机检锚点:grep -F 断言 compat.bpf.c 里两个判定宏
#     (bpf_core_field_exists / bpf_core_type_exists)与教案点注释锚点
#     (字段存在≠语义一致)在场。
#   - 源文件:环境变量 COMPAT_SRC,默认 compat.bpf.c;
#   - 只编译:不加载 BPF 程序、不写源码树、对内核零触碰。
#
# 【输出结构】
#   - 逐项一行:PASS <描述> 或 FAIL <描述>(附原因);
#   - 末尾固定汇总:TOTAL=6 PASS=<n> FAIL=<n> SRC=<源文件>
#     (TOTAL/PASS/FAIL 口径同 scripts/verify-citations.sh);
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 定长)、make、bpftool(经 Makefile)、clang(经 Makefile);
#   - make/编译器缺失:编译级两项 FAIL,汇总后 exit 1
#     (即负路径:CLANG 指到不存在的可执行 → exit 1)。
#
# 【失败模式】
#   1. 锚点未命中:compat.bpf.c 被改动丢了宏或注释——按 README 参考思路补回;
#   2. 编译失败:先看 make 透传的诊断;探测字段/类型不在本机 vmlinux.h 时
#      clang 报 no member/unknown type,按「先看本机 BTF 再写代码」换探测
#      对象(README 提示节有选法);
#   3. CLANG/BPFTOOL 经环境变量传给 make,可指向别的工具链做对照。
set -u
cd "$(dirname "$0")"    # 相对路径一律以脚本所在目录为基准

COMPAT_SRC="${COMPAT_SRC:-compat.bpf.c}"

total=0
pass=0
fail=0

pass_row() { printf 'PASS %s\n' "$1"; pass=$((pass + 1)); }
fail_row() { printf 'FAIL %s(%s)\n' "$1" "$2"; fail=$((fail + 1)); }

# ---- 第 1 项:源文件在场(负路径:COMPAT_SRC 指向不存在的文件 → FAIL → exit 1)----
total=$((total + 1))
if [[ -f $COMPAT_SRC ]]; then
    pass_row "源文件在场($COMPAT_SRC)"
else
    fail_row "源文件在场" "文件不存在:$COMPAT_SRC"
fi

# ---- 第 2-4 项:机检锚点(两个判定宏 + 教案点注释;按符号不按行号)----
total=$((total + 1))
if [[ -f $COMPAT_SRC ]] && grep -F -q 'bpf_core_field_exists(' "$COMPAT_SRC"; then
    pass_row "宏锚点 bpf_core_field_exists( 在场(bpf_core_read.h:132,v6.6.78)"
else
    fail_row "宏锚点 bpf_core_field_exists( 在场" "grep -F 未命中"
fi
total=$((total + 1))
if [[ -f $COMPAT_SRC ]] && grep -F -q 'bpf_core_type_exists(' "$COMPAT_SRC"; then
    pass_row "宏锚点 bpf_core_type_exists( 在场(bpf_core_read.h:185,v6.6.78)"
else
    fail_row "宏锚点 bpf_core_type_exists( 在场" "grep -F 未命中"
fi
total=$((total + 1))
if [[ -f $COMPAT_SRC ]] && grep -F -q '字段存在≠语义一致' "$COMPAT_SRC"; then
    pass_row "教案点注释锚点「字段存在≠语义一致」在场"
else
    fail_row "教案点注释锚点「字段存在≠语义一致」在场" "grep -F 未命中"
fi

# ---- 第 5-6 项:编译级(make clean all 全链实跑退出码 0 + 产物在场)----
out=''
rc=-1   # -1 = 未实跑(源文件缺席),与「跑了但失败」区分开
if [[ -f $COMPAT_SRC ]]; then
    out=$(make --no-print-directory clean all 2>&1)   # clean:强制重编, clang 必须真跑一遍
    rc=$?
fi

total=$((total + 1))
if [[ $rc -eq 0 ]]; then
    pass_row "make 编译级通过(退出码 0,输出见下)"
    printf '%s\n' "$out" | sed 's/^/      /'
elif [[ $rc -eq -1 ]]; then
    fail_row "make 编译级通过" "源文件缺席,未实跑"
else
    fail_row "make 编译级通过" "退出码 $rc(诊断见下)"
    printf '%s\n' "$out" | sed 's/^/      /'
fi

total=$((total + 1))
if [[ $rc -eq 0 && -f compat.bpf.o ]]; then
    pass_row "产物 compat.bpf.o 在场"
else
    fail_row "产物 compat.bpf.o 在场" "编译未成功或产物缺失"
fi

printf 'TOTAL=%d PASS=%d FAIL=%d SRC=%s\n' "$total" "$pass" "$fail" "$COMPAT_SRC"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

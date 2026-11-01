#!/usr/bin/env bash
# check-answer.sh —— 练习①「字节码回读」机检(对反汇编输出断言五个特征序列)
#
# 【脚本用途与输入】
# 对 01-bpf-asm 锚点程序的反汇编输出断言五个特征——`r0 = 0x2`、`r0 = 0x1`、
# `r0 = 0x0`、`if r1 == 0xdd86`、`exit`——五个全中即代表锚点程序按书稿口径
# 编译出 14 条指令、三条返回路径与 IPv6 比较俱在。无参数。
#   - 断言对象:仓库内 xdp_count.bpf.o 的 llvm-objdump -d 输出
#     (WALK_TREE 不适用——本练习不读内核源码树);
#   - 自足性:先自动 make 01-bpf-asm(.bpf.o 不在或过期都会现场重建),
#     克隆仓库后不手工 make 也能跑;
#   - 只编译不加载,无挂载风险——本脚本不调用 bpf() 系统调用。
#
# 【输出结构】
#   - 逐特征一行:PASS <特征> 或 FAIL <特征>(附原因);
#   - 末尾固定汇总一行:TOTAL=5 PASS=<n> FAIL=<n>(口径同 verify-citations.sh);
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1。
#
# 【依赖命令与降级行为】
#   - 依赖:make、clang(经 make)、llvm-objdump(经 ../01-bpf-asm/disasm.sh)、
#     grep(-F 定长断言)、dirname;
#   - llvm-objdump 或 clang 缺失:在 make/disasm 环节报错,本脚本按 FAIL 计;
#   - 脚本可从任意 cwd 调用(全部路径相对脚本自身定位)。
#
# 【失败模式】
#   1. make 失败:clang 缺失或版本过旧(无 -target bpf 支持)——FAIL 并提示
#      对齐第 6 章 00-env 自检的工具链;
#   2. 特征未命中:clang 大版本变化改变了指令排布/常量折叠(如 0 值 mov
#      优化为 xor、立即数形式变化)——本章锚点与答案绑定 clang 18.1.3,
#      先 `clang --version` 对齐版本,再比对答案;
#   3. disasm 输出为空:上一步 make 产出的 .o 异常——重跑 make 看编译报错。
set -u

total=0
pass=0
fail=0

# 相对脚本自身定位锚点目录,与调用 cwd 无关
here="$(cd "$(dirname "$0")" && pwd)" || exit 1
asm_dir="$here/../../01-bpf-asm"

check() { # $1 特征文本
    total=$((total + 1))
    if printf '%s\n' "$disasm" | grep -F -q -- "$1"; then
        printf 'PASS %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL %s(反汇编输出中未命中)\n' "$1"
        fail=$((fail + 1))
    fi
}

# 依赖粗检:缺 make 就没有自足性可言
if ! command -v make >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 make,无法自足编译\n' >&2
    exit 1
fi
if ! command -v grep >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 grep,无法断言\n' >&2
    exit 1
fi

# 自足编译:现场 make(.bpf.o 已在且新于源码时 make 无操作)
if ! make -C "$asm_dir" >/dev/null 2>&1; then
    printf 'FAIL make 失败:检查 clang(需 -target bpf 支持,锚点绑 18.1.3)与 uapi 头\n' >&2
    make -C "$asm_dir" >&2   # 重放一遍,把错误暴露给用户
    exit 1
fi

# 反汇编(复用 01-bpf-asm 的封装;其内部校验 llvm-objdump 在场)
if ! disasm="$(bash "$asm_dir/disasm.sh" 2>&1)"; then
    printf 'FAIL 反汇编失败:%s\n' "$disasm" >&2
    exit 1
fi

# 五个特征序列(与 answer.md / 01-bpf-asm README 对照表逐字一致)
check 'r0 = 0x2'
check 'r0 = 0x1'
check 'r0 = 0x0'
check 'if r1 == 0xdd86'
check 'exit'

printf 'TOTAL=%d PASS=%d FAIL=%d\n' "$total" "$pass" "$fail"

if [[ $fail -ne 0 ]]; then
    printf '(提示:指令排布随 clang 大版本漂移;先对齐 clang 18.1.3 再比对 answer.md)\n'
    exit 1
fi
exit 0

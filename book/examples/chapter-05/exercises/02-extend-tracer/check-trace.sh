#!/usr/bin/env bash
# check-trace.sh —— 练习②切片器参考实现的自检脚本
#
# 【脚本用途与输入】
# 对同目录 trace-btf-load.sh 做两层断言:①bash -n 语法检查;②实跑并在
# 输出里 grep 三个锚点(case BPF_BTF_LOAD / bpf_btf_load / btf_parse),
# 确认三站切片真实产出。
#   - 走读树:环境变量 WALK_TREE,默认 /work/linux/linux-6.6.78,原样传给
#     被测脚本(练习②的换树实测与负路径即用它);
#   - 被测脚本:按本脚本所在目录解析 trace-btf-load.sh,与调用时 cwd 无关;
#   - 只读源码树,不构建、不加载;脚本与被测脚本对源码树零写入。
#
# 【输出结构】
#   - 每条断言一行:PASS <断言> 或 FAIL <原因>;
#   - 末尾固定汇总一行:
#       TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1(含被测脚本实跑非零退出)。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep、mktemp、rm,无其他外部命令;
#   - 依赖缺失:启动即报错退出(1),绝不产出「假 PASS」;
#   - WALK_TREE 指向不存在/错误的目录:语法断言仍 PASS(语法检查不碰树),
#     实跑断言 FAIL(被测脚本启动即退 1)、三锚点断言随之 FAIL——汇总
#     FAIL≠0,退出码 1,即负路径预期行为。
#
# 【失败模式】
#   1. 被测脚本缺失:与 check-trace.sh 不同目录部署被打断——FAIL 后退出 1;
#   2. 语法断言失败:bash -n 报错——被测脚本本身写坏,先修语法;
#   3. 实跑失败或锚点缺失:被测脚本因树不在(失败模式 1 降级)或锚点漂移
#      (STATION n: NOT FOUND)退出非零/输出缺锚点——回树校准锚点后重跑
#      (处置链同排错④)。
set -u

TREE_VERSION="v6.6.78"
DEFAULT_TREE="/work/linux/linux-6.6.78"

WALK_TREE="${WALK_TREE:-$DEFAULT_TREE}"

for cmd in bash grep mktemp rm; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf 'FAIL 缺少依赖命令 %s,无法自检\n' "$cmd" >&2
        exit 1
    fi
done

here="$(cd "$(dirname "$0")" && pwd)"
if [[ -z $here || ! -d $here ]]; then
    printf 'FAIL 脚本定位失败:无法解析 %s 所在目录\n' "$0" >&2
    exit 1
fi
target="$here/trace-btf-load.sh"

total=0
pass=0
fail=0
note() { # $1 结果 PASS|FAIL $2 文本
    printf '%s %s\n' "$1" "$2"
    total=$((total + 1))
    if [[ $1 == PASS ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
}

# 断言 1:被测脚本在场
if [[ -f $target ]]; then
    note PASS "被测脚本在场:trace-btf-load.sh"
else
    note FAIL "被测脚本缺失:$target"
fi

# 断言 2:bash -n 语法检查(不执行、不碰树)
if bash -n "$target" 2>/dev/null; then
    note PASS "bash -n trace-btf-load.sh(语法断言)"
else
    note FAIL "bash -n trace-btf-load.sh(语法断言):被测脚本语法错误"
fi

# 断言 3:实跑(输出收进临时文件,退出码单列断言)
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
run_rc=0
bash "$target" "$WALK_TREE" >"$out" 2>&1 || run_rc=$?
if [[ $run_rc -eq 0 ]]; then
    note PASS "实跑 trace-btf-load.sh(退出码 0)"
else
    note FAIL "实跑 trace-btf-load.sh:退出码 $run_rc(树不在或存在 NOT FOUND 站,见输出)"
fi

# 断言 4-6:三个锚点在实跑输出中命中(切片真实产出的证据)
for a in 'case BPF_BTF_LOAD' 'bpf_btf_load' 'btf_parse'; do
    if grep -F -q -- "$a" "$out"; then
        note PASS "锚点命中:$a"
    else
        note FAIL "锚点缺失:$a(实跑输出见上方,或重跑:bash $target $WALK_TREE)"
    fi
done

printf 'TOTAL=%d PASS=%d FAIL=%d WALK_TREE=%s TREE_VERSION=%s\n' \
    "$total" "$pass" "$fail" "$WALK_TREE" "$TREE_VERSION"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

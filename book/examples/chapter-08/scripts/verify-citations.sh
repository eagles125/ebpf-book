#!/usr/bin/env bash
# verify-citations.sh —— 第 8 章引用锚点符号机检(BTF 与 CO-RE 机制章的「编译级」等价物)
#
# 【脚本用途与输入】
# 以章级树根的 citations.txt(格式:文件|符号|说明)为唯一数据源,逐行到
# eBPF 走读源码树做只读核对:先查文件是否存在,再用 grep -F 定长匹配符号。
#   - 走读树:环境变量 WALK_TREE,默认 /work/linux/linux-6.6.78;
#   - 数据源:固定取脚本自身位置旁的 ../citations.txt
#     (按脚本所在目录解析,与调用时的 cwd 无关,任意目录运行结果一致);
#   - 只读源码树,不构建、不加载;脚本对源码树零写入。
#
# 【输出结构】
#   - 每条锚点一行:PASS <文件>|<符号> 或 FAIL <原因>;
#   - 末尾固定汇总一行:
#       TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78
#     其中 TREE_VERSION 是书稿锁定的走读树版本常量(非自动探测);若实际
#     树的版本与之不符,机检结果不可信,应先把 WALK_TREE 指对;
#   - 退出码:FAIL=0 → 0;FAIL≠0 → 1,可直接作 CI 门禁。
#
# 【依赖命令与降级行为】
#   - 依赖:bash、grep(-F 定长匹配)、dirname,无其他外部命令;
#   - grep 缺失:启动即报错退出(退出码 1),绝不在缺工具时产出「假 PASS」;
#   - WALK_TREE 指向不存在/错误的目录:每条锚点按「文件不在树」逐行记
#     FAIL,汇总 FAIL=TOTAL,退出码 1。
#
# 【失败模式(三类原因;任何 FAIL 都回修 citations.txt,不改脚本放水)】
#   1. 文件不在树:清单里的文件在 $WALK_TREE 下不存在——树版本不符、
#      WALK_TREE 指错目录,或清单路径拼写错;
#   2. 符号未命中:文件在,但 grep -F 找不到该符号——内核把符号改名、
#      内联或宏展开,或清单写的形态与源码不一致;
#   3. 格式错:清单行不是「文件|符号|说明」三段(以 | 分割字段数≠3,或
#      文件/符号字段为空)——清单行写错,先修清单再重跑。
set -u

TREE_VERSION="v6.6.78"
DEFAULT_TREE="/work/linux/linux-6.6.78"

WALK_TREE="${WALK_TREE:-$DEFAULT_TREE}"

# 数据源:相对脚本定位 ../citations.txt(章级树根),与 cwd 无关
if ! citations_dir="$(cd "$(dirname "$0")/.." && pwd)"; then
    printf 'FAIL 脚本定位失败:无法解析 %s/..\n' "$0" >&2
    exit 1
fi
citations="$citations_dir/citations.txt"
if [[ ! -r $citations ]]; then
    printf 'FAIL 数据源缺失或不可读:%s\n' "$citations" >&2
    exit 1
fi

# 依赖检查:缺 grep 时不做任何核对,直接失败(降级行为见头部注释)
if ! command -v grep >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 grep,无法机检\n' >&2
    exit 1
fi

total=0
pass=0
fail=0
lineno=0

while IFS= read -r line || [[ -n $line ]]; do
    lineno=$((lineno + 1))
    line=${line%$'\r'}                          # 容忍 CRLF 行尾,避免符号尾带 \r
    [[ $line =~ ^[[:space:]]*$ ]] && continue   # 跳过空行
    [[ $line =~ ^[[:space:]]*# ]] && continue   # 跳过 # 注释行

    total=$((total + 1))

    # 字段校验:以 | 分割必须恰好 3 段(即恰好 2 个分隔符)
    pipes=${line//[^|]/}
    if [[ ${#pipes} -ne 2 ]]; then
        printf 'FAIL 格式错(第%d行,字段数≠3):%s\n' "$lineno" "$line"
        fail=$((fail + 1))
        continue
    fi
    IFS='|' read -r cfile csym _desc <<< "$line"
    if [[ -z $cfile || -z $csym ]]; then
        printf 'FAIL 格式错(第%d行,文件或符号字段为空):%s\n' "$lineno" "$line"
        fail=$((fail + 1))
        continue
    fi

    # 检查 1:文件必须在走读树中存在(只读测试)
    if [[ ! -f $WALK_TREE/$cfile ]]; then
        printf 'FAIL 文件不在树:%s|%s\n' "$cfile" "$csym"
        fail=$((fail + 1))
        continue
    fi

    # 检查 2:grep -F 定长匹配符号(-- 防止以 - 开头的符号被当选项)
    if grep -F -q -- "$csym" "$WALK_TREE/$cfile"; then
        printf 'PASS %s|%s\n' "$cfile" "$csym"
        pass=$((pass + 1))
    else
        printf 'FAIL 符号未命中:%s|%s\n' "$cfile" "$csym"
        fail=$((fail + 1))
    fi
done < "$citations"

printf 'TOTAL=%d PASS=%d FAIL=%d WALK_TREE=%s TREE_VERSION=%s\n' \
    "$total" "$pass" "$fail" "$WALK_TREE" "$TREE_VERSION"

if [[ $fail -ne 0 ]]; then
    exit 1
fi
exit 0

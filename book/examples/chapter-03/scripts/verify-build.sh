#!/usr/bin/env bash
# 第 3 章编译级验证：00-env 自检 + 01-pkt-parse 全链 + 画廊七对（bad/fixed 都必须编译通过）
#                    + exercises 逐目录 make 汇总。
# 无特权可执行；退出码 0=全绿，1=存在 FAIL。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-03
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
pass=0; fail=0

run() {
    local name="$1"; shift
    if "$@" >"$LOG" 2>&1; then
        echo "PASS  $name"; pass=$((pass+1))
    else
        echo "FAIL  $name"; sed -n '1,30p' "$LOG"; fail=$((fail+1))
    fi
}

echo "== BTF 前置 =="
if bpftool btf dump file /sys/kernel/btf/vmlinux format c >/dev/null 2>&1; then
    echo "OK    vmlinux BTF 可读"
else
    echo "FAIL  BTF 不可读：全部示例无法编译"; exit 1
fi

echo "== 环境自检 =="
run "00-env" bash 00-env/check-env.sh

echo "== 锚点编译（全链） =="
run "01-pkt-parse" make -C 01-pkt-parse clean all

echo "== 失败画廊编译（bad 与 fixed 都必须通过） =="
run "02-fail-gallery/01-map-null"       make -C 02-fail-gallery/01-map-null clean all
run "02-fail-gallery/02-pkt-bounds"     make -C 02-fail-gallery/02-pkt-bounds clean all
run "02-fail-gallery/03-ptr-math"       make -C 02-fail-gallery/03-ptr-math clean all
run "02-fail-gallery/04-stack-uninit"   make -C 02-fail-gallery/04-stack-uninit clean all
run "02-fail-gallery/05-loop"           make -C 02-fail-gallery/05-loop clean all
run "02-fail-gallery/06-helper-misuse"  make -C 02-fail-gallery/06-helper-misuse clean all
run "02-fail-gallery/07-kernel-ptr"     make -C 02-fail-gallery/07-kernel-ptr clean all

echo "== 练习编译 =="
run "exercises/01-fix-challenge" make -C exercises/01-fix-challenge clean all
run "exercises/02-log-diagnosis" make -C exercises/02-log-diagnosis clean all

echo
echo "== 汇总: PASS=$pass FAIL=$fail =="
[ "$fail" -eq 0 ]

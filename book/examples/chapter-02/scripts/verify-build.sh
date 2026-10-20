#!/usr/bin/env bash
# 第 2 章编译级验证：00-env 自检 + 01-trace-stat + exercises 逐目录 make 汇总。
# 无特权可执行；退出码 0=全绿，1=存在 FAIL。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-02
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

echo "== 示例编译 =="
run "01-trace-stat" make -C 01-trace-stat clean all

echo "== 练习编译 =="
run "exercises/01-ringbuf-events" make -C exercises/01-ringbuf-events clean all
run "exercises/02-lru-top-proc"   make -C exercises/02-lru-top-proc clean all

echo
echo "== 汇总: PASS=$pass FAIL=$fail =="
[ "$fail" -eq 0 ]

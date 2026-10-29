#!/usr/bin/env bash
# 第 4 章编译级验证：类型画廊与锚点全链——00-env 自检 + 两集成锚点（01-multi-observe、
#                    02-xdp-tc）全链 + 类型画廊 8 目录（compile-only）+ exercises 两目录
#                    逐目录 make 汇总，共 13 项。
# 无特权可执行；退出码 0=全绿，1=存在 FAIL。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-04
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

echo "== 集成锚点编译（全链） =="
run "01-multi-observe" make -C 01-multi-observe clean all
run "02-xdp-tc"        make -C 02-xdp-tc clean all

echo "== 类型画廊编译（compile-only） =="
run "03-type-gallery/01-kretprobe"     make -C 03-type-gallery/01-kretprobe clean all
run "03-type-gallery/02-uprobe"        make -C 03-type-gallery/02-uprobe clean all
run "03-type-gallery/03-cgroup-skb"    make -C 03-type-gallery/03-cgroup-skb clean all
run "03-type-gallery/04-socket-filter" make -C 03-type-gallery/04-socket-filter clean all
run "03-type-gallery/05-sockops"       make -C 03-type-gallery/05-sockops clean all
run "03-type-gallery/06-sk-msg"        make -C 03-type-gallery/06-sk-msg clean all
run "03-type-gallery/07-lsm"           make -C 03-type-gallery/07-lsm clean all
run "03-type-gallery/08-netfilter"     make -C 03-type-gallery/08-netfilter clean all

echo "== 练习编译 =="
run "exercises/01-type-selection" make -C exercises/01-type-selection clean all
run "exercises/02-type-mismatch"  make -C exercises/02-type-mismatch clean all

echo
echo "== 汇总: PASS=$pass FAIL=$fail =="
[ "$fail" -eq 0 ]

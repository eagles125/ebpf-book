#!/usr/bin/env bash
# 顶层编译级验证：逐阶段 make + exercises 编译 + 汇总。无特权可执行。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 退出码：0=全绿；1=存在 FAIL；Go 网络依赖失败记 WARN（不算失败，需人工复验）。
set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-17
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
pass=0; fail=0; warn=0

run() {
    local name="$1"; shift
    if [ ! -d "$name" ]; then
        echo "SKIP  $name（目录未创建）"; return
    fi
    if "$@" >"$LOG" 2>&1; then
        echo "PASS  $name"; pass=$((pass+1))
    elif grep -qE 'module lookup|dial tcp|connection refused|could not resolve|no required module' "$LOG"; then
        echo "WARN  $name（Go 依赖拉取失败：需网络 go mod tidy，编译级待验证）"; warn=$((warn+1))
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

echo "== 阶段编译 =="
for s in 01-xdp-drop 02-parse-acl 03-conntrack 04-nat-tc 05-firewall; do
    run "$s" make -C "$s" clean all
done

echo "== 练习编译 =="
for e in exercises/01-ipv6-acl exercises/02-rule-persistence \
         exercises/03-prometheus-export exercises/04-port-rate-limit; do
    run "$e" make -C "$e" clean all
done

echo
echo "== 汇总: PASS=$pass WARN=$warn FAIL=$fail =="
[ "$fail" -eq 0 ]

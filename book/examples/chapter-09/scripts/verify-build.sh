#!/usr/bin/env bash
# 第 9 章编译级验证：00-env 自检 + 示例 01–08 全链 make + 09-netfilter-go（内核 6.6+ 门禁）汇总。
# 无特权可执行；退出码 0=无 FAIL，1=存在 FAIL（WARN/SKIP 不算失败，需人工复验）。
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-09
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
pass=0; fail=0; warn=0

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
# check-env.sh 尾部 exit "$fail"（0=无 FAIL，1=存在 FAIL，与 ch02–04 同约定）；
# 本机检仍以输出中的 FAIL 行 grep 兜底判定（WARN 行不算），退出码仅作辅助。
bash 00-env/check-env.sh >"$LOG" 2>&1
if grep -q '^FAIL  ' "$LOG"; then
    echo "FAIL  00-env"; sed -n '1,30p' "$LOG"; fail=$((fail+1))
else
    echo "PASS  00-env"; pass=$((pass+1))
fi

echo "== 示例编译（01–08，libbpf-bootstrap 全链） =="
run "01-hello-world"          make -C 01-hello-world clean all
run "02-pid-filter"           make -C 02-pid-filter clean all
run "03-container-security"   make -C 03-container-security clean all
run "04-ring-buffer"          make -C 04-ring-buffer clean all
run "05-open-file-kprobe"     make -C 05-open-file-kprobe clean all
run "06-open-file-perf-event" make -C 06-open-file-perf-event clean all
run "07-open-file-return"     make -C 07-open-file-return clean all
run "08-open-file-tracepoint" make -C 08-open-file-tracepoint clean all

echo "== 09-netfilter-go（需内核 6.6+） =="
# 门禁为本脚本的精确判定：主版本 >6，或 =6 且次版本 >=6（即 6.6+）。
# 00-env/check-env.sh 内核段仅按主版本行内提示——6.0–6.5 内核上两者判定会分歧，以这里为准。
# 不满足记 SKIP（不算失败）；Go 依赖拉取失败记 WARN（ch17 口径：需网络 go mod tidy，
# 编译级待人工复验）；其余失败照常记 FAIL。
kmaj=$(uname -r | cut -d. -f1); kmin=$(uname -r | cut -d. -f2)
if [ "${kmaj:-0}" -gt 6 ] || { [ "${kmaj:-0}" -eq 6 ] && [ "${kmin:-0}" -ge 6 ]; }; then
    if [ ! -f 09-netfilter-go/go.sum ]; then
        if (cd 09-netfilter-go && go mod tidy) >"$LOG" 2>&1; then
            echo "OK    go mod tidy（生成 go.sum，依赖就绪）"
        else
            echo "WARN  09-netfilter-go（Go 依赖拉取失败：需网络 go mod tidy，编译级待验证）"
            warn=$((warn+1))
        fi
    fi
    if [ -f 09-netfilter-go/go.sum ]; then
        if make -C 09-netfilter-go clean all >"$LOG" 2>&1; then
            echo "PASS  09-netfilter-go"; pass=$((pass+1))
        elif grep -qE 'module lookup|dial tcp|connection refused|could not resolve|no required module' "$LOG"; then
            echo "WARN  09-netfilter-go（Go 依赖拉取失败：需网络 go mod tidy，编译级待验证）"
            warn=$((warn+1))
        else
            echo "FAIL  09-netfilter-go"; sed -n '1,30p' "$LOG"; fail=$((fail+1))
        fi
    fi
else
    echo "SKIP  09-netfilter-go（内核 <6.6，Netfilter 示例编译级待验证）"
fi

echo
echo "== 汇总: PASS=$pass WARN=$warn FAIL=$fail =="
[ "$fail" -eq 0 ]

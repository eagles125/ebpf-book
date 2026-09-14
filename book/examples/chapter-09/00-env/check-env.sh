#!/usr/bin/env bash
# 第 9 章示例环境自检脚本（对应 ebpf.top BPF 入门系列：开发环境搭建）。
# 用法：bash check-env.sh
# 输出各项依赖的检查结果；FAIL 项表示对应示例可能无法编译或运行。

set -u

fail=0

check() {
    # check <名称> <检查命令...>
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        printf 'OK    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        fail=1
    fi
}

echo "== 内核 =="
printf 'kernel: %s\n' "$(uname -r)"

kver_major="$(uname -r | cut -d. -f1)"
if [ "${kver_major:-0}" -ge 6 ]; then
    echo 'OK    内核版本 >= 6.x，满足全部示例要求（Netfilter 示例需 6.6+）'
elif [ "${kver_major:-0}" -ge 5 ]; then
    echo 'OK    内核版本 5.x，满足示例 01-08（09-netfilter-go 需 6.6+）'
else
    echo 'WARN  内核版本低于 5.10，多数示例可能不受支持'
fi

echo "== BTF / CO-RE =="
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用）'
else
    echo 'FAIL  /sys/kernel/btf/vmlinux 缺失：CO-RE 示例无法编译，参见附录 C'
    fail=1
fi

echo "== 工具链 =="
check 'clang（编译 BPF C）'        clang --version
check 'gcc（编译用户态 loader）'   gcc --version
check 'bpftool（BTF dump / skeleton）' bpftool version
check 'make'                        make --version
check 'libbpf 头文件（libbpf-dev）' test -f /usr/include/bpf/bpf_helpers.h
check 'Go（仅 09-netfilter-go 需要）' go version

echo "== 运行时环境 =="
if [ -d /sys/kernel/tracing/events ]; then
    echo 'OK    tracefs 已挂载于 /sys/kernel/tracing'
else
    echo 'WARN  tracefs 未挂载，可执行: sudo mount -t tracefs tracefs /sys/kernel/tracing'
fi

if [ "$(id -u)" -eq 0 ]; then
    echo 'OK    当前为 root，可直接运行 loader'
else
    echo 'WARN  当前非 root：示例可编译，但运行 loader 需要 sudo 或 CAP_BPF/CAP_PERFMON 等能力'
fi

echo "== 汇总 =="
if [ "$fail" -eq 0 ]; then
    echo '环境满足第 9 章示例的编译要求。'
else
    echo '存在 FAIL 项：请先补齐对应依赖（Ubuntu/Debian 可参考: sudo apt install clang llvm gcc bpftool libbpf-dev make）。'
fi

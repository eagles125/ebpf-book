#!/usr/bin/env bash
# 第 3 章示例环境自检（分级诊断）。
# 用法：bash check-env.sh（无 root 也可运行，按「编译级/加载级/运行级」三档给出结论）
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只读检查，不做任何加载。

set -u
fail=0

check() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        printf 'OK    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        fail=1
    fi
}

echo "== 内核与 BTF =="
printf 'kernel: %s\n' "$(uname -r)"
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用，CO-RE 可用）'
else
    echo 'FAIL  内核 BTF 缺失：全部示例无法编译，参见附录 C'
    fail=1
fi

echo "== 工具链（编译级） =="
check 'clang（编译 BPF C）'              clang --version
check 'gcc（编译用户态 loader）'          gcc --version
check 'bpftool（BTF dump / skeleton）'   bpftool version
check 'make'                             make --version
check 'libbpf 头文件（libbpf-dev）'       test -f /usr/include/bpf/bpf_helpers.h

echo "== 权限分级（加载级/运行级） =="
if [ "$(id -u)" -eq 0 ]; then
    echo 'OK    当前为 root：可加载、可挂载 XDP 与 tracepoint'
elif capsh --print 2>/dev/null | grep -q 'cap_bpf'; then
    echo 'OK    具备 CAP_BPF：可加载程序，挂载 XDP 还需 CAP_NET_ADMIN'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo 未知)"
    echo "WARN  非 root 且无 CAP_BPF（unprivileged_bpf_disabled=${ubd}）："
    echo '      - 可以完成：编译级验证（make / scripts/verify-build.sh）'
    echo '      - 不能完成：加载与挂载 XDP/tracepoint、bpftool prog/map 观察（需 sudo）'
fi
if ip link show lo >/dev/null 2>&1; then
    echo 'OK    lo 接口存在（XDP 锚点程序的推荐挂载目标）'
else
    echo 'WARN  lo 接口不可见（XDP 挂载目标缺失，编译不受影响）'
fi

echo
if [ "$fail" -eq 0 ]; then
    echo '结论：编译级依赖齐备；加载级/运行级能力见上方权限分级输出。'
else
    echo '结论：存在 FAIL 项，先补齐依赖（Ubuntu: sudo apt install clang llvm gcc bpftool libbpf-dev make）。'
fi
exit "$fail"

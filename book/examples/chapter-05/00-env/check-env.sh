#!/usr/bin/env bash
# 第 5 章示例环境自检(分级诊断)——源码走读章的「编译级」自检等价物。
#
# 【用途与输入】
#   检查本机是否具备《第 5 章 eBPF 子系统内核代码结构》全部走读资产的前置条件:
#   走读树(Linux 源码树)、机检与切片工具、内核 BTF、权限档位。
#   输入:环境变量 WALK_TREE(默认 /work/linux/linux-6.6.78),可用
#   `WALK_TREE=<你的源码树路径> bash check-env.sh` 覆盖;不接受位置参数。
#
# 【输出结构】
#   四个检查区 + 末尾三档结论,正文「四、实战」第一层逐行引用本输出作示例:
#   ①走读树 ②工具链 ③内核 BTF ④权限档位;
#   结论按「编译级 / 机检级(脚本与机检) / 加载级与特权查询(bpftool prog show 等)」三档给出。
#
# 【依赖命令与降级行为】
#   grep 为本脚本自身硬依赖(缺失直接报错退出)——它同时是全部机检/切片脚本的硬依赖;
#   sed/wc 为机检级软依赖(切片器区间提取与行数统计),缺失只打 WARN 降级提示;
#   bpftool 为可选依赖:缺失仅影响正文四节 introspection 预期输出
#   (bpftool version / feature probe / prog show 等),机检与切片不受影响。
#   本脚本只读源码树与 /sys、/proc、/boot 下的系统文件,不构建、不加载。
#
# 【失败模式(退出码)】
#   2  走读树不存在或不是有效源码树(硬性阻断)——打印 kernel.org 获取指引后退出;
#   1  树在但关键文件缺失(kernel/bpf/syscall.c 或 verifier.c 不在场,树不完整);
#   0  树与关键文件齐备(工具缺失/BTF 缺失/非 root 均只降级提示,不改变退出码)。

set -u

# ---- grep:本脚本与全部机检/切片脚本的硬依赖,缺失无法继续 ----
if ! command -v grep >/dev/null 2>&1; then
    echo 'FATAL  缺少 grep:本脚本与 verify-citations.sh、两个切片器都以 grep 为硬依赖,请先安装后重试。'
    exit 1
fi

fail=0   # 树级失败计数(关键文件缺失 -> exit 1);工具/BTF/权限缺项只降级,不计数

echo "== ① 走读树(本章机检与切片的唯一数据源) =="
WALK_TREE="${WALK_TREE:-/work/linux/linux-6.6.78}"
if [ ! -f "$WALK_TREE/Makefile" ]; then
    echo "FAIL  走读树不可用:$WALK_TREE(未找到 Makefile)"
    echo '       本章全部机检与切片脚本以该树为数据源,树缺失属于硬性阻断。'
    echo '       从 kernel.org 获取 Linux 源码树(两种方式任选):'
    echo '         1) git clone(浅克隆指定 tag,可换成任意 v6.6.x):'
    echo '            git clone --branch v6.6.78 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-6.6.78'
    echo '         2) tar 包(直接下载稳定版源码):'
    echo '            curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.78.tar.xz'
    echo '            tar xf linux-6.6.78.tar.xz'
    echo '       拿到树后用 WALK_TREE=<树的路径> bash check-env.sh 重新自检。'
    exit 2
fi
echo "OK    WALK_TREE=$WALK_TREE(可用 WALK_TREE=<路径> 换树自检)"

# 从 Makefile 识别树版本(纯 bash 提取,不依赖 sed/awk):
# 正文所有行号引用一律标注该版本(v6.6.78)并注明随版本漂移。
v="$(grep -m1 '^VERSION'    "$WALK_TREE/Makefile")"
p="$(grep -m1 '^PATCHLEVEL' "$WALK_TREE/Makefile")"
s="$(grep -m1 '^SUBLEVEL'   "$WALK_TREE/Makefile")"
v="${v##*=}"; v="${v// /}"; p="${p##*=}"; p="${p// /}"; s="${s##*=}"; s="${s// /}"
tree_ver="未知"
if [ -n "$v" ] && [ -n "$p" ] && [ -n "$s" ]; then
    tree_ver="v$v.$p.$s"
    echo "OK    树版本:$tree_ver(VERSION=$v PATCHLEVEL=$p SUBLEVEL=$s)"
else
    echo 'WARN  Makefile 在场但未识别出 VERSION/PATCHLEVEL/SUBLEVEL 行:树版本未知,行号引用慎标(随版本漂移)'
fi

# 本章两块主基石:系统调用入口所在(kernel/bpf/syscall.c)与验证器所在(kernel/bpf/verifier.c)。
# wc 为软依赖:缺失时略过行数统计,只降级不判 FAIL。
for f in kernel/bpf/syscall.c kernel/bpf/verifier.c; do
    if [ -f "$WALK_TREE/$f" ]; then
        if command -v wc >/dev/null 2>&1; then
            echo "OK    $f 在场($(wc -l < "$WALK_TREE/$f") 行)"
        else
            echo "OK    $f 在场(wc 缺失,略过行数统计)"
        fi
    else
        echo "FAIL  $f 不在场:树不完整或版本差异过大,正文二/三节走读无从对照"
        fail=1
    fi
done
echo

echo "== ② 工具链(机检级依赖;缺失按降级提示处理) =="
# sed/wc:切片器(sed -n 区间提取、grep -n 定位后配 wc -l 看规模)的软依赖
for t in sed wc; do
    if command -v "$t" >/dev/null 2>&1; then
        echo "OK    $t(切片器与行数统计依赖)"
    else
        echo "WARN  $t 缺失:切片器与行数统计降级,走读树本身不受影响(安装系统包 sed/coreutils)"
    fi
done
# bpftool:仅服务正文四节 introspection 示例(均属特权查询档)。
# Debian/Ubuntu 装在 /usr/sbin,个别非 root PATH 不含它,故补一个常见路径回退。
bpftool_bin=""
if command -v bpftool >/dev/null 2>&1; then
    bpftool_bin="$(command -v bpftool)"
elif [ -x /usr/sbin/bpftool ]; then
    bpftool_bin=/usr/sbin/bpftool
fi
if [ -n "$bpftool_bin" ]; then
    bv="$("$bpftool_bin" version 2>/dev/null)"; bv="${bv%%$'\n'*}"
    echo "OK    bpftool($bv,位于 $bpftool_bin)——仅服务正文四节 introspection 预期输出,属特权查询档"
else
    echo 'WARN  bpftool 缺失:仅影响正文四节 introspection 预期输出(bpftool version/feature probe 等),机检与切片不受影响'
fi
echo

echo "== ③ 内核 BTF(特权查询档的前提;机检与切片不需要它) =="
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在且可读(运行内核自带 BTF)'
else
    echo 'WARN  /sys/kernel/btf/vmlinux 不可读:机检与切片不受影响;bpftool introspection 类示例的复验需 BTF 在场'
fi
if [ -r "/boot/config-$(uname -r)" ] && grep -q '^CONFIG_DEBUG_INFO_BTF=y' "/boot/config-$(uname -r)"; then
    echo "OK    CONFIG_DEBUG_INFO_BTF=y(/boot/config-$(uname -r)):BTF 与内核构建一致(bpftool btf dump 的前提)"
elif [ -r /proc/config.gz ] && command -v zgrep >/dev/null 2>&1 && zgrep -q '^CONFIG_DEBUG_INFO_BTF=y' /proc/config.gz; then
    echo 'OK    CONFIG_DEBUG_INFO_BTF=y(/proc/config.gz):BTF 与内核构建一致(bpftool btf dump 的前提)'
else
    echo 'WARN  未检出 CONFIG_DEBUG_INFO_BTF=y(/boot/config-* 与 /proc/config.gz 均不可读或未启用):仅影响特权查询档,机检与切片不受影响'
fi
echo

echo "== ④ 权限档位(加载级与特权查询的前提) =="
uid="$(id -u)"
if [ "$uid" -eq 0 ]; then
    echo 'OK    当前为 root:三档全部可真实验证'
else
    echo "OK    当前为非特权用户(uid=$uid):编译级与机检级可真实验证——本章无 BPF 程序可编译,脚本机检即本章的编译级等价物"
fi
ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo 未知)"
case "$ubd" in
    0) echo 'OK    kernel.unprivileged_bpf_disabled=0:允许非特权加载(eBPF 程序可无特权加载)' ;;
    1) echo 'INFO  kernel.unprivileged_bpf_disabled=1:非特权加载已禁用(root 可运行时改回 0)' ;;
    2) echo 'INFO  kernel.unprivileged_bpf_disabled=2:非特权加载彻底禁用(不可运行时改回)——本机默认档;该开关的内核出处即 kernel/bpf/syscall.c 的 sysctl_unprivileged_bpf_disabled(正文一节走读对象)' ;;
    *) echo "WARN  kernel.unprivileged_bpf_disabled 不可读($ubd):仅影响权限档位说明,不影响机检" ;;
esac
if [ "$uid" -eq 0 ]; then
    echo 'OK    加载级与特权查询(bpftool prog show / feature probe 等):root 档,可真实验证'
else
    echo 'INFO  加载级与特权查询(bpftool prog show / feature probe 等):需要特权,输出为预期示例(运行级待验证)'
fi
echo

echo "== 结论(三档口径,正文「四、实战」逐档引用) =="
if [ "$fail" -ne 0 ]; then
    echo "[1] 编译级             FAIL  走读树关键文件缺失——先按上方 kernel.org 指引补齐树"
    echo "[2] 机检级(脚本与机检) FAIL  树不完整,verify-citations.sh 与切片器无从实跑"
    echo "结论:走读树不完整——本章机检与切片硬性阻断。"
    exit 1
fi
# 机检级工具完备性:sed 与 wc 任一缺失即降级(切片器/行数统计不可用)
tools_ok=1
command -v sed >/dev/null 2>&1 || tools_ok=0
command -v wc  >/dev/null 2>&1 || tools_ok=0
if [ "$tools_ok" -eq 1 ]; then
    echo "[1] 编译级             PASS  走读树 $tree_ver 齐备 + grep/sed/wc 在场(本章无 BPF 程序,树+工具即编译级前提)"
    echo "[2] 机检级(脚本与机检) PASS  check-env.sh 实跑通过;verify-citations.sh 与两个切片器对同一棵树实跑即机检级"
else
    echo "[1] 编译级             PASS  走读树 $tree_ver 齐备(工具缺项见上方 WARN:仅降级,不阻断)"
    echo "[2] 机检级(脚本与机检) WARN  部分机检工具缺失,切片器/行数统计降级"
fi
if [ "$uid" -eq 0 ]; then
    echo '[3] 加载级与特权查询    PASS  root 档:bpftool prog show / feature probe 等可真实验证'
else
    echo '[3] 加载级与特权查询    需要特权,输出为预期示例(运行级待验证):bpftool prog show / feature probe 等需 root'
fi
echo '结论:走读树与机检级前提齐备;加载级与特权查询按权限档位给出(见上)。'
exit 0

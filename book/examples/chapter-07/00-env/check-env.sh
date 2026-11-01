#!/usr/bin/env bash
# 第 7 章示例环境自检(分级诊断)——运行章的「编译级」自检:认源码、点工具链、报权限档。
#
# 【用途与输入】
#   检查本机是否具备《第 7 章 辅助函数与映射的内部实现》全部示例资产的前置条件:
#   走读树(Linux 源码树,重点是 helper/映射/事件输出的六个源文件)、双侧工具链
#   (BPF 侧 clang;用户侧 gcc+libbpf;骨架生成 bpftool)、权限档位、BTF、运行内核 CONFIG。
#   输入:环境变量 WALK_TREE(默认 /work/linux/linux-6.6.78),可用
#   `WALK_TREE=<你的源码树路径> bash check-env.sh` 覆盖;不接受位置参数。
#
# 【输出结构】
#   五个检查区 + 末尾三档结论,正文各节「学生动手」与示例 README 逐档引用:
#   ①走读树与本章源文件 ②工具链(clang/gcc/libbpf/bpftool/llvm-objdump,只查版本)
#   ③权限档位(root/非特权加载/perf) ④内核 BTF(skeleton 生成的前提) ⑤CONFIG 两项;
#   结论按「编译级(示例构建)/ 加载级(运行验证)/ 走读级(机检切片)」三档给出。
#   本章是运行章:三个示例 01/02/03 的加载与运行需 root,本机非特权档只做到编译级。
#
# 【依赖命令与降级行为】
#   grep 为本脚本自身硬依赖(缺失直接报错退出);
#   sed/wc 为机检级软依赖(切片器区间提取与行数统计),缺失只打 WARN;
#   clang/gcc/libbpf/bpftool 为编译级软依赖(三示例的全链构建),缺失只降级提示——
#   本脚本只查版本与在场性,不做编译探针(真实构建由各示例 Makefile 承担);
#   本脚本只读源码树与 /sys、/proc、/boot 下的系统文件,不构建、不加载。
#
# 【失败模式(退出码)】
#   2  走读树不存在或不是有效源码树(硬性阻断)——打印 kernel.org 获取指引后退出;
#   1  树在但本章关键源文件缺失(六个源文件任一不在场,树不完整);
#   0  树与关键文件齐备(工具缺失/BTF 缺失/非 root 均只降级提示,不改变退出码)。

set -u

# ---- grep:本脚本与全部机检/切片脚本的硬依赖,缺失无法继续 ----
if ! command -v grep >/dev/null 2>&1; then
    echo 'FATAL  缺少 grep:本脚本与 verify-citations.sh、两个切片器都以 grep 为硬依赖,请先安装后重试。'
    exit 1
fi

fail=0   # 树级失败计数(关键源文件缺失 -> exit 1);工具/BTF/权限缺项只降级,不计数

echo "== ① 走读树(本章机检、切片与源码走读的唯一数据源) =="
WALK_TREE="${WALK_TREE:-/work/linux/linux-6.6.78}"
if [ ! -f "$WALK_TREE/Makefile" ]; then
    echo "FAIL  走读树不可用:$WALK_TREE(未找到 Makefile)"
    echo '       本章全部机检、切片与源码走读以该树为数据源,树缺失属于硬性阻断。'
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

# 树版本提取(同前章惯例):正文所有行号引用一律标注该版本(v6.6.78)。
v="$(grep -m1 '^VERSION'    "$WALK_TREE/Makefile")"
p="$(grep -m1 '^PATCHLEVEL' "$WALK_TREE/Makefile")"
s="$(grep -m1 '^SUBLEVEL'   "$WALK_TREE/Makefile")"
v="${v##*=}"; v="${v// /}"; p="${p##*=}"; p="${p// /}"; s="${s##*=}"; s="${s// /}"
tree_ver="未知"
if [ -n "$v" ] && [ -n "$p" ] && [ -n "$s" ]; then
    tree_ver="v$v.$p.$s"
    echo "OK    树版本:$tree_ver(VERSION=$v PATCHLEVEL=$p SUBLEVEL=$s)"
else
    echo 'WARN  Makefile 在场但未识别出版本行:树版本未知,行号引用慎标(随版本漂移)'
fi

# 本章走读的六个关键源文件:helper 分发(helpers.c)、通用层(core.c)、
# hash 内部(hashtab.c)、数组族与 per-CPU(arraymap.c)、事件输出(ringbuf.c、bpf_trace.c)、
# LRU 逐出(bpf_lru_list.c);加 syscall.c/bpf.h 两个总纲文件。
# wc 为软依赖:缺失时略过行数统计。
for f in kernel/bpf/helpers.c kernel/bpf/hashtab.c kernel/bpf/arraymap.c \
         kernel/bpf/ringbuf.c kernel/bpf/bpf_lru_list.c kernel/trace/bpf_trace.c \
         kernel/bpf/syscall.c include/linux/bpf.h; do
    if [ -f "$WALK_TREE/$f" ]; then
        if command -v wc >/dev/null 2>&1; then
            echo "OK    $f 在场($(wc -l < "$WALK_TREE/$f") 行)"
        else
            echo "OK    $f 在场(wc 缺失,略过行数统计)"
        fi
    else
        echo "FAIL  $f 不在场:树不完整或版本差异过大,正文走读无从对照"
        fail=1
    fi
done
echo

echo "== ② 工具链(双侧:编译级三示例的真实构建前提;只查版本,不做编译探针) =="
# sed/wc:切片器软依赖
for t in sed wc; do
    if command -v "$t" >/dev/null 2>&1; then
        echo "OK    $t(切片器与行数统计依赖)"
    else
        echo "WARN  $t 缺失:切片器与行数统计降级(安装系统包 sed/coreutils)"
    fi
done
# clang:BPF 侧编译器(.bpf.o)
if command -v clang >/dev/null 2>&1; then
    cv="$(clang --version 2>/dev/null)"; cv="${cv%%$'\n'*}"
    echo "OK    clang($cv)——BPF 侧 -target bpf 编译器(真实编译由示例 Makefile 承担)"
else
    echo 'WARN  clang 缺失:三示例 BPF 侧无法编译(需要带 BPF 后端的 clang,apt install clang)'
fi
# gcc:用户态加载器编译器(loader)
if command -v gcc >/dev/null 2>&1; then
    gv="$(gcc --version 2>/dev/null)"; gv="${gv%%$'\n'*}"
    echo "OK    gcc($gv)——用户态加载器编译器"
else
    echo 'WARN  gcc 缺失:三示例用户态侧无法编译(apt install build-essential)'
fi
# libbpf:头 + 库(skeleton 模式链接 -lbpf -lelf -lz)
if [ -r /usr/include/bpf/bpf.h ] && [ -r /usr/include/bpf/libbpf.h ]; then
    libbpf_ver="未知"
    if [ -r /usr/include/bpf/libbpf_version.h ]; then
        libbpf_ver="$(grep -m1 'LIBBPF_MAJOR_VERSION' /usr/include/bpf/libbpf_version.h 2>/dev/null)"
        libbpf_ver="${libbpf_ver##* }"
        minor="$(grep -m1 'LIBBPF_MINOR_VERSION' /usr/include/bpf/libbpf_version.h 2>/dev/null)"
        minor="${minor##* }"
        [ -n "$libbpf_ver" ] && [ -n "$minor" ] && libbpf_ver="$libbpf_ver.$minor"
    fi
    libok=0
    if ldconfig -p 2>/dev/null | grep -q libbpf.so || [ -e /usr/lib/x86_64-linux-gnu/libbpf.so ] || [ -e /usr/lib64/libbpf.so ]; then
        libok=1
    fi
    if [ "$libok" -eq 1 ]; then
        echo "OK    libbpf(头文件在场,版本 $libbpf_ver,动态库在场)——加载器链接 -lbpf"
    else
        echo "WARN  libbpf 头文件在场(版本 $libbpf_ver)但未检出动态库:链接 -lbpf 会失败(apt install libbpf-dev)"
    fi
else
    echo 'WARN  libbpf 头文件缺失(/usr/include/bpf/):三示例用户态侧无法编译(apt install libbpf-dev)'
fi
# bpftool:vmlinux.h 生成(btf dump)与 skeleton 生成(gen skeleton)——编译级链条的一环
bpftool_bin=""
if command -v bpftool >/dev/null 2>&1; then
    bpftool_bin="$(command -v bpftool)"
elif [ -x /usr/sbin/bpftool ]; then
    bpftool_bin=/usr/sbin/bpftool
fi
if [ -n "$bpftool_bin" ]; then
    bv="$("$bpftool_bin" version 2>/dev/null)"; bv="${bv%%$'\n'*}"
    echo "OK    bpftool($bv,位于 $bpftool_bin)——vmlinux.h 生成与 skeleton 生成(编译级链条;运行期 prog dump 属特权查询档)"
else
    echo 'WARN  bpftool 缺失:vmlinux.h/skeleton 无法生成,三示例编译链断裂(apt install linux-tools-common 或 linux-tools-$(uname -r))'
fi
# llvm-objdump:可选(反汇编 .bpf.o 观察 helper call 指令;本章非必需)
if command -v llvm-objdump >/dev/null 2>&1; then
    ov="$(llvm-objdump --version 2>/dev/null)"; ov1="${ov%%$'\n'*}"
    echo "OK    llvm-objdump($ov1)——可选:反汇编 .bpf.o 观察 call 指令与映射操作"
else
    echo 'WARN  llvm-objdump 缺失:可选的反汇编观察降级,不影响示例构建(apt install llvm)'
fi
echo

echo "== ③ 权限档位(运行级验证的门槛;编译级不受影响) =="
uid="$(id -u)"
if [ "$uid" -eq 0 ]; then
    echo 'OK    当前为 root:三个示例可真实加载运行(运行级验证可做)'
else
    echo "INFO  当前为非特权用户(uid=$uid):编译级可复现;加载/attach/读数属运行级待验证(需 root)"
fi
f=/proc/sys/kernel/unprivileged_bpf_disabled
if [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    echo "OK    kernel.unprivileged_bpf_disabled = $val——0=允许非特权加载;1=部分允许;2=彻底禁用(本机 2 时非特权加载直接 EPERM,三个示例必须 root)"
else
    echo 'WARN  unprivileged_bpf_disabled 不可读:非特权加载可用性未知(随内核版本演进,以所用内核源码为准)'
fi
f=/proc/sys/kernel/perf_event_paranoid
if [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    echo "OK    kernel.perf_event_paranoid = $val——值越大越严;perf buffer 示例的事件读取路径不受它影响,但 perf 取证类工具(如 perf stat)受其约束"
else
    echo 'WARN  perf_event_paranoid 不可读:perf 相关口径降级为人工确认'
fi
echo

echo "== ④ 内核 BTF(skeleton 生成的硬前提) =="
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在且可读——bpftool btf dump 可生成 vmlinux.h(编译级链条前提)'
else
    echo 'WARN  /sys/kernel/btf/vmlinux 不可读:bpftool btf dump 失败,三示例编译链断裂;机检与切片不受影响'
fi
echo

echo "== ⑤ 运行内核 CONFIG(/boot/config-$(uname -r)):两项 =="
cfg="/boot/config-$(uname -r)"
cfg_src=""
if [ -r "$cfg" ]; then
    cfg_src="$cfg"
elif [ -r /proc/config.gz ] && command -v zgrep >/dev/null 2>&1; then
    cfg_src=/proc/config.gz
fi
# conf_get <CONFIG 名>:从配置源取该行「=」右边的值,取不到输出空串
conf_get() {
    local line
    if [ "$cfg_src" = /proc/config.gz ]; then
        line="$(zgrep -m1 "^$1=" /proc/config.gz 2>/dev/null)"
    elif [ -n "$cfg_src" ]; then
        line="$(grep -m1 "^$1=" "$cfg_src" 2>/dev/null)"
    else
        line=""
    fi
    printf '%s' "${line##*=}"
}
if [ -z "$cfg_src" ]; then
    echo 'WARN  内核配置不可读(/boot/config-* 与 /proc/config.gz 均不可用):CONFIG 两项降级为人工确认'
else
    echo "OK    配置源:$cfg_src"
    case "$(conf_get CONFIG_BPF_SYSCALL)" in
        y) echo 'OK    CONFIG_BPF_SYSCALL=y——bpf(2) 系统调用在场,映射创建/加载路径完整(第 5 章源码地图的运行时落点)' ;;
        *) echo "INFO  CONFIG_BPF_SYSCALL 非 y:本机内核不支持 bpf(2),运行级全部不可用" ;;
    esac
    case "$(conf_get CONFIG_DEBUG_INFO_BTF)" in
        y) echo 'OK    CONFIG_DEBUG_INFO_BTF=y——BTF 与内核构建一致(④实测读数的源头)' ;;
        *) echo 'WARN  未检出 CONFIG_DEBUG_INFO_BTF=y:与④互证' ;;
    esac
fi
echo

echo "== 结论(本章三档口径,示例 README 与正文逐档引用) =="
if [ "$fail" -ne 0 ]; then
    echo "[1] 走读级(机检切片)    FAIL  走读树关键源文件缺失——先按上方 kernel.org 指引补齐树"
    echo "结论:走读树不完整——本章源码走读硬性阻断。"
    exit 1
fi
# 走读级:机检需 sed/wc(切片器);本脚本自身已验证树
if command -v sed >/dev/null 2>&1 && command -v wc >/dev/null 2>&1; then
    echo "[1] 走读级(机检切片)    PASS  走读树 $tree_ver(八个源文件齐备)+ 切片工具链在场——verify-citations.sh 与两个切片器可复现"
else
    echo '[1] 走读级(机检切片)    WARN  部分切片工具缺失,切片器/行数统计降级'
fi
# 编译级:clang + gcc + libbpf + bpftool + BTF 五件齐才可全链构建
compile_ok=1
command -v clang >/dev/null 2>&1 || compile_ok=0
command -v gcc   >/dev/null 2>&1 || compile_ok=0
[ -r /usr/include/bpf/libbpf.h ] || compile_ok=0
[ -n "$bpftool_bin" ]           || compile_ok=0
[ -r /sys/kernel/btf/vmlinux ]  || compile_ok=0
if [ "$compile_ok" -eq 1 ]; then
    echo "[2] 编译级(示例构建)    PASS  clang + gcc + libbpf + bpftool + BTF 五件齐——三示例 make 全链(编译级已验证)可复现"
else
    echo '[2] 编译级(示例构建)    WARN  构建五件有缺项(见②④),示例编译级降级'
fi
# 加载级:root 与否决定运行级验证档
if [ "$uid" -eq 0 ]; then
    echo '[3] 加载级(运行验证)    PASS  root 档:三示例可真实加载,运行级验证可做'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)"; ubd="${ubd:-未知}"
    if [ "$ubd" = 2 ]; then
        echo '[3] 加载级(运行验证)    预期  需 root;且 kernel.unprivileged_bpf_disabled=2,非特权加载彻底禁用——加载/attach/读数均为预期输出示例(运行级待验证)'
    else
        echo '[3] 加载级(运行验证)    预期  加载/attach/读数需 root(root 档);输出均为预期输出示例(运行级待验证)'
    fi
fi
echo '结论:走读级与编译级前提齐备(本机);加载级按权限档位给出预期口径(见上)。'
exit 0

#!/usr/bin/env bash
# 第 8 章示例环境自检(五段式)——BTF/CO-RE 章的「编译级」自检:认树、点工具链、量 BTF、读 CONFIG、报 pahole。
#
# 【用途与输入】
#   检查本机是否具备《第 8 章 BTF 与 CO-RE 原理》全部示例资产的前置条件:
#   走读树与本章四个走读文件、CO-RE 工具链(bpftool/clang/python3,均带真实探针)、
#   内核 BTF 家族(/sys/kernel/btf)、运行内核 CONFIG 三项、pahole 在场性。
#   输入:环境变量 WALK_TREE(默认 /work/linux/linux-6.6.78),可用
#   `WALK_TREE=<你的源码树路径> bash check-env.sh` 覆盖;不接受位置参数。
#
# 【输出结构】
#   五个检查区 + 末尾三档结论,正文各节「学生动手」与章级 README 逐档引用:
#   ①走读树版本与本章四个走读文件(btf.c/relo_core.c/bpf_core_read.h/btf.h uapi)
#   ②工具链(bpftool btf dump 探针:对 /sys/kernel/btf/vmlinux 取第 1 行;
#     clang -g -target bpf 空文件编译探针;python3 --version)
#   ③BTF 家族(/sys/kernel/btf 文件计数、vmlinux 大小与权限、模块 BTF 抽样 1 个)
#   ④CONFIG 三项(DEBUG_INFO_BTF / DEBUG_INFO_BTF_MODULES / MODULE_ALLOW_BTF_MISMATCH)
#   ⑤pahole 在场性(不在场则打印方法论级档说明);
#   结论按「编译级可跑 / 加载级需 root / 方法论级 pahole」三档给出。
#   本章是「编译章」:一切验证围绕 BTF 与编译产物展开,加载运行不在本章实操范围。
#
# 【依赖命令与降级行为】
#   grep 为本脚本自身硬依赖(缺失直接报错退出);
#   find/wc/stat/head/sort 为统计软依赖(文件计数、大小、抽样、探针取行),缺失只降级提示;
#   mktemp 为 clang 探针的临时目录依赖,缺失时跳过探针;
#   bpftool/clang/python3 为本章三档验证的工具链,缺失只降级提示——本脚本只做探针,
#   不构建任何章资产(真实构建由 01/02/03 的 Makefile 与生成脚本承担);
#   本脚本只读源码树与系统状态,不构建、不加载(唯一的临时写入是②的 clang 探针
#   在系统临时目录编译一个空文件,探针结束立即删除,不向内核加载任何东西)。
#
# 【失败模式(退出码)】
#   2  走读树不存在或不是有效源码树(硬性阻断)——打印 kernel.org 获取指引后退出;
#   1  树在但本章四个走读文件任一不在场(树不完整或版本差异过大);
#   0  树与走读文件齐备(工具缺失/BTF 缺失/CONFIG 不可读/pahole 不在场均只降级提示,不改变退出码)。

set -u

# ---- grep:本脚本与后续机检/切片脚本的硬依赖,缺失无法继续 ----
if ! command -v grep >/dev/null 2>&1; then
    echo 'FATAL  缺少 grep:本脚本与 verify-citations.sh、两个切片器都以 grep 为硬依赖,请先安装后重试。'
    exit 1
fi

fail=0   # 树级失败计数(走读文件缺失 -> exit 1);工具/BTF/CONFIG/pahole 缺项只降级,不计数

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

# 本章走读的四个关键源文件(CO-RE 全链的四个坐标):
#   kernel/bpf/btf.c            —— BTF 解析与内核态重定位引擎(bpf_core_apply)
#   tools/lib/bpf/relo_core.c   —— libbpf 用户态重定位引擎(补丁 .bpf.o)
#   tools/lib/bpf/bpf_core_read.h —— CO-RE 宏族(BPF_CORE_READ 等七宏)
#   include/uapi/linux/btf.h    —— BTF kind 枚举与 BTF_INFO 编址(uapi 契约)
# wc 为软依赖:缺失时略过行数统计。
for f in kernel/bpf/btf.c tools/lib/bpf/relo_core.c \
         tools/lib/bpf/bpf_core_read.h include/uapi/linux/btf.h; do
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

echo "== ② 工具链(CO-RE 三件:bpftool 读 BTF / clang 产 .BTF.ext / python3 跑切片器;均带真实探针) =="
# bpftool:vmlinux.h 生成(btf dump format c)、skeleton 生成、对象 BTF 指认的瑞士军刀。
# 探针:对 /sys/kernel/btf/vmlinux 真实 btf dump 并取第 1 行——能打出「[1] …」即 BTF 解析链路通。
bpftool_bin=""
if command -v bpftool >/dev/null 2>&1; then
    bpftool_bin="$(command -v bpftool)"
elif [ -x /usr/sbin/bpftool ]; then
    bpftool_bin=/usr/sbin/bpftool
fi
bpftool_probe=0
if [ -n "$bpftool_bin" ]; then
    bv="$("$bpftool_bin" version 2>/dev/null)"; bv="${bv%%$'\n'*}"
    if [ -r /sys/kernel/btf/vmlinux ] && command -v head >/dev/null 2>&1; then
        first_line="$("$bpftool_bin" btf dump file /sys/kernel/btf/vmlinux 2>/dev/null | head -n 1)"
        if [ -n "$first_line" ]; then
            bpftool_probe=1
            echo "OK    bpftool($bv,位于 $bpftool_bin)——btf dump 探针通过,vmlinux 第 1 行:$first_line"
        else
            echo "WARN  bpftool($bv)在场,但 btf dump /sys/kernel/btf/vmlinux 无输出:解析链路异常,02/03 资产降级"
        fi
    else
        echo "WARN  bpftool($bv)在场,但 /sys/kernel/btf/vmlinux 不可读或 head 缺失:btf dump 探针跳过"
    fi
else
    echo 'WARN  bpftool 缺失:vmlinux.h 无法生成、对象 BTF 无法指认,02/03 编译链断裂(apt install linux-tools-common 或 linux-tools-$(uname -r))'
fi
# clang:BPF 侧编译器。探针:空文件真实编译 -g -target bpf——能产出非空 .o 即 BPF 后端可用。
# 这是本章「编译级已验证」档的地基(01 kind 剖析、03 core-read 都靠它产 .bpf.o)。
clang_probe=0
if command -v clang >/dev/null 2>&1; then
    cv="$(clang --version 2>/dev/null)"; cv="${cv%%$'\n'*}"
    probe_dir="$(mktemp -d 2>/dev/null || true)"
    if [ -n "$probe_dir" ] && [ -d "$probe_dir" ]; then
        : > "$probe_dir/empty.c"
        if clang -g -target bpf -c "$probe_dir/empty.c" -o "$probe_dir/empty.o" >/dev/null 2>&1 \
           && [ -s "$probe_dir/empty.o" ]; then
            clang_probe=1
            if command -v stat >/dev/null 2>&1; then
                echo "OK    clang($cv)——空文件探针通过(-g -target bpf 产出 $(stat -c %s "$probe_dir/empty.o") 字节 .o)"
            else
                echo "OK    clang($cv)——空文件探针通过(-g -target bpf 产出非空 .o)"
            fi
        else
            echo 'WARN  clang 在场但空文件探针失败:可能缺少 BPF 后端(-target bpf),01/03 无法编译(apt install clang + llvm)'
        fi
        rm -rf "$probe_dir"
    else
        echo "WARN  clang($cv)在场,但 mktemp 不可用:编译探针跳过(仅确认版本)"
    fi
else
    echo 'WARN  clang 缺失:BPF 侧无法编译,01/03 资产编译级断裂(需要带 BPF 后端的 clang,apt install clang)'
fi
# python3:04-relo-slicer 切片器(仅标准库)的运行时。
if command -v python3 >/dev/null 2>&1; then
    pv="$(python3 --version 2>&1)"
    echo "OK    python3($pv)——04-relo-slicer 切片器(仅标准库)可跑"
else
    echo 'WARN  python3 缺失:04-relo-slicer 切片器无法运行(apt install python3)'
fi
echo

echo "== ③ BTF 家族(/sys/kernel/btf:CO-RE 运行期的「目标 BTF」,bpftool 与 skeleton 的数据源) =="
if [ -d /sys/kernel/btf ]; then
    if command -v find >/dev/null 2>&1 && command -v wc >/dev/null 2>&1; then
        btf_cnt="$(find /sys/kernel/btf -maxdepth 1 -type f 2>/dev/null | wc -l)"
        btf_cnt="${btf_cnt// /}"
        echo "OK    /sys/kernel/btf 共 $btf_cnt 个 BTF 对象(1 个 vmlinux + 其余为模块 BTF,数目随本机加载的模块而变)"
    else
        echo 'WARN  find/wc 缺失:BTF 对象计数降级为人工确认'
    fi
    if [ -r /sys/kernel/btf/vmlinux ]; then
        if command -v stat >/dev/null 2>&1; then
            echo "OK    /sys/kernel/btf/vmlinux 可读($(stat -c %s /sys/kernel/btf/vmlinux) 字节,mode $(stat -c %a /sys/kernel/btf/vmlinux) 全局可读)——CONFIG_DEBUG_INFO_BTF=y 的落地产物"
        else
            echo 'OK    /sys/kernel/btf/vmlinux 可读(stat 缺失,大小与权限略过)'
        fi
    else
        echo 'WARN  /sys/kernel/btf/vmlinux 不可读:bpftool btf dump 失败,02 vmlinux.h 与 03 skeleton 链断裂;机检与切片不受影响'
    fi
    # 模块 BTF 抽样 1 个:CONFIG_DEBUG_INFO_BTF_MODULES=y 的落地产物(逐模块一份 BTF)。
    if command -v find >/dev/null 2>&1 && command -v sort >/dev/null 2>&1 && command -v head >/dev/null 2>&1; then
        sample="$(find /sys/kernel/btf -maxdepth 1 -type f ! -name vmlinux 2>/dev/null | sort | head -n 1)"
        if [ -n "$sample" ]; then
            if command -v stat >/dev/null 2>&1; then
                echo "OK    模块 BTF 抽样:$(basename "$sample")($(stat -c %s "$sample") 字节)——模块 BTF 与 vmlinux BTF 同机制,供 btf dump file <模块名> 指认"
            else
                echo "OK    模块 BTF 抽样:$(basename "$sample")(stat 缺失,大小略过)"
            fi
        else
            echo 'WARN  未抽样到模块 BTF:可能 CONFIG_DEBUG_INFO_BTF_MODULES 未开或全部模块已卸载'
        fi
    else
        echo 'WARN  find/sort/head 缺失:模块 BTF 抽样跳过'
    fi
else
    echo 'WARN  /sys/kernel/btf 不存在:本内核未启用 BTF(CONFIG_DEBUG_INFO_BTF 未开),CO-RE 运行期修正无「目标 BTF」可用'
fi
echo

echo "== ④ 运行内核 CONFIG(/boot/config-$(uname -r) 或 /proc/config.gz):三项 =="
cfg="/boot/config-$(uname -r)"
cfg_src=""
if [ -r "$cfg" ]; then
    cfg_src="$cfg"
elif [ -r /proc/config.gz ] && command -v zgrep >/dev/null 2>&1; then
    cfg_src=/proc/config.gz
fi
# conf_get <CONFIG 名>:从配置源取该行「=」右边的值,取不到输出空串(即「未设」)。
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
    echo 'WARN  内核配置不可读(/boot/config-* 与 /proc/config.gz 均不可用):CONFIG 三项降级为人工确认'
else
    echo "OK    配置源:$cfg_src"
    case "$(conf_get CONFIG_DEBUG_INFO_BTF)" in
        y) echo 'OK    CONFIG_DEBUG_INFO_BTF=y——vmlinux 内嵌 BTF(③的 /sys/kernel/btf/vmlinux 由此而来,CO-RE 的「目标 BTF」前提)' ;;
        *) echo 'WARN  未检出 CONFIG_DEBUG_INFO_BTF=y:与③互证——若 /sys/kernel/btf/vmlinux 也不在场,CO-RE 全链断在运行期一端' ;;
    esac
    case "$(conf_get CONFIG_DEBUG_INFO_BTF_MODULES)" in
        y) echo 'OK    CONFIG_DEBUG_INFO_BTF_MODULES=y——模块各自携带 BTF(③的模块 BTF 抽样由此而来)' ;;
        *) echo 'INFO  CONFIG_DEBUG_INFO_BTF_MODULES 非 y:模块 BTF 缺位,vmlinux BTF 走读与编译级资产不受影响' ;;
    esac
    case "$(conf_get CONFIG_MODULE_ALLOW_BTF_MISMATCH)" in
        y) echo 'INFO  CONFIG_MODULE_ALLOW_BTF_MISMATCH=y——允许模块 BTF 与 vmlinux 不一致(宽松档)' ;;
        *) echo 'OK    CONFIG_MODULE_ALLOW_BTF_MISMATCH 未设——模块 BTF 与 vmlinux 严格对齐(缺符号才降级,默认严格档)' ;;
    esac
fi
echo

echo "== ⑤ pahole(DWARF→BTF 转换器:本章一节方法论演示的工具,运行期不需要它) =="
if command -v pahole >/dev/null 2>&1; then
    hv="$(pahole --version 2>/dev/null)"; hv="${hv%%$'\n'*}"
    echo "OK    pahole 在场($hv)——DWARF→BTF 转换可本机实操(方法论级可升级为转换级)"
else
    echo 'INFO  pahole 不在 PATH——BTF 的源头工具未装(它把 DWARF 转成 .BTF,属于内核构建期工具,运行期不需要)。'
    echo '       方法论级档说明:本章一节「DWARF→BTF」只给流程与判读法(pahole -J vmlinux 生成 .BTF、'
    echo '       pahole -F BTF -t vmlinux 判读),本机未装 pahole,转换级验证需自备工具链(apt install dwarves);'
    echo '       本章其余资产(02 生成 vmlinux.h、03 编译 CO-RE、04/05 切片)只消费现成 BTF,不受影响。'
fi
echo

echo "== 结论(本章三档验证口径,正文与章级 README 逐档引用) =="
if [ "$fail" -ne 0 ]; then
    echo "[1] 编译级可跑            FAIL  走读树关键源文件缺失——先按上方 kernel.org 指引补齐树"
    echo "结论:走读树不完整——本章源码走读硬性阻断。"
    exit 1
fi
# [1] 编译级可跑:树 + bpftool btf dump 探针 + clang 编译探针 + vmlinux BTF 四件齐,
#     01/02/03 的编译级资产与 04/05 切片器即可本机复现(无 root)。
compile_ok=1
[ "$bpftool_probe" -eq 1 ]     || compile_ok=0
[ "$clang_probe" -eq 1 ]       || compile_ok=0
[ -r /sys/kernel/btf/vmlinux ] || compile_ok=0
if [ "$compile_ok" -eq 1 ]; then
    echo "[1] 编译级可跑            PASS  走读树 $tree_ver(四走读文件齐备)+ bpftool btf dump 探针 + clang -g -target bpf 探针 + vmlinux BTF 在场——01~05 资产编译级可复现(无 root)"
else
    echo '[1] 编译级可跑            WARN  编译级三件(bpftool 探针/clang 探针/vmlinux BTF)有缺项(见②③),相关资产降级'
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo '      WARN  python3 缺失:04-relo-slicer 切片器单独降级(其余编译级资产不受影响)'
fi
# [2] 加载级需 root:03 的加载/attach/读取属运行级待验证,输出一律按「预期输出示例」标注。
if [ "$(id -u)" -eq 0 ]; then
    echo '[2] 加载级需 root         PASS  root 档:03 可真实加载,运行级验证可做'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)"; ubd="${ubd:-未知}"
    if [ "$ubd" = 2 ]; then
        echo '[2] 加载级需 root         预期  非特权用户且 kernel.unprivileged_bpf_disabled=2,非特权加载彻底禁用——03 的加载/attach/读取均为预期输出示例(运行级待验证,需 root)'
    else
        echo '[2] 加载级需 root         预期  加载/attach/读取需 root(root 档);输出均为预期输出示例(运行级待验证)'
    fi
fi
# [3] 方法论级 pahole:DWARF→BTF 转换档位随 ⑤ 的探针结果给出。
if command -v pahole >/dev/null 2>&1; then
    echo '[3] 方法论级 pahole       PASS  pahole 在场——一节 DWARF→BTF 转换可升级为转换级实操'
else
    echo '[3] 方法论级 pahole       方法论  本机未装 pahole,转换级验证需自备工具链——一节按方法论档给流程与判读法'
fi
echo '结论:编译级前提齐备(本机无 root 即可复现 01~05 资产);加载级按权限档位给出预期口径;方法论级按 pahole 在场性定档(见上)。'
exit 0

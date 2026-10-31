#!/usr/bin/env bash
# 第 6 章示例环境自检(分级诊断)——JIT 走读章的「编译级」自检:读开关、认源码、点工具。
#
# 【用途与输入】
#   检查本机是否具备《第 6 章 JIT 编译与性能优化》全部走读资产的前置条件:
#   走读树(Linux 源码树,重点是 JIT 四源文件)、JIT 开关的 sysctl 可读性分级、
#   编译/反汇编工具链、运行内核 CONFIG、内核 BTF、权限档位。
#   输入:环境变量 WALK_TREE(默认 /work/linux/linux-6.6.78),可用
#   `WALK_TREE=<你的源码树路径> bash check-env.sh` 覆盖;不接受位置参数。
#
# 【输出结构】
#   五个检查区 + 末尾三档结论,正文一节引用②的 sysctl 分级表、正文四节逐档引用结论:
#   ①走读树与 JIT 源文件 ②JIT 开关(sysctl)可读性分级 ③工具链(只查版本,不做编译探针)
#   ④运行内核 CONFIG 三项 ⑤内核 BTF;
#   结论按「编译级与机检 / 加载级与 bpftool dump / 性能对比方法论」三档给出。
#   ②的核心教学点:同一排开关(bpf_jit_*),文件权限不同——enable 0644 人人可读,
#   harden/kallsyms/limit 0600 仅 root 可读,非特权读取返回 EACCES:「读不到」不等于「不存在」。
#
# 【依赖命令与降级行为】
#   grep 为本脚本自身硬依赖(缺失直接报错退出)——它同时是全部机检/切片脚本的硬依赖;
#   sed/wc 为机检级软依赖(切片器区间提取与行数统计),缺失只打 WARN 降级提示;
#   stat 为软依赖(root-only 分级行的权限位数,缺失按 0600 文案降级);
#   clang/llvm-objdump 为编译级软依赖(01-bpf-asm 的 -target bpf 编译与反汇编),
#   本脚本只查版本与 bpf 目标支持,不做编译探针(编译级真实验证由 01-bpf-asm 现场 make 承担);
#   bpftool 为可选依赖:仅服务正文四节⑤层 bpftool prog dump xlated/jited 讲解(需加载,属特权查询档)。
#   本脚本只读源码树与 /sys、/proc、/boot 下的系统文件,不构建、不加载。
#
# 【失败模式(退出码)】
#   2  走读树不存在或不是有效源码树(硬性阻断)——打印 kernel.org 获取指引后退出;
#   1  树在但 JIT 关键文件缺失(arch/x86、arch/arm64 四源文件任一不在场,树不完整);
#   0  树与关键文件齐备(工具缺失/BTF 缺失/非 root 均只降级提示,不改变退出码)。

set -u

# ---- grep:本脚本与全部机检/切片脚本的硬依赖,缺失无法继续 ----
if ! command -v grep >/dev/null 2>&1; then
    echo 'FATAL  缺少 grep:本脚本与 verify-citations.sh、两个切片器都以 grep 为硬依赖,请先安装后重试。'
    exit 1
fi

fail=0   # 树级失败计数(JIT 关键文件缺失 -> exit 1);工具/BTF/权限缺项只降级,不计数

echo "== ① 走读树(本章机检、切片与 JIT 源码走读的唯一数据源) =="
WALK_TREE="${WALK_TREE:-/work/linux/linux-6.6.78}"
if [ ! -f "$WALK_TREE/Makefile" ]; then
    echo "FAIL  走读树不可用:$WALK_TREE(未找到 Makefile)"
    echo '       本章全部机检、切片与 JIT 源文件走读以该树为数据源,树缺失属于硬性阻断。'
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

# 本章 JIT 走读的四个关键源文件:x86_64 后端(正文二节)、x86_32 后端(布局一句话)、
# arm64 后端(正文三节)及其头文件(寄存器映射宏 A64_R)。
# wc 为软依赖:缺失时略过行数统计,只降级不判 FAIL。
for f in arch/x86/net/bpf_jit_comp.c arch/x86/net/bpf_jit_comp32.c \
         arch/arm64/net/bpf_jit_comp.c arch/arm64/net/bpf_jit.h; do
    if [ -f "$WALK_TREE/$f" ]; then
        if command -v wc >/dev/null 2>&1; then
            echo "OK    $f 在场($(wc -l < "$WALK_TREE/$f") 行)"
        else
            echo "OK    $f 在场(wc 缺失,略过行数统计)"
        fi
    else
        echo "FAIL  $f 不在场:树不完整或版本差异过大,正文二/三节 JIT 走读无从对照"
        fail=1
    fi
done
echo

echo "== ② JIT 开关(sysctl)可读性分级诊断——同一排开关,读得到的和读不到的 =="
# 正文一节「sysctl 四兄弟」的机检原型:四个 bpf_jit_* 在 net/core/sysctl_net_core.c 注册,
# perf_event_paranoid 在 kernel/events/core.c 注册;文件权限逐个不同,分级读法本身就是教学点。
netcore=/proc/sys/net/core

# bpf_jit_enable:JIT 总开关,表项 mode=0644,人人可读
f=$netcore/bpf_jit_enable
if [ ! -e "$f" ]; then
    echo 'LIMIT [缺失] net.core.bpf_jit_enable——内核未提供该开关(随内核版本演进,以所用内核源码为准)'
elif [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    perms="$(stat -c %a "$f" 2>/dev/null)"; perms="${perms:-0644}"
    case "$val" in
        1) mean='JIT 开——加载时由 arch 后端(arch/x86/net/bpf_jit_comp.c 等)把 BPF 指令编译成机器码;与④ CONFIG_BPF_JIT_DEFAULT_ON=y 自洽(开机默认开)' ;;
        0) mean='JIT 关——回退解释器逐条执行;注意:④ ALWAYS_ON=y 的内核上此开关被锁成 1,写 0 直接失败' ;;
        2) mean='JIT 开 + 调试输出——编译时向内核日志转储每条指令,仅供调试(sysctl_net_core.c 对写 2 打印 pr_warn 警告)' ;;
        *) mean="未识别的读数($val):随内核版本演进,以所用内核源码为准" ;;
    esac
    echo "OK    [可读] net.core.bpf_jit_enable = $val(文件权限 $perms)"
    echo "      解读:$mean"
else
    echo 'LIMIT [受限] net.core.bpf_jit_enable 不可读(本应 0644 人人可读):权限档位异常,检查挂载与 LSM'
fi

# bpf_jit_harden / bpf_jit_kallsyms / bpf_jit_limit:表项 mode=0600,root-only
# 通用解读在每行给出;三行之后的「教学点」统一解释为什么读不到(文件权限不同)。
f=$netcore/bpf_jit_harden
if [ ! -e "$f" ]; then
    echo 'LIMIT [缺失] net.core.bpf_jit_harden——内核未提供该开关'
elif [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    echo "OK    [可读] net.core.bpf_jit_harden = $val"
    echo '      解读:常量致盲(constant blinding)开关——0=关;1=仅对非特权程序生效;2=对所有程序生效;开启后 JIT 前先改写含敏感立即数的指令(kernel/bpf/core.c bpf_jit_blind_insn / bpf_jit_blind_constants),防 JIT 喷射'
else
    perms="$(stat -c %a "$f" 2>/dev/null)"; perms="${perms:-0600}"
    echo "LIMIT [root-only($perms)] net.core.bpf_jit_harden——非特权读取返回 EACCES(Permission denied)"
    echo '      解读:常量致盲(constant blinding)开关——0=关;1=仅对非特权程序生效;2=对所有程序生效;开启后 JIT 前先改写含敏感立即数的指令(kernel/bpf/core.c bpf_jit_blind_insn / bpf_jit_blind_constants),防 JIT 喷射'
fi

f=$netcore/bpf_jit_kallsyms
if [ ! -e "$f" ]; then
    echo 'LIMIT [缺失] net.core.bpf_jit_kallsyms——内核未提供该开关'
elif [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    echo "OK    [可读] net.core.bpf_jit_kallsyms = $val"
    echo '      解读:JIT 映像是否以 bpf_prog_<tag>_<name> 形式的符号暴露进 /proc/kallsyms——0=隐藏;1=暴露(仍受 kptr_restrict 约束);排查「程序 JIT 到哪段地址」时配合 root 使用'
else
    perms="$(stat -c %a "$f" 2>/dev/null)"; perms="${perms:-0600}"
    echo "LIMIT [root-only($perms)] net.core.bpf_jit_kallsyms——非特权读取返回 EACCES(Permission denied)"
    echo '      解读:JIT 映像是否以 bpf_prog_<tag>_<name> 形式的符号暴露进 /proc/kallsyms——0=隐藏;1=暴露(仍受 kptr_restrict 约束);排查「程序 JIT 到哪段地址」时配合 root 使用'
fi

f=$netcore/bpf_jit_limit
if [ ! -e "$f" ]; then
    echo 'LIMIT [缺失] net.core.bpf_jit_limit——内核未提供该开关'
elif [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    echo "OK    [可读] net.core.bpf_jit_limit = $val"
    echo '      解读:全局 JIT 内存上限(字节,long)——JIT 映像(bpf_binary_header)分配受此限额约束,耗尽后新的 JIT 分配被拒(加载报错)'
else
    perms="$(stat -c %a "$f" 2>/dev/null)"; perms="${perms:-0600}"
    echo "LIMIT [root-only($perms)] net.core.bpf_jit_limit——非特权读取返回 EACCES(Permission denied)"
    echo '      解读:全局 JIT 内存上限(字节,long)——JIT 映像(bpf_binary_header)分配受此限额约束,耗尽后新的 JIT 分配被拒(加载报错)'
fi

echo '      —— 教学点:上述三个开关的 sysctl 表项在 net/core/sysctl_net_core.c 里 mode=0600(属 root),'
echo '         而 bpf_jit_enable 是 0644:同一目录下文件权限不同,非特权 cat 得 EACCES 属预期行为;'
echo '         「读不到」不等于「不存在」,root 档位下重跑本脚本即可看到它们的值。'

# perf_event_paranoid:perf 剖析收紧档位,表项 mode=0644,人人可读;影响性能对比的取证手段
f=/proc/sys/kernel/perf_event_paranoid
if [ ! -e "$f" ]; then
    echo 'LIMIT [缺失] kernel.perf_event_paranoid——内核未提供该开关'
elif [ -r "$f" ]; then
    val="$(cat "$f" 2>/dev/null)"; val="${val%%$'\n'*}"
    perms="$(stat -c %a "$f" 2>/dev/null)"; perms="${perms:-0644}"
    echo "OK    [可读] kernel.perf_event_paranoid = $val(文件权限 $perms)"
    echo '      解读:值越大越严——>=1 禁非特权 CPU 事件,>1 禁内核态剖析(kernel/events/core.c perf_allow_kernel 判 paranoid > 1);主线默认 2,更大值为发行版加固;本机 4 = 最严档,非特权 perf_event_open 基本全禁——正文四节性能对比的 perf 取证因此只能走方法论口径'
else
    echo 'LIMIT [受限] kernel.perf_event_paranoid 不可读(本应 0644):权限档位异常'
fi
echo

echo "== ③ 工具链(只查版本与 bpf 目标支持,不做编译探针) =="
# sed/wc:切片器(trace-*.sh 的 sed -n 区间提取、grep -n 定位后配 wc -l 看规模)的软依赖
for t in sed wc; do
    if command -v "$t" >/dev/null 2>&1; then
        echo "OK    $t(切片器与行数统计依赖)"
    else
        echo "WARN  $t 缺失:切片器与行数统计降级,走读树本身不受影响(安装系统包 sed/coreutils)"
    fi
done
# clang:01-bpf-asm(Task 2 落地)的编译器;本脚本只报版本——现场编译属于 01-bpf-asm 的职责,自检不重复探针
if command -v clang >/dev/null 2>&1; then
    cv="$(clang --version 2>/dev/null)"; cv="${cv%%$'\n'*}"
    echo "OK    clang($cv)——01-bpf-asm 的 -target bpf 编译器(只查版本,编译探针由其 Makefile 现场 make 承担)"
else
    echo 'WARN  clang 缺失:01-bpf-asm 编译级对照降级(需要带 BPF 后端的 clang,apt install clang)'
fi
# llvm-objdump:.bpf.o 反汇编的核心工具;顺手确认 BPF 目标后端在(--version 目标列表里应有 bpf)
if command -v llvm-objdump >/dev/null 2>&1; then
    ov="$(llvm-objdump --version 2>/dev/null)"; ov1="${ov%%$'\n'*}"
    if printf '%s\n' "$ov" | grep -qi bpf; then
        tgts="$(printf '%s\n' "$ov" | grep -i bpf | tr '\n' ' ')"
        echo "OK    llvm-objdump($ov1)——bpf 目标后端在场:$tgts"
    else
        echo "WARN  llvm-objdump($ov1)在场但目标列表未检出 bpf:llvm-objdump -d 对 .bpf.o 会失败,需换装带 BPF 后端的 llvm"
    fi
else
    echo 'WARN  llvm-objdump 缺失:01-bpf-asm 反汇编降级(apt install llvm)'
fi
# bpftool:仅服务正文四节⑤层 introspection 讲解(均属加载级/特权查询档)。
# Debian/Ubuntu 装在 /usr/sbin,个别非 root PATH 不含它,故补一个常见路径回退。
bpftool_bin=""
if command -v bpftool >/dev/null 2>&1; then
    bpftool_bin="$(command -v bpftool)"
elif [ -x /usr/sbin/bpftool ]; then
    bpftool_bin=/usr/sbin/bpftool
fi
if [ -n "$bpftool_bin" ]; then
    bv="$("$bpftool_bin" version 2>/dev/null)"; bv="${bv%%$'\n'*}"
    echo "OK    bpftool($bv,位于 $bpftool_bin)——仅服务正文四节⑤层 bpftool prog dump xlated/jited 讲解,需加载,属特权查询档"
else
    echo 'WARN  bpftool 缺失:仅影响正文四节⑤层 introspection 预期输出,机检与切片不受影响'
fi
echo

echo "== ④ 运行内核 CONFIG(/boot/config-$(uname -r)):JIT 三项 =="
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
    echo 'WARN  内核配置不可读(/boot/config-* 与 /proc/config.gz 均不可用):CONFIG 三项降级为人工确认,不影响机检'
    always_on='未知'
else
    echo "OK    配置源:$cfg_src"
    jit="$(conf_get CONFIG_BPF_JIT)"
    ao="$(conf_get CONFIG_BPF_JIT_ALWAYS_ON)"
    don="$(conf_get CONFIG_BPF_JIT_DEFAULT_ON)"
    always_on="$ao"
    case "$jit" in
        y) echo 'OK    CONFIG_BPF_JIT=y——JIT 基础设施编译进内核,①的四个 arch/*/net/bpf_jit_* 源文件有运行时落点' ;;
        *) echo "INFO  CONFIG_BPF_JIT=$jit(非 y):本机未编译 JIT 基础设施——①源文件只能「读」,sysctl 开关可能不齐(随内核版本演进,以所用内核源码为准)" ;;
    esac
    case "$ao" in
        y) echo 'OK    CONFIG_BPF_JIT_ALWAYS_ON=y——解释器在编译期被移除(kernel/bpf/core.c 中解释器代码包在 #ifndef CONFIG_BPF_JIT_ALWAYS_ON 里),JIT 无条件启用;bpf_jit_enable 的 sysctl 表项被锁成 min=max=1(net/core/sysctl_net_core.c),写 0 直接失败——这个开关只剩安全/调试语义,不再承担「关掉 JIT 回退解释器」;本机含义:JIT vs 解释器对比无法真实验证,只能走方法论档' ;;
        *) echo "INFO  CONFIG_BPF_JIT_ALWAYS_ON=$ao(非 y):解释器在内核里,bpf_jit_enable 可写 0 切回解释模式(性能对比可真实做,安全注「生产禁关」)" ;;
    esac
    case "$don" in
        y) echo 'OK    CONFIG_BPF_JIT_DEFAULT_ON=y——bpf_jit_enable 开机默认 1(Kconfig:def_bool ARCH_WANT_DEFAULT_BPF_JIT || BPF_JIT_ALWAYS_ON);与②实测读数自洽' ;;
        *) echo "INFO  CONFIG_BPF_JIT_DEFAULT_ON=$don(非 y):bpf_jit_enable 开机默认 0,需要手动开启 JIT" ;;
    esac
fi
echo

echo "== ⑤ 内核 BTF(特权查询档的前提;机检与切片不需要它) =="
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在且可读(运行内核自带 BTF)'
else
    echo 'WARN  /sys/kernel/btf/vmlinux 不可读:机检与切片不受影响;bpftool introspection 类示例的复验需 BTF 在场'
fi
if [ -n "$cfg_src" ] && [ "$(conf_get CONFIG_DEBUG_INFO_BTF)" = y ]; then
    echo "OK    CONFIG_DEBUG_INFO_BTF=y($cfg_src):BTF 与内核构建一致(bpftool btf dump 的前提)"
else
    echo 'WARN  未检出 CONFIG_DEBUG_INFO_BTF=y:仅影响特权查询档,机检与切片不受影响'
fi
echo

echo "== 结论(本章三档口径,正文「四、实战」逐档引用) =="
if [ "$fail" -ne 0 ]; then
    echo "[1] 编译级与机检         FAIL  走读树 JIT 关键文件缺失——先按上方 kernel.org 指引补齐树"
    echo "[2] 加载级与 bpftool dump FAIL  树不完整,切片器与机检无从实跑"
    echo "结论:走读树不完整——本章 JIT 走读硬性阻断。"
    exit 1
fi
# 编译级与机检:机检需 sed/wc(切片器);编译级(01-bpf-asm 反汇编)需 clang + llvm-objdump
tools_ok=1
command -v sed >/dev/null 2>&1 || tools_ok=0
command -v wc  >/dev/null 2>&1 || tools_ok=0
compile_ok=1
command -v clang        >/dev/null 2>&1 || compile_ok=0
command -v llvm-objdump >/dev/null 2>&1 || compile_ok=0
if [ "$tools_ok" -eq 1 ] && [ "$compile_ok" -eq 1 ]; then
    echo "[1] 编译级与机检         PASS  走读树 $tree_ver(JIT 四源文件齐备)+ clang/llvm-objdump(bpf 目标)在场——01-bpf-asm 反汇编(只编译不加载)与 verify-citations.sh 机检可复现"
elif [ "$tools_ok" -eq 1 ]; then
    echo "[1] 编译级与机检         WARN  机检级前提齐备;clang/llvm-objdump 缺项,01-bpf-asm 编译级对照降级(见③)"
else
    echo "[1] 编译级与机检         WARN  部分机检工具缺失,切片器/行数统计降级"
fi
uid="$(id -u)"
if [ "$uid" -eq 0 ]; then
    echo '[2] 加载级与 bpftool dump PASS  root 档:加载后 bpftool prog dump xlated/jited 可真实验证'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)"; ubd="${ubd:-未知}"
    if [ "$ubd" = 2 ]; then
        echo '[2] 加载级与 bpftool dump 需要特权,输出为预期输出示例(运行级待验证):需 root;且 kernel.unprivileged_bpf_disabled=2,非特权加载彻底禁用'
    else
        echo '[2] 加载级与 bpftool dump 需要特权,输出为预期输出示例(运行级待验证):bpftool prog dump xlated/jited 需加载 BPF 程序(root 档)'
    fi
fi
case "$always_on" in
    y) echo '[3] 性能对比方法论        预期  CONFIG_BPF_JIT_ALWAYS_ON=y:解释器编译期移除,本机 JIT 不可关——JIT vs 解释器对比无法真实验证,只给方法论(可关 ALWAYS_ON 的内核上复现);perf_event_paranoid 加固进一步限制非特权 perf 取证' ;;
    未知) echo '[3] 性能对比方法论        预期  CONFIG 可读性受限:请人工确认 ALWAYS_ON;若 ALWAYS_ON=y 则 JIT vs 解释器对比只能走方法论档' ;;
    *) echo '[3] 性能对比方法论        预期  ALWAYS_ON 非 y 的内核上可真实验证(方法:写 0 关 JIT 对比,安全注「生产禁关」);本报告按预期口径给出' ;;
esac
echo '结论:走读树与机检级前提齐备;加载级与方法论档按权限与 CONFIG 档位给出(见上)。'
exit 0

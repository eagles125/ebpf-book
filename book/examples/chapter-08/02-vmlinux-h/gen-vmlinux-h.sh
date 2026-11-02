#!/usr/bin/env bash
# 第 8 章示例 02-vmlinux-h 生成脚本——用 bpftool 从运行内核 BTF 生成 vmlinux.h 并统计规模。
#
# 【用途与输入】
#   把「vmlinux.h 从哪来」落成一个可重复执行的动作:读取运行内核暴露在
#   /sys/kernel/btf/vmlinux 的 BTF(BPF Type Format,BPF 类型格式),
#   用 `bpftool btf dump ... format c` 转成 C 头文件 vmlinux.h,落在本目录,
#   末尾打印规模统计(字节/行数/struct/enum/typedef 计数)。
#   不接受参数;唯一可选输入是 BPFTOOL 环境变量(默认 bpftool,可指向别的路径)。
#   vmlinux.h 是构建产物,由仓库 .gitignore 全局规则(book/examples/**/vmlinux.h)
#   覆盖,不入库;重复生成无副作用(内容只随内核版本变)。
#
# 【输出结构】
#   1) 生成进度提示(全量转换约数十秒,属正常);
#   2) 本目录的 vmlinux.h(bpftool btf dump file /sys/kernel/btf/vmlinux format c 的产物);
#   3) 末尾「规模统计」五项(README.md 同口径,数值标「本机实况,其他内核数值不同」):
#        字节(wc -c)/ 行数(wc -l)/ struct 声明数(grep -c "^struct")/
#        enum 声明数(grep -c "^enum")/ typedef 声明数(grep -c "^typedef")。
#
# 【依赖与降级行为】
#   bpftool(或 $BPFTOOL)、grep、wc 为硬依赖,缺失或源 BTF 不可读直接失败退出,不假成功;
#   stat 仅用于打印源 BTF 大小的辅助信息,缺失只跳过该行。
#
# 【失败模式(退出码)】
#   1  前置缺失:bpftool 不在 PATH;或 /sys/kernel/btf/vmlinux 不存在/不可读
#      (典型原因:内核未开 CONFIG_DEBUG_INFO_BTF,或系统不支持 BTF)——
#      给出排错指引后退出;
#   2  生成失败:bpftool 转换返回非零,或产物为空(此时不落盘 vmlinux.h,
#      保留任何已存在的旧产物不受影响);
#   0  成功:vmlinux.h 已生成并打印规模统计。
#
# 【安全声明】本脚本只读系统 BTF(/sys/kernel/btf/vmlinux),不构建、不加载
#   任何 BPF 程序;唯一写入是本目录的 vmlinux.h 产物文件。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BPFTOOL="${BPFTOOL:-bpftool}"
BTF_SRC="/sys/kernel/btf/vmlinux"
OUT="vmlinux.h"

# ---- 硬依赖检查:bpftool 与源 BTF,缺一不可 ----
if ! command -v "$BPFTOOL" >/dev/null 2>&1; then
    echo "FATAL  找不到 bpftool(可执行文件或 $BPFTOOL 环境变量所指路径)。"
    echo '       bpftool 随内核源码 tools/bpf/bpftool 发布,发行版多提供同名包'
    echo '       (如 Ubuntu 的 linux-tools-common/linux-tools-$(uname -r))。'
    exit 1
fi
if [ ! -r "$BTF_SRC" ]; then
    echo "FATAL  源 BTF 不可读:$BTF_SRC 不存在或无读权限。"
    echo '       典型原因:运行内核未开启 CONFIG_DEBUG_INFO_BTF=y,没有编入内核 BTF;'
    echo '       可用 `grep CONFIG_DEBUG_INFO_BTF /boot/config-$(uname -r)` 核对'
    echo '       (参考 00-env/check-env.sh 的 CONFIG 三项检查)。'
    exit 1
fi

# ---- 辅助信息:源 BTF 体量(仅提示,stat 缺失则跳过) ----
if command -v stat >/dev/null 2>&1; then
    echo "== 源 BTF: $BTF_SRC($(stat -c '%s' "$BTF_SRC" 2>/dev/null) 字节) =="
else
    echo "== 源 BTF: $BTF_SRC =="
fi
echo "开始转换为 C 头文件(bpftool btf dump format c;视机器与 bpftool 版本可能数十秒,属正常)……"

# ---- 生成:先写临时文件,校验非空再落盘,失败不破坏已存在的旧产物 ----
TMP_OUT="$OUT.tmp.$$"
t0=$SECONDS
if ! "$BPFTOOL" btf dump file "$BTF_SRC" format c > "$TMP_OUT"; then
    rm -f "$TMP_OUT"
    echo "FATAL  bpftool btf dump 转换失败(非零返回),未写入 $OUT。"
    exit 2
fi
if [ ! -s "$TMP_OUT" ]; then
    rm -f "$TMP_OUT"
    echo "FATAL  转换产物为空(源 BTF 可能损坏或 bpftool 版本异常),未写入 $OUT。"
    exit 2
fi
mv "$TMP_OUT" "$OUT"
echo "生成完成:$OUT(耗时 $((SECONDS - t0)) 秒)"

# ---- 末尾规模统计(与 README.md 同口径;grep -c 计数为 0 时返回非零,须用 || true 兜住) ----
bytes=$(wc -c < "$OUT")
lines=$(wc -l < "$OUT")
n_struct=$(grep -c '^struct' "$OUT" || true)
n_enum=$(grep -c '^enum' "$OUT" || true)
n_typedef=$(grep -c '^typedef' "$OUT" || true)
echo "== 规模统计(本机实况,其他内核数值不同) =="
echo "  字节: $bytes(wc -c)"
echo "  行数: $lines(wc -l)"
echo "  struct 声明数: $n_struct(grep -c '^struct')"
echo "  enum 声明数: $n_enum(grep -c '^enum')"
echo "  typedef 数: $n_typedef(grep -c '^typedef')"
echo "产物不入库:.gitignore 全局规则 book/examples/**/vmlinux.h 已覆盖;重复生成无副作用。"

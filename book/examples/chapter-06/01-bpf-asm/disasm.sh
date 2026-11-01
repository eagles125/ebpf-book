#!/usr/bin/env bash
# disasm.sh —— 第 6 章 01-bpf-asm 反汇编锚点的快捷封装
#
# 【脚本用途与输入】
# 对本目录的 xdp_count.bpf.o 做 llvm-objdump -d 反汇编,把「源码 → BPF
# 字节码」锚点程序的目标码逐条打印出来;无参数。本示例只编译不加载,
# 无挂载风险——本脚本同样不加载、不调用 bpf() 系统调用。
#
# 【输出结构】
#   - llvm-objdump -d 的原始输出:文件头信息 + 逐条 BPF 指令
#     (偏移 | 机器码 8 字节 | 反汇编助记符),锚点程序应为 14 条指令;
#   - 与 README.md 的逐行 source↔bytecode 对照表逐字对应。
#
# 【依赖与降级】
#   - 依赖:llvm-objdump(与编译用的 clang 同为 LLVM 18 系列);
#   - .bpf.o 不存在:先提示 make,不自动编译(保持「跑什么反什么」的
#     确定性);llvm-objdump 缺失:报错退出 1。
#
# 【失败模式】
#   - 反汇编器与 clang 版本不匹配时输出可能异形:用 00-env/check-env.sh
#     自检工具链;锚点程序按 v6.6.78 书稿口径产出 14 条指令,clang 大版本
#     变化可能改变指令数(寄存器分配/常量折叠策略演进)。
set -u

cd "$(dirname "$0")" || exit 1

if ! command -v llvm-objdump >/dev/null 2>&1; then
    printf 'FAIL 缺少依赖命令 llvm-objdump,无法反汇编\n' >&2
    exit 1
fi

if [[ ! -f xdp_count.bpf.o ]]; then
    printf 'FAIL 未找到 xdp_count.bpf.o:请先执行 make 生成,再运行 ./disasm.sh\n' >&2
    exit 1
fi

exec llvm-objdump -d xdp_count.bpf.o

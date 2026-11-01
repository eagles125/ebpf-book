# Makefile —— 第 6 章 01-bpf-asm 反汇编锚点(只编译不加载,无挂载风险)
#
# 目标:
#   all    —— 编译 xdp_count.bpf.c 产出 BPF 目标码 xdp_count.bpf.o(默认目标)
#   disasm —— 先确保 .bpf.o 在,再反汇编(llvm-objdump -d)
#   clean  —— 删除构建产物
#
# 依赖:clang(-target bpf)、llvm-objdump;uapi 头来自系统默认路径,
# -I/usr/include/x86_64-linux-gnu 仅供 asm/types.h(多架构头)解析。

all: xdp_count.bpf.o

xdp_count.bpf.o: xdp_count.bpf.c
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu -c xdp_count.bpf.c -o xdp_count.bpf.o

disasm: xdp_count.bpf.o
	llvm-objdump -d xdp_count.bpf.o

clean:
	rm -f xdp_count.bpf.o

.PHONY: all disasm clean

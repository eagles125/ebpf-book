# 第 4 章「类型画廊」编译脚本（compile-only：本目录程序只编译不加载）
# 安全警告：本脚本只编译不加载（无挂载风险）
BPF_SRCS := lsm.bpf.c
# 目标名替换用 .c=.o：源文件名已是 *.bpf.c，直接得到 *.bpf.o
BPF_OBJS := $(BPF_SRCS:.c=.o)

all: $(BPF_OBJS)

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

%.bpf.o: %.bpf.c vmlinux.h
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -c $< -o $@

clean:
	rm -f $(BPF_OBJS) vmlinux.h

.PHONY: all clean

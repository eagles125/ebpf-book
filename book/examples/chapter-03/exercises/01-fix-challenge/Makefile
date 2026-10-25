# 第 3 章「课后练习」编译脚本（compile-only：拒绝发生在加载期，clang 层面必须全部通过）
# 安全警告：本脚本只编译不加载（无 XDP 挂载风险）——broken 产物预期在加载期被验证器拒绝
BPF_SRCS := broken.bpf.c fixed.bpf.c
# 源文件名为 <名字>.bpf.c，目标为 <名字>.bpf.o——后缀替换用 .c=.o（写 .c=.bpf.o 会得到双重扩展名）
BPF_OBJS := $(BPF_SRCS:.c=.o)

all: $(BPF_OBJS)

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

%.bpf.o: %.bpf.c vmlinux.h
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -c $< -o $@

clean:
	rm -f $(BPF_OBJS) vmlinux.h

.PHONY: all clean

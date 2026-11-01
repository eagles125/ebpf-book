# Makefile —— 第 7 章 02-percpu-counter(libbpf-bootstrap 模式)
#
# 【用途与输入】
#   构建 per-CPU 计数器示例:vmlinux.h → xdp_percpu.bpf.o → skeleton 头 →
#   gcc 链接用户态 loader 产出 pcpu_count。无输入参数;构建产物均不入库。
#
# 【输出结构】
#   all(默认)= vmlinux.h → xdp_percpu.bpf.o → xdp_percpu_bpf.skel.h → pcpu_count;
#   clean 删除全部产物。
#
# 【依赖与降级】
#   clang(-target bpf)、bpftool(btf dump + gen skeleton)、gcc + libbpf
#   (-lbpf -lelf -lz)。任何一环缺失 make 即失败,不做降级假成功。
#
# 【失败模式】
#   - bpftool btf dump 失败:内核无 BTF(/sys/kernel/btf/vmlinux 缺失);
#   - 链接失败:libbpf 开发包缺失(apt install libbpf-dev)。
#   验证状态:构建=编译级已验证;pcpu_count 运行需 root(预期输出示例)。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
CC ?= gcc

# 多架构头目录:linux/types.h 里 #include <asm/types.h>,BPF 目标下需显式给出
MULTIARCH := $(shell gcc -print-multiarch 2>/dev/null)
INCLUDES := -I$(shell $(CLANG) -print-file-name=include) -I/usr/include/$(MULTIARCH)
CFLAGS := -g -Wall -Wextra

.PHONY: all clean

all: pcpu_count

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

xdp_percpu.bpf.o: xdp_percpu.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

# skeleton 头文件名与文件内符号名(xdp_percpu_bpf)保持一致
xdp_percpu_bpf.skel.h: xdp_percpu.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

pcpu_count: loader.c xdp_percpu_bpf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h pcpu_count

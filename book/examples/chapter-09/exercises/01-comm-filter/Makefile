# 第 9 章练习 01-comm-filter 构建脚本（libbpf-bootstrap 模式，与基线 02-pid-filter 一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: comm_filter

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

comm_filter.bpf.o: comm_filter.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

comm_filter.skel.h: comm_filter.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

comm_filter: comm_filter.c comm_filter.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h comm_filter

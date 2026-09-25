# 第 17 章阶段 4 构建脚本（libbpf-bootstrap 模式，与第 9 章示例一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: nat

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

nat.bpf.o: nat.bpf.c vmlinux.h fw_common.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

nat.skel.h: nat.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

nat: nat.c nat.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h nat

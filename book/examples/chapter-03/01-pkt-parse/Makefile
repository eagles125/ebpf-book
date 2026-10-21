# 第 3 章示例构建脚本（libbpf-bootstrap 模式，与第 2/17 章示例一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）
# 安全警告：XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: pkt_parse

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

pkt_parse.bpf.o: pkt_parse.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

pkt_parse.skel.h: pkt_parse.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

pkt_parse: pkt_parse.c pkt_parse.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h pkt_parse

# 第 9 章示例 08-open-file-tracepoint 构建脚本（libbpf-bootstrap 模式）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: opensnoop_tp

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

opensnoop_tp.bpf.o: opensnoop_tp.bpf.c opensnoop_tp_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

opensnoop_tp.skel.h: opensnoop_tp.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

opensnoop_tp: opensnoop_tp.c opensnoop_tp_common.h opensnoop_tp.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h opensnoop_tp

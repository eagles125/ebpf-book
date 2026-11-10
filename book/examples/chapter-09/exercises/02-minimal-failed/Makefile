# 第 9 章练习 02-minimal-failed 构建脚本（libbpf-bootstrap 模式，与基线 05-open-file-kprobe 一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: opensnoop_mf

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

opensnoop_mf.bpf.o: opensnoop_mf.bpf.c opensnoop_mf_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

opensnoop_mf.skel.h: opensnoop_mf.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

opensnoop_mf: opensnoop_mf.c opensnoop_mf_common.h opensnoop_mf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h opensnoop_mf

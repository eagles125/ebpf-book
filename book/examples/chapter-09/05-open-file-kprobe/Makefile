# 第 9 章示例 05-open-file-kprobe 构建脚本（libbpf-bootstrap 模式）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: opensnoop_kprobe

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

opensnoop_kprobe.bpf.o: opensnoop_kprobe.bpf.c opensnoop_kprobe_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

opensnoop_kprobe.skel.h: opensnoop_kprobe.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

opensnoop_kprobe: opensnoop_kprobe.c opensnoop_kprobe_common.h opensnoop_kprobe.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h opensnoop_kprobe

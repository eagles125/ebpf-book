# 第 9 章示例 06-open-file-perf-event 构建脚本（libbpf-bootstrap 模式）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: opensnoop_perf

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

opensnoop_perf.bpf.o: opensnoop_perf.bpf.c opensnoop_perf_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

opensnoop_perf.skel.h: opensnoop_perf.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

opensnoop_perf: opensnoop_perf.c opensnoop_perf_common.h opensnoop_perf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h opensnoop_perf

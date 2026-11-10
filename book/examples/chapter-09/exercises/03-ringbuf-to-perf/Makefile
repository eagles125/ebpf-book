# 第 9 章练习 03-ringbuf-to-perf 构建脚本（libbpf-bootstrap 模式，与基线 04-ring-buffer 一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: perf_exec

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

perf_exec.bpf.o: perf_exec.bpf.c perf_exec_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

perf_exec.skel.h: perf_exec.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

perf_exec: perf_exec.c perf_exec_common.h perf_exec.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h perf_exec

# 第 9 章示例 04-ring-buffer 构建脚本（libbpf-bootstrap 模式）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: ringbuf_exec

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

ringbuf_exec.bpf.o: ringbuf_exec.bpf.c ringbuf_exec_common.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

ringbuf_exec.skel.h: ringbuf_exec.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

ringbuf_exec: ringbuf_exec.c ringbuf_exec_common.h ringbuf_exec.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h ringbuf_exec

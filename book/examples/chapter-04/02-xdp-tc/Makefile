# 第 4 章示例构建脚本（libbpf-bootstrap 模式，与第 2/3/17 章示例一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）
# 安全警告：加载未经验证的 eBPF 程序会影响系统行为——本脚本只编译不加载。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: xdp_tc

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

xdp_count.bpf.o: xdp_count.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

tc_count.bpf.o: tc_count.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

xdp_count.skel.h: xdp_count.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

tc_count.skel.h: tc_count.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

xdp_tc: xdp_tc.c xdp_count.skel.h tc_count.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h xdp_tc

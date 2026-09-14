# 第 9 章示例 09-netfilter-go 构建脚本。
# 用法：make（生成 vmlinux.h -> 编译 BPF 数据面 -> 编译 Go 控制面）
# 首次构建前先执行：go mod tidy（生成 go.sum）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
GO ?= go

.PHONY: all clean

all: nfctl

bpf/vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

bpf/nf_guard.bpf.o: bpf/nf_guard.bpf.c bpf/vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Ibpf -c $< -o $@

nfctl: bpf/nf_guard.bpf.o cmd/nfctl/main.go
	cd cmd/nfctl && $(GO) build -o ../../nfctl

clean:
	rm -f bpf/*.o bpf/vmlinux.h nfctl

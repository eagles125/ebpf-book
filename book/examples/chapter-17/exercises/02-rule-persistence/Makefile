# 第 17 章练习 02（规则持久化）构建脚本：BPF 数据面 + Go 控制面联合构建。
# 构建体系复用 ../../05-firewall 的 Makefile 模式（bpf/ 与 user/ 目录结构一致）。
# 用法：make（首次构建前先执行 go mod tidy 拉取依赖）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
GO ?= go

.PHONY: all clean

all: fwctl

bpf/vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

bpf/fw_xdp.bpf.o: bpf/fw_xdp.bpf.c bpf/fw_common.h bpf/vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Ibpf -c $< -o $@

bpf/fw_tc.bpf.o: bpf/fw_tc.bpf.c bpf/fw_common.h bpf/vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Ibpf -c $< -o $@

fwctl: bpf/fw_xdp.bpf.o bpf/fw_tc.bpf.o user/go.mod user/main.go user/rules.go user/maps.go user/stats.go user/persist.go
	cd user && $(GO) build -o ../fwctl

clean:
	rm -f bpf/*.o bpf/vmlinux.h fwctl

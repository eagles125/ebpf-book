# Makefile —— 第 7 章 01-hash-lookup-update(libbpf-bootstrap 模式)
#
# 【用途与输入】
#   构建 hash 查找/更新示例:先从本机 BTF 生成 vmlinux.h,再编译 BPF 侧
#   xdp_hash.bpf.c 产出 .bpf.o,由 bpftool 生成 skeleton 头,最后 gcc 链接
#   用户态 loader 产出可执行 hash_count。无输入参数;构建产物均不入库
#   (.gitignore 覆盖 vmlinux.h/*.skel.h/*.o/hash_count)。
#
# 【输出结构】
#   all(默认)= vmlinux.h → xdp_hash.bpf.o → xdp_hash.skel.h → hash_count;
#   clean 删除全部产物。
#
# 【依赖与降级】
#   clang(-target bpf)、bpftool(btf dump + gen skeleton)、gcc + libbpf
#   (-lbpf -lelf -lz)。任何一环缺失 make 即失败——编译级验证由此承担,
#   本文件不做降级假成功。
#
# 【失败模式】
#   - bpftool btf dump 失败:内核无 BTF(/sys/kernel/btf/vmlinux 缺失);
#   - gen skeleton 失败:bpftool 版本过旧(<0.30 的 strict 模式差异);
#   - 链接失败:libbpf 开发包缺失(apt install libbpf-dev)。
#   验证状态:本 Makefile 产物构建=编译级已验证;hash_count 的运行需 root
#   (预期输出示例 / 运行级待验证)。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
CC ?= gcc

# 多架构头目录:linux/types.h 里 #include <asm/types.h>,BPF 目标下需要
# 显式给出宿主 multiarch 路径(与第 6 章 01-bpf-asm 同款)
MULTIARCH := $(shell gcc -print-multiarch 2>/dev/null)
INCLUDES := -I$(shell $(CLANG) -print-file-name=include) -I/usr/include/$(MULTIARCH)
CFLAGS := -g -Wall -Wextra

.PHONY: all clean

all: hash_count

# 从当前内核 BTF 生成类型头文件(CO-RE 的基础,构建产物不入库)
vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

xdp_hash.bpf.o: xdp_hash.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

# skeleton 头文件名与文件内符号名(xdp_hash_bpf,由 bpftool 按 .bpf.o 净化生成)保持一致
xdp_hash_bpf.skel.h: xdp_hash.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

hash_count: loader.c xdp_hash_bpf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h hash_count

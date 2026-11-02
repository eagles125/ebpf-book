# Makefile —— 第 8 章 01-btf-kinds(仅编译,不链接、不加载)
#
# 【用途与输入】
#   编译刻意覆盖多 BTF kind 的教学样例 kinds.bpf.c,产出携带 .BTF 的
#   kinds.bpf.o,供 bpftool btf dump / llvm-objdump -h 做只读剖析
#   (README 给逐行指认)。无输入参数;源码自包含(不 include 任何内核头),
#   不依赖 /sys/kernel/btf,也不生成 vmlinux.h——那是 02/03 资产的事。
#
# 【输出结构】
#   all(默认)= kinds.bpf.o(只编译,不链接;无 skeleton、无用户态可执行);
#   clean 删除产物。kinds.bpf.o 由 .gitignore 全局规则(book/examples/**/*.o)
#   覆盖,不入库。
#
# 【依赖与降级】
#   仅 clang(需带 BPF 后端,-target bpf)。无 libbpf、无 bpftool 依赖——
#   bpftool 只在验证步骤用(README 中的剖析命令),不在构建链上。
#   任何一环缺失 make 即失败,不做降级假成功。
#
# 【失败模式】
#   - clang 缺 BPF 后端:-target bpf 报错(apt install clang);
#   - 其余失败均视为源码/编译器问题,原样透传 clang 诊断。
#   验证状态:本 Makefile 产物构建=编译级已验证(本机 clang 18.1.3 实测);
#   本示例不加载,无运行级档位。

CLANG ?= clang

# 编译级验证的标准旗标:调试信息(-g,产 .BTF 的前提)+ 优化(-O2,与
# 03 等 BPF 侧编译同口径)+ BPF 目标 + 架构宏(无 bpf_helpers 时仅占位,
# 与本章其余 .bpf.c 保持同一命令行)。
CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_x86

.PHONY: all clean

all: kinds.bpf.o

kinds.bpf.o: kinds.bpf.c
	$(CLANG) $(CFLAGS) -c $< -o $@

clean:
	rm -f kinds.bpf.o

# Makefile —— 第 8 章练习① 01-relo-kind-detect(两步链:头文件 → 可切片对象)
#
# 【用途与输入】
#   仿 ../03-core-read/Makefile 的模式,裁到本练习需要的两步:
#   vmlinux.h(←bpftool btf dump format c)→ demo.bpf.o(←clang -g,
#   -g 产 .BTF/.BTF.ext——3.3 的命门:漏了 -g 就没有重定位记录可切片)。
#   无输入参数;唯一外部输入是 /sys/kernel/btf/vmlinux(运行内核 BTF,
#   mode 444 全局可读,无需 root)。
#
# 【输出结构】
#   all(默认)= demo.bpf.o。中间产物 vmlinux.h;clean 删除全部产物。
#   两类产物均不入库:由 .gitignore 全局规则(book/examples/**/*.o、
#   **/vmlinux.h)覆盖。
#
# 【依赖与降级行为】
#   bpftool、clang(带 BPF 后端)。vmlinux.h 规则与 ../02-vmlinux-h/
#   gen-vmlinux-h.sh 的主干等价;任何一环缺失 make 即失败,不做降级假成功。
#
# 【失败模式(退出码)】
#   - /sys/kernel/btf/vmlinux 不可读(内核未开 CONFIG_DEBUG_INFO_BTF):
#     bpftool btf dump 失败,make 停在第一步(排错指引见 ../../00-env/);
#   - clang 缺 BPF 后端:-target bpf 报错(apt install clang);
#   - 其余失败原样透传编译器诊断。
#   验证状态:本 Makefile=编译级已验证(本机 clang 18.1.3 + bpftool 7.7.0
#   实测,make 零告警产 demo.bpf.o);本对象不加载(见 relo-demo.bpf.c 头注)。
#
# 【安全声明】本文件只做编译;不加载 BPF 程序、不触碰内核状态。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)

.PHONY: all clean

all: demo.bpf.o

# 第一步:从当前内核 BTF 生成类型头文件(机制解释入口在 ../../02-vmlinux-h/;
# 本练习只用它提供 __u32 等基础类型,自编类型不依赖内核布局)
vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

# 第二步:编译 BPF 对象(-g 是命门:没有它连 .BTF.ext 都不产,切片器站 1 即 FAIL)
demo.bpf.o: relo-demo.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

clean:
	rm -f demo.bpf.o vmlinux.h

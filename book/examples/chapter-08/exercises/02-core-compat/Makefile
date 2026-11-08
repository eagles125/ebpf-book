# Makefile —— 第 8 章练习② 02-core-compat(两步链:头文件 → 兼容分支对象)
#
# 【用途与输入】
#   仿 ../03-core-read/Makefile 的模式,裁到本练习需要的两步:
#   vmlinux.h(←bpftool btf dump format c)→ compat.bpf.o(←clang -g,
#   -g 产 .BTF/.BTF.ext——兼容分支的两个判定宏都靠重定位记录在加载期
#   常量化,没有 -g 记录无处安放)。无输入参数;唯一外部输入是
#   /sys/kernel/btf/vmlinux(运行内核 BTF,mode 444 全局可读,无需 root)。
#
# 【输出结构】
#   all(默认)= compat.bpf.o。中间产物 vmlinux.h;clean 删除全部产物。
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
#   - 探测字段/类型不在本机 vmlinux.h:clang 报 no member / unknown type
#     ——本机 BTF 里得先有才可编译,这正是「先看本机 BTF 再写代码」的
#     工作流(正文 3.4;探测对象怎么选见 README 提示);
#   - 其余失败原样透传编译器诊断。
#   验证状态:本 Makefile=编译级已验证(本机 clang 18.1.3 + bpftool 7.7.0
#   实测,make 零告警产 compat.bpf.o);加载运行为预期档(见 README)。
#
# 【安全声明】本文件只做编译;加载 BPF 程序属运行级待验证(需 root),
#   仅挂 tracepoint 跟踪点、只读任务字段,请在实验环境进行。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)

.PHONY: all clean

all: compat.bpf.o

# 第一步:从当前内核 BTF 生成类型头文件(CO-RE 的基础,构建产物不入库;
# 这条规则的机制解释入口在 ../../02-vmlinux-h/)
vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

# 第二步:编译 BPF 对象(-g 产 .BTF/.BTF.ext;vmlinux.h 文件头的
# preserve_access_index 让每个字段访问带上 CO-RE 重定位记录)
compat.bpf.o: compat.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

clean:
	rm -f compat.bpf.o vmlinux.h

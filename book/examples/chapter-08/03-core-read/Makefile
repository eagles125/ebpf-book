# Makefile —— 第 8 章 03-core-read(ch09 bootstrap 模式四步构建链)
#
# 【用途与输入】
#   与 ch09 各示例同款的四步链:02-vmlinux-h 拆解过的「构建链第一环」在此
#   回收入链——vmlinux.h(←bpftool btf dump)→ core.bpf.o(←clang -g,
#   按本机 BTF 偏移编译 + .BTF.ext 重定位记录)→ core_bpf.skel.h(←bpftool
#   gen skeleton)→ core_read(←gcc -lbpf -lelf -lz)。无输入参数;唯一外部
#   输入是 /sys/kernel/btf/vmlinux(运行内核 BTF,mode 444 全局可读,无需 root)。
#
# 【输出结构】
#   all(默认)= core_read(用户态加载器)。中间产物:core.bpf.o、
#   core_bpf.skel.h、vmlinux.h;clean 删除全部产物。四类产物均不入库:
#   前三者由 .gitignore 全局规则(book/examples/**/*.o、**/*.skel.h、
#   **/vmlinux.h)覆盖,可执行 core_read 由 .gitignore 逐文件条目覆盖。
#
# 【依赖与降级行为】
#   bpftool、clang(带 BPF 后端)、gcc、libbpf(-lbpf)+ libelf + zlib。
#   vmlinux.h 规则与 ../02-vmlinux-h/gen-vmlinux-h.sh 的主干等价;
#   任何一环缺失 make 即失败,不做降级假成功。
#
# 【失败模式(退出码)】
#   - /sys/kernel/btf/vmlinux 不可读(内核未开 CONFIG_DEBUG_INFO_BTF):
#     bpftool btf dump 失败,make 停在第一步(排错指引见 ../00-env/);
#   - clang 缺 BPF 后端:-target bpf 报错(apt install clang);
#   - 链接缺库:ld 报 cannot find -lbpf(apt install libbpf-dev);
#   - 其余失败原样透传编译器/链接器诊断。
#   验证状态:本 Makefile 全链=编译级已验证(本机 clang 18.1.3 + bpftool
#   7.7.0 + gcc 实测,make 零告警产 core_read);core_read 的运行=预期档
#   (需 root,运行级待验证,见 README)。
#
# 【安全声明】本文件只做编译与链接;加载 BPF 程序由编译产物 core_read 在
#   root 档完成(仅挂 tracepoint 跟踪点、只读少量任务字段,见 core.bpf.c)。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: core_read

# 第一步:从当前内核 BTF 生成类型头文件(CO-RE 的基础,构建产物不入库;
# 这条规则的机制解释入口在 ../02-vmlinux-h/)
vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

# 第二步:编译 BPF 对象(-g 产 .BTF/.BTF.ext;vmlinux.h 文件头的
# preserve_access_index 让每个字段访问带上 CO-RE 重定位记录)
core.bpf.o: core.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

# 第三步:从 BPF 对象生成 skeleton 头(对象名默认取自文件名:core.bpf → core_bpf)
core_bpf.skel.h: core.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

# 第四步:链接用户态加载器
core_read: loader.c core_bpf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h core_read

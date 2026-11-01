# Makefile —— 第 7 章 03-event-output(ringbuf + perf 双变体,libbpf-bootstrap 模式)
#
# 【用途与输入】
#   同一事件源(execve tracepoint)构建两条输出通道的完整双可执行:
#   event_ringbuf(ringbuf 变体)与 event_perf(perf buffer 变体)。
#   无输入参数;构建产物均不入库。
#
# 【输出结构】
#   all(默认)= vmlinux.h + 两个 .bpf.o + 两个 skeleton 头 + 两个可执行;
#   clean 删除全部产物。
#
# 【依赖与降级】
#   clang/bpftool/gcc+libbpf 同前两个示例;任一环缺失即失败,不假成功。
#
# 【失败模式】
#   - bpftool btf dump 失败:内核无 BTF;
#   - perf_buffer__new 运行期失败:perf_event_open 档位不足(root 侧);
#   验证状态:构建=编译级已验证;两个可执行的运行需 root(预期输出示例)。

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
CC ?= gcc

# 多架构头目录:linux/types.h 里 #include <asm/types.h>,BPF 目标下需显式给出
MULTIARCH := $(shell gcc -print-multiarch 2>/dev/null)
INCLUDES := -I$(shell $(CLANG) -print-file-name=include) -I/usr/include/$(MULTIARCH)
CFLAGS := -g -Wall -Wextra

.PHONY: all clean

all: event_ringbuf event_perf

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

# -Wno-missing-declarations:抑制生成文件 vmlinux.h 的翻译残缺告警
# (bpftool 对个别内核结构的空声明;内核 7.0.0-38 实测 7 处,非示例代码问题)
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -Wno-missing-declarations

ringbuf.bpf.o: ringbuf.bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

perf.bpf.o: perf.bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# skeleton 头文件名与文件内符号名(ringbuf_bpf / perf_bpf)保持一致
ringbuf_bpf.skel.h: ringbuf.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

perf_bpf.skel.h: perf.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

event_ringbuf: ringbuf.c ringbuf_bpf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

event_perf: perf.c perf_bpf.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h event_ringbuf event_perf

/* xdp_count.bpf.c —— 第 6 章《JIT 编译与性能优化》反汇编锚点程序
 *
 * 文件用途:01-bpf-asm 示例的源码锚点。用 clang 编译成 BPF 目标码后,
 * 用 llvm-objdump -d 反汇编,即可在本目录 README 的逐行对照表里看到
 * 「源码 → BPF 字节码」的一一对应。
 *
 * 教学目标:亲手看到源码 → BPF 字节码的对应关系——一次 ctx 指针加载、
 * 一次边界检查、一次 IPv6 以太类型比较、三个返回值,共 14 条指令。
 *
 * 安全声明:本程序只编译不加载,无挂载风险——不经过 bpf() 系统调用,
 * 不会触碰运行内核;验证范围仅到「编译级 + 反汇编级」。
 */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_count(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;

	if ((void *)(eth + 1) > data_end)
		return XDP_ABORTED;

	if (eth->h_proto == __builtin_bswap16(0x86dd))
		return XDP_DROP;

	return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

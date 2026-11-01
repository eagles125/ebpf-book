/* xdp_percpu.bpf.c —— 第 7 章 02-percpu-counter 的 BPF 侧
 *
 * 文件用途:演示 per-CPU 数组映射的「零锁计数」——每个 CPU 各有一份独立的
 * 值槽,XDP 收包只动本 CPU 的槽,完全无跨 CPU 争用;聚合留给用户态(loader)。
 *
 * 教学要点:BPF_MAP_TYPE_PERCPU_ARRAY 的 value 在每个 CPU 上各一份;
 * bpf_map_lookup_elem 返回的是**本 CPU** 槽位的指针——这是 per-CPU 语义与
 * 普通 hash/array 的核心差别(正文三节)。
 *
 * 安全声明:本程序只编译不加载;加载由 loader.c 在 root 档完成(运行级待验证),
 * XDP 只允许挂 lo,严禁挂物理网卡。
 */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>

/* per-CPU 数组:key=下标(固定用 0 号槽),value=每 CPU 一个 __u64 计数 */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} pkt_count SEC(".maps");

SEC("xdp")
int percpu_count(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;
	__u32 key = 0;
	__u64 *cnt;

	if ((void *)(eth + 1) > data_end)
		return XDP_ABORTED;

	/* per-CPU 语义:返回本 CPU 槽位,无锁自增 */
	cnt = bpf_map_lookup_elem(&pkt_count, &key);
	if (cnt)
		__sync_fetch_and_add(cnt, 1);

	return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

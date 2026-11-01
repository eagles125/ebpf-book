/* xdp_hash.bpf.c —— 第 7 章 01-hash-lookup-update 的 BPF 侧
 *
 * 文件用途:演示 hash 映射的「查找 + 更新」完整闭环——XDP 收到包后按以太类型
 * 计数:首包用 bpf_map_update_elem(BPF_NOEXIST)建项,后续包先
 * bpf_map_lookup_elem 拿到槽位再原子自增。
 *
 * 教学要点:hash 映射是「BPF 写、用户态读」的共享结构——BPF 侧只做 O(1) 的
 * 查改,读聚合交给 loader;NOEXIST 与 EXIST 两种语义在正文二节与源码
 * htab_map_update_elem(kernel/bpf/hashtab.c)对上。
 *
 * 安全声明:本程序只编译不加载;加载由 loader.c 在 root 档完成(运行级待验证),
 * XDP 只允许挂 lo,严禁挂物理网卡。
 */
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* hash 映射:key=以太类型(网络字节序),value=包计数。
 * max_entries=16 够教学用;满了以后 NOEXIST 建项会失败(E2BIG),loader 打印可见。 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16);
	__type(key, __be16);
	__type(value, __u64);
} proto_count SEC(".maps");

SEC("xdp")
int hash_count(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;
	__u64 one = 1;
	__u64 *cnt;

	if ((void *)(eth + 1) > data_end)
		return XDP_ABORTED;

	cnt = bpf_map_lookup_elem(&proto_count, &eth->h_proto);
	if (cnt) {
		/* 已存在:原子自增(EXIST 槽位;多 CPU 并发安全) */
		__sync_fetch_and_add(cnt, 1);
	} else if (bpf_map_update_elem(&proto_count, &eth->h_proto,
				       &one, BPF_NOEXIST) != 0) {
		/* 首包建项失败:表满(E2BIG)或并发竞争,放弃本包计数 */
		return XDP_ABORTED;
	}

	return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";

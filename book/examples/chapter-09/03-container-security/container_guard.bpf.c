// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章示例 03-container-security
 * （对应 ebpf.top BPF 入门系列实践教程三：借助 eBPF 保护容器安全）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_openat
 * 输出：bpf_printk -> trace_pipe
 *
 * 把过滤维度从 PID 扩展到容器：内核态用 bpf_get_current_cgroup_id()
 * 读取当前任务的 cgroup id，在 target_cgroups（HASH map）中查询是否命中
 * 目标容器；命中则读取被打开的文件名并输出事件。
 *
 * 重要边界：本示例是“观测型”探针，只记录不阻断。tracepoint 没有
 * 决策语义，不能改变系统调用的结果。要做真正的阻断，需要换用具备
 * 返回决策能力的挂载点（LSM BPF、LSM 钩子、XDP/TC/Netfilter 等）。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* 策略取值：当前示例只实现 POLICY_LOG；POLICY_DENY 是留给
 * LSM 版本的占位，在 tracepoint 上没有阻断语义。 */
#define POLICY_LOG  0
#define POLICY_DENY 1

/* key: cgroup id；value: 策略 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, __u64);
	__type(value, __u32);
} target_cgroups SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int guard_openat(struct trace_event_raw_sys_enter *ctx)
{
	char comm[16];
	char filename[128];
	__u64 cgid = bpf_get_current_cgroup_id();
	__u32 *policy = bpf_map_lookup_elem(&target_cgroups, &cgid);
	__u32 pid;

	if (!policy)
		return 0;	/* 不属于任何目标容器 */

	if (*policy != POLICY_LOG)
		return 0;	/* 未实现的策略，先不处理 */

	pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(comm, sizeof(comm));

	/* args[1] 是 openat 的第二个参数：用户态 filename 指针。
	 * tracepoint 篇（08-open-file-tracepoint）会详细解释 ctx->args。 */
	bpf_probe_read_user_str(filename, sizeof(filename),
				(void *)ctx->args[1]);

	bpf_printk("container-guard: cgid=%llu pid=%u comm=%s open(%s)\n",
		   cgid, pid, comm, filename);
	return 0;
}

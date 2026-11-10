// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 第 9 章练习 01-comm-filter
 * （02-pid-filter 的改写练习：把按 PID 过滤改为按进程名过滤）。
 *
 * 挂载点：tracepoint/syscalls/sys_enter_execve（与基线相同）
 * 输出：bpf_printk -> trace_pipe（与基线同形）
 *
 * 与基线 02-pid-filter 的唯一差别是过滤维度：ARRAY map 里保存的不再是
 * 目标 PID（4 字节整数），而是目标进程名（16 字节，TASK_COMM_LEN）。
 * 「不过滤」的约定由「值写 0」改为「写空串（首字节为 0）」。
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* map value 用命名结构体包一层 16 字节字符串：与 04–07 的事件结构同一
 * 纪律——类型有名字，补零约定有处可写（裸数组的取舍见 README 参考思路
 * 第 1 条）。 */
struct comm_value {
	char comm[16];
};

/* key 0 保存目标进程名；空串（首字节为 0）表示“不过滤，输出所有进程”。 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct comm_value);
} target_comm SEC(".maps");

/*
 * 逐字节比较两个 16 字节 comm，遇到 NUL 即判等。
 * 两侧约定一致才可靠：bpf_get_current_comm() 走 strscpy_pad——NUL 之后
 * 补零（kernel/bpf/helpers.c:264，v6.6.78 走读树）；用户态写 map 时也
 * 必须补零到 16 字节。bpf_strncmp() 的第二串只收编译期常量
 * （ARG_PTR_TO_CONST_STR），比不了 map 里的值，所以用有界循环。
 */
static bool comm_eq(const char *a, const char *b)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (a[i] != b[i])
			return false;
		if (!a[i])	/* 同一处 NUL：尾部都是补零，两串相等 */
			return true;
	}
	return true;
}

SEC("tracepoint/syscalls/sys_enter_execve")
int comm_filter(void *ctx)
{
	char comm[16];
	__u32 key = 0;
	struct comm_value *target;

	/* strscpy_pad：NUL 之后补零——与用户态写 map 的补零约定成对 */
	bpf_get_current_comm(comm, sizeof(comm));

	/* map 查找可能返回 NULL，必须先判断再解引用 */
	target = bpf_map_lookup_elem(&target_comm, &key);
	if (target && target->comm[0] != '\0' &&
	    !comm_eq(comm, target->comm))
		return 0;

	bpf_printk("execve: pid=%u comm=%s\n",
		   bpf_get_current_pid_tgid() >> 32, comm);
	return 0;
}

/* loader.c —— 第 8 章 03-core-read 的用户态加载器
 *
 * 【文件用途】
 *   skeleton(骨架)全链五步:open → load → attach → poll 一轮 → 销毁——
 *   把一个 CO-RE 程序从 .bpf.o 跑完整个生命周期后干净退出(一次性演示:
 *   不像 ch09 hello 常驻等信号,poll 一个 5 秒窗口就收摊)。
 *
 * 【对象名】skeleton 头 core_bpf.skel.h 由 bpftool 从 core.bpf.o 生成,
 *   对象名默认取自文件名(core.bpf → core_bpf),API 形如 core_bpf__open()。
 *
 * 【验证状态】编译级已验证(本机 gcc + libbpf 实测链接零告警);运行需
 *   root,输出为预期输出示例(运行级待验证,见 README)。
 *
 * 【安全声明】本程序只负责加载/挂载/销毁;BPF 侧仅只读任务字段(见 core.bpf.c)。
 */

#include <errno.h>
#include <stdio.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "core_bpf.skel.h"

/* 与 core.bpf.c 同布局的事件定义:骨架不搬运 BPF 侧类型,用户态自复读一份 */
struct exec_event {
	__u32 pid;
	char comm[16];
	__u32 kstack_offset;
	__u8 kstack_offset_exists;
};

/* ring_buffer__poll(轮询)回调:每个已提交事件调用一次 */
static int on_event(void *ctx, void *data, size_t len)
{
	const struct exec_event *e = data;

	(void)ctx;	/* 本例不需要上下文 */
	(void)len;	/* 长度恒为 sizeof(struct exec_event) */
	/* kstack_offset_exists=1:目标内核 task_struct 有该成员,兼容分支走
	 * 「有」路;放到 6.6 内核上跑会看到 =0 且 kstack_offset=0(哨兵值)
	 * ——CO-RE 兼容分支的另一半。 */
	printf("[core-read] pid=%-7u comm=%-16s kstack_offset_exists=%u kstack_offset=%u\n",
	       e->pid, e->comm, e->kstack_offset_exists, e->kstack_offset);
	return 0;
}

int main(void)
{
	struct core_bpf *skel;
	struct ring_buffer *rb = NULL;
	int err, n;

	/* 与 ch07 同款:显式声明走 libbpf 1.x 严格模式 API */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	/* 第 1 步 open:解析骨架内嵌的 .bpf.o,建 bpf_object */
	skel = core_bpf__open();
	if (!skel) {
		fprintf(stderr, "FAIL skeleton open\n");
		return 1;
	}

	/* 第 2 步 load:verifier 校验 + 装载。CO-RE 引擎在这一步把 .BTF.ext
	 * 里的重定位记录逐条按目标内核 BTF 应用(修正字段偏移与存在性) */
	err = core_bpf__load(skel);
	if (err) {
		fprintf(stderr, "FAIL skeleton load(err=%d):常见原因——权限不足"
			"(需 root)、内核过老(bpf_get_current_task_btf 需 5.11+)\n",
			err);
		core_bpf__destroy(skel);
		return 1;
	}

	/* 第 3 步 attach:按 SEC 名自动挂到 tracepoint/sched/sched_process_exec */
	err = core_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "FAIL attach(err=%d)\n", err);
		goto cleanup;
	}

	/* 建轮询器:绑定 ring buffer map(映射)的 fd,事件回调 on_event */
	rb = ring_buffer__new(bpf_map__fd(skel->maps.events), on_event, NULL, NULL);
	if (!rb) {
		fprintf(stderr, "FAIL ring_buffer__new\n");
		goto cleanup;
	}

	printf("已挂上 tracepoint/sched/sched_process_exec\n");
	printf("请在 5 秒内在另一个终端执行任意命令(如 ls、/bin/true)……\n");

	/* 第 4 步 poll 一轮:单次调用,窗口 5 秒,期间触发的 exec 事件全部
	 * 收完即返回;返回值=消费的事件数(负数=出错,-EINTR 视为正常收摊) */
	n = ring_buffer__poll(rb, 5000);
	if (n < 0 && n != -EINTR) {
		fprintf(stderr, "FAIL ring_buffer__poll(err=%d)\n", n);
		goto cleanup;
	}
	printf("poll 一轮结束:收到 %d 条事件,销毁对象退出\n", n < 0 ? 0 : n);

cleanup:
	/* 第 5 步销毁:轮询器与骨架对象依序释放(ring_buffer__free 对 NULL 安全) */
	ring_buffer__free(rb);
	core_bpf__destroy(skel);
	return 0;
}

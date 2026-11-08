/* relo-demo.bpf.c —— 第 8 章练习① 切片判读素材(BPF 侧)
 *
 * 【文件用途】
 *   刻意包含多种 CO-RE 重定位记录的最小对象,供 04-relo-slicer 切片判读
 *   「每条记录补丁什么、怎么补」(kind 语义)。覆盖三种 kind:
 *     - kind 0(BPF_CORE_FIELD_BYTE_OFFSET):BPF_CORE_READ 读字段;
 *     - kind 2(BPF_CORE_FIELD_EXISTS):bpf_core_field_exists 兼容分支;
 *     - kind 9(BPF_CORE_TYPE_SIZE):bpf_core_type_size 问目标类型大小
 *       (uapi bpf.h:7272;注意 kind 6 是 TYPE_ID_LOCAL、kind 9 是 TYPE_SIZE,
 *       对照口径=13 种,uapi bpf.h:7262-7276)。
 *   类型全部自编(仿 01-btf-kinds 的 demo 风格),不依赖内核类型漂移;
 *   参数指针当根指针用(正文 3.3 探针同款手法)。
 *
 *   本对象只为编译级/切片级练习:**不加载**。自编类型不在任何目标内核
 *   BTF 里,真加载会因候选为空按 kind 分流毒化指令(正文 4.2 的失败
 *   语义,预期失败而非事故)——README「参考思路」有展开。
 *
 * 【验证状态】编译级已验证(本机 clang 18.1.3 实测产 demo.bpf.o,
 *   切片器五站全过、RELOS=5);不涉及加载运行。
 *
 * 【安全声明】仅编译与切片,不加载、不 attach;程序体只做整数运算,
 *   不读内核内存(bpf_probe_read_kernel 由宏族引入但永不执行)。
 */

/* 关掉 vmlinux.h 文件头的 preserve_access_index「批发」通道(正文 3.2):
 * 让每条重定位记录都能对到一行显式宏族调用(「零售」),切片判读时
 * 「源码一行 ↔ 记录一条」的对应最干净。 */
#define BPF_NO_PRESERVE_ACCESS_INDEX
/* 本机内核(7.0)BTF 生成的 vmlinux.h 里有几处「无声明符声明」(BTF 里悬在
 * struct/union 内部的 FWD 构造,bpftool 原样发射),clang 18 会告警
 * -Wmissing-declarations;属生成头文件的固有噪音,与本示例代码无关,故在
 * include 处局部静音,保持 make 全链零告警(vmlinux.h 本身不动,与
 * ../03-core-read/ 同款处理)。 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* 判读对象①:字段类记录的根类型。extra_mode 是「假想的新版本字段」——
 * 在本机 BTF(vmlinux.h)存在才可编译;兼容分支借它演示 kind 2。 */
struct demo_config {
	int mode;
	int flags;
	int extra_mode;
};

/* 判读对象②:类型类记录的根类型(kind 9 问它的目标内核大小)。 */
struct demo_event {
	__u32 pid;
	__u32 mode;
	__u32 flags;
	char tag[16];
};

SEC("socket")
int demo_detect(void *ctx)
{
	struct demo_config *cfg = (struct demo_config *)ctx;
	int mode;

	/* 记录 kind 0:读 mode(访问链 0:0)——立即数=本机字节偏移,
	 * 加载期由 CO-RE 引擎按目标内核 BTF 重算后改写 imm */
	mode = BPF_CORE_READ(cfg, mode);

	/* 记录 kind 2:问 extra_mode 在目标内核是否存在——补 1/0,
	 * 补什么值编译期无从知道,这正是 kind 2 存在的意义(正文 3.8 之②) */
	if (bpf_core_field_exists(cfg->extra_mode)) {
		/* 记录 kind 0:同字段再读一次(存在分支内) */
		mode = BPF_CORE_READ(cfg, extra_mode);
	}

	/* 记录 kind 9:问 demo_event 在目标内核的大小——访问串恒为 "0",
	 * 补目标内核 BTF 里该类型的字节数(类型类记录不带字段链) */
	if (bpf_core_type_size(struct demo_event) == sizeof(struct demo_event)) {
		/* 记录 kind 0:读 flags(访问链 0:1) */
		mode += BPF_CORE_READ(cfg, flags);
	}

	return mode;
}

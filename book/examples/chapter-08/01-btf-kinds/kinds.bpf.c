/* kinds.bpf.c —— 第 8 章 01-btf-kinds:刻意覆盖多 BTF kind 的教学样例
 *
 * 文件用途:用一个「什么都是一点点」的 C 文件编译出携带丰富 .BTF 的
 * kinds.bpf.o,供 `bpftool btf dump file kinds.bpf.o` 逐一指认 kind 与
 * 类型 ID 编址(正文二节的实证面)。
 *
 * 编译级已验证;本示例仅编译 BPF 对象,不加载。
 *   不写 SEC() 程序段、不产 skeleton、不链接用户态、不触碰内核:
 *   make 出 kinds.bpf.o 后,全部验证都是对 ELF 文件的只读剖析
 *   (bpftool btf dump / llvm-objdump -h,见 README)。
 *
 * 设计要点:让 11 类覆盖项全部落进 .BTF,不被 -O2 优化掉——
 *   1) 全部实体用外链接(不用 static):外链接符号 -O2 不删,
 *      其类型图(成员、参数、返回值引用的一切类型)随之整体保留;
 *   2) 每个自定义类型都被「活实体」引用(全局变量的类型、存活函数的
 *      参数/返回值):BTF 只保留活实体的类型图,没人引用的 typedef/
 *      struct 连同声明一起被裁掉(实测可见,见 README);
 *   3) 只读全局用 volatile const(BPF 只读全局惯用法,落 .rodata 段)。
 */

/* ---- 覆盖项 1:struct → STRUCT kind(复合类型:逐成员记尺寸/偏移/类型 ID) ---- */
struct demo_dev {
	int ifindex;             /* 成员基类型:INT kind(整数基类型,几乎每个字段都踩到) */
	char name[16];           /* 成员类型:ARRAY kind(数组,元素 char/INT,长度 16) */
	unsigned long long caps; /* 成员基类型:INT kind(无符号 64 位) */
};

/* ---- 覆盖项 2:union → UNION kind(成员共享偏移 0;bpftool 行式与 STRUCT 同族,vlen=成员数) ---- */
union demo_value {
	unsigned int u32_view;
	unsigned long long u64_view;
};
union demo_value demo_value_slot; /* 存活锚点:外链接全局变量引用 union,防止类型图被 -O2 裁掉 */

/* ---- 覆盖项 3:enum → ENUM kind(枚举常量表;值超 32 位会升级为 ENUM64,见 README 的 vmlinux 抽样) ---- */
enum demo_state {
	DEMO_IDLE = 0,
	DEMO_RUNNING = 1,
	DEMO_STOPPED = 2,
};

/* ---- 覆盖项 4:typedef → TYPEDEF kind(类型别名,一行指向被别名类型;须被活实体引用才保留) ---- */
typedef unsigned long long demo_u64;
typedef struct demo_dev demo_dev_t;

/* ---- 覆盖项 5:const → CONST kind(volatile const 全局:BPF 只读全局惯用法,落 .rodata 段) ---- */
volatile const int demo_limit = 100;

/* ---- 覆盖项 6:ptr → PTR kind(全局指针变量:VAR 挂 PTR,PTR 挂 STRUCT) ---- */
struct demo_dev *demo_dev_ptr;

/* ---- 覆盖项 7:array → ARRAY kind(独立全局数组:元素 INT、长度 8;struct 里 name[16] 是第二例) ---- */
int demo_ring[8];

/* ---- 覆盖项 8:全局变量 → VAR kind + DATASEC kind(变量一行 VAR;所在 ELF 段各一行 DATASEC) ---- */
volatile int demo_counter;            /* 未初始化外链接全局 → .bss(零页支持) */
volatile long demo_counter_init = 7;  /* 显式非零初始化 → .data */

/* ---- 覆盖项 9:func → FUNC kind / 覆盖项 10:func_proto → FUNC_PROTO kind ----
 * 参数与返回值把 PTR、CONST、TYPEDEF、ENUM、STRUCT 串进同一张类型图:
 *   返回 demo_u64 → TYPEDEF → INT;
 *   参数一 enum demo_state → ENUM;
 *   参数二 const demo_dev_t * → PTR → CONST → TYPEDEF → STRUCT。 */
demo_u64 demo_state_key(enum demo_state st, const demo_dev_t *dev)
{
	return (demo_u64)st + (demo_u64)(unsigned long long)dev;
}

/* ---- 覆盖项 11:btf_decl_tag → DECL_TAG kind(挂在声明上的标签;component_idx=-1 表示挂整体而非成员/参数) ---- */
__attribute__((btf_decl_tag("ch08-demo-decl-tag")))
volatile int demo_tagged_var = 5;

/* ---- 补位:btf_type_tag → TYPE_TAG kind(挂在「类型」而非「声明」上的标签) ----
 * vmlinux BTF 抽样无 TYPE_TAG 真实 kind 行(本机实况,gcc 构建未产 TYPE_TAG),
 * 改由本对象本地指认:VAR → PTR → TYPE_TAG → STRUCT 链。 */
struct demo_dev __attribute__((btf_type_tag("ch08-type-tag"))) *demo_tagged_ptr;

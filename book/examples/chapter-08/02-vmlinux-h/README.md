# 02-vmlinux-h —— 从运行内核 BTF 生成 vmlinux.h(ch07/ch09 构建链的机制解释入口)

**验证状态:编译级已验证**;本示例只读系统 BTF(/sys/kernel/btf/vmlinux),不构建、不加载任何 BPF 程序,无 root 可复现。

第 7 章三个示例与第 9 章九个示例的 Makefile 里都有一条同款规则:用 `bpftool btf dump` 从当前内核生成 vmlinux.h——那两章把它当「构建链第一环」直接使用,本目录把这条链环拆开看:**它做了什么、产物长什么样、为什么能当 BPF 程序的唯一类型源**。这是章正文「一、BTF:内核的类型说明书」(BTF→C 的转换)与「三、CO-RE 编译期」(文件头里 `preserve_access_index` 的伏笔)的实证面。

## 文件清单

| 文件 | 内容 |
|---|---|
| `gen-vmlinux-h.sh` | 生成脚本:`bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h` + 末尾规模统计;先写临时文件、校验非空再落盘,失败不破坏已有产物;可执行 |
| `README.md` | 本文:生成方法、规模统计、结构导读、ch07/ch09 构建链回收 |
| `vmlinux.h` | 产物(约 3.5MB),由 `.gitignore` 全局规则(`book/examples/**/vmlinux.h`)覆盖,**不入库**;重复生成无副作用 |

## 快速上手(本机实跑实录)

```console
$ ./gen-vmlinux-h.sh
== 源 BTF: /sys/kernel/btf/vmlinux(7119843 字节) ==
开始转换为 C 头文件(bpftool btf dump format c;视机器与 bpftool 版本可能数十秒,属正常)……
生成完成:vmlinux.h(耗时 1 秒)
== 规模统计(本机实况,其他内核数值不同) ==
  字节: 3500327(wc -c)
  行数: 166914(wc -l)
  struct 声明数: 13897(grep -c '^struct')
  enum 声明数: 2737(grep -c '^enum')
  typedef 数: 2695(grep -c '^typedef')
产物不入库:.gitignore 全局规则 book/examples/**/vmlinux.h 已覆盖;重复生成无副作用。
```

## 生成方法(一条命令的来龙去脉)

脚本的主干只有一行,完全可以手动执行:

```console
$ bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

拆开看三件事:

1. **数据源 `/sys/kernel/btf/vmlinux`**:运行内核把自己完整的 BTF(BPF Type Format,BPF 类型格式)挂在这里(本机实况:7119843 字节,mode 444 全局可读,无需 root)。它是内核构建期由 `pahole` 从 DWARF 转换、编进内核镜像的类型说明书——内核没开 `CONFIG_DEBUG_INFO_BTF=y` 就没有这个文件,脚本会给出排错指引后退出(exit 1);
2. **`btf dump ... format c`**:bpftool 遍历 BTF 的每一个类型 ID,把 INT/STRUCT/UNION/ENUM/TYPEDEF 等 kind 还原成等价的 C 声明——这一步是「BTF→C 的反序列化」,kind 体系见 `../01-btf-kinds/`;
3. **重定向落盘**:产物写到本目录。脚本多包了一层「临时文件 + 非空校验 + mv」:转换失败不留半截 vmlinux.h,已有的旧产物也不被破坏(失败 exit 2)。

> 对照一个容易混淆的口径:`bpftool btf dump file /sys/kernel/btf/vmlinux`(不带 `format c`)是**纯文本 dump**,本机实况 401978 行,每行一个类型(`[156855] STRUCT 'task_struct' size=9920 vlen=274`,见 01-btf-kinds);加上 `format c` 才是 **C 头文件**,166914 行——两个口径别混。

## 规模统计(本机实况,其他内核数值不同)

与脚本末尾输出同口径(`wc -c`/`wc -l`/`grep -c '^struct'`/`grep -c '^enum'`/`grep -c '^typedef'`):

| 统计项 | 命令 | 本机实况 |
|---|---|---|
| 源 BTF 大小 | `stat -c %s /sys/kernel/btf/vmlinux` | 7119843 字节 |
| 头文件字节 | `wc -c vmlinux.h` | 3500327 |
| 头文件行数 | `wc -l vmlinux.h` | 166914 |
| struct 起行数 | `grep -c '^struct'` | 13897(含 `struct X;` 形式的前置声明 2221 个) |
| enum 起行数 | `grep -c '^enum'` | 2737 |
| typedef 起行数 | `grep -c '^typedef'` | 2695 |

3.5MB、近 17 万行——这就是「把整个内核的类型系统塞进一个头文件」的实际体量:近 2 万个类型声明起行(约 1.4 万 struct、2.7 千 enum、2.7 千 typedef),随内核版本增减,你的数值几乎必然不同。

## 头文件结构导读(片段摘自实际生成文件)

先立一个正确预期:**文件不是按 kind 分区,而是按 BTF 类型 ID 顺序「先来后到」平铺**(类型 ID 编址见 01-btf-kinds)。「enum 区 / typedef 兼容区 / struct 声明区」是导读上的粗粒度视角——宏观上 enum 集中于前段(本机实况:首个 enum 在第 24 行)、typedef 从第 10300 行起、具名 struct 从第 33247 行起、`task_struct` 在第 139093 行;这些行号随内核版本漂移,读的时候请以符号搜索为准(如 `grep -n '^struct task_struct {' vmlinux.h`)。

### ① 文件头:CO-RE 的编译期开关(第 1–6 行实摘)

```c
#ifndef __VMLINUX_H__
#define __VMLINUX_H__

#ifndef BPF_NO_PRESERVE_ACCESS_INDEX
#pragma clang attribute push (__attribute__((preserve_access_index)), apply_to = record)
#endif
```

这六行是整个文件最有信息量的部分:`preserve_access_index` 告诉 clang——对本文件里**所有**结构体/联合体(apply_to = record)的成员访问生成「访问偏移重定位记录」。BPF 源码 `#include "vmlinux.h"` 后写下的每一个 `task->pid`,都会带着「字段在*本机*内核的偏移」这条记录进 `.BTF.ext`,等加载到其他内核时由 CO-RE 修正(正文三节;`03-core-read` 编译出实例、`04-relo-slicer` 切片指认)。想关掉这个行为,定义宏 `BPF_NO_PRESERVE_ACCESS_INDEX` 即可。紧随其后的 `__ksym`/`__weak`/`__bpf_fastcall` 宏定义,则是为引用内核符号(kfunc)预留的属性标记。

### ② enum 区(前段,第 24 行起实摘)

```c
enum {
	ACOMP_WALK_SLEEP = 1,
	ACOMP_WALK_SRC_LINEAR = 2,
	ACOMP_WALK_DST_LINEAR = 4,
};
```

大量内核枚举是匿名的,bpftool 原样发 `enum { ... }`(具名枚举如 `enum CSI_J {` 在第 10928 行出现)。宏观数:2737 个 enum 起行集中在前段(第 24 行到第 3 万余行,这一段约占全文件行数的五分之一)——枚举不需要字段偏移修正,在 CO-RE 里属于「存在性/取值」层面的重定位(`bpf_core_enum_value_exists`)。

### ③ typedef 兼容区(中段,第 10300 行起实摘)

```c
typedef _Bool bool;

typedef __int128 s_max;

typedef __int128 unsigned __u128;

typedef __u128 u128;

typedef u128 freelist_full_t;

typedef __int128 unsigned u_max;

typedef char *__guest_handle_char;

typedef char acpi_bus_id[8];
```

2695 个 typedef 是 vmlinux.h **单头自洽**的关键:内核源码里经 typedef 起名的类型(`pid_t`、`u128`、匿名 struct 的对外名……)在 BTF 里就是 TYPEDEF 节点,原样还原后,内核各处的声明链无需 include 任何内核头即可闭合。其中 `typedef struct { ... } BIT_CStream_t;` 一类(第 33102 行起)把匿名 struct 挂上对外名字,`typedef char acpi_bus_id[8];` 则连数组 typedef 都完整保留——「兼容区」之名由此而来:它兼容的是内核代码里**所有起名方式**。

### ④ struct 声明区(中后段,task_struct 第 139093 行起实摘)

```c
struct task_struct {
	struct thread_info thread_info;
	unsigned int __state;
	unsigned int saved_state;
	void *stack;
	refcount_t usage;
	unsigned int flags;
	unsigned int ptrace;
	int on_cpu;
	struct __call_single_node wake_entry;
	unsigned int wakee_flips;
	long unsigned int wakee_flip_decay_ts;
```

约 1.4 万个 struct 起行(含 2221 个 `struct X;` 形式的前置声明;FWD 前置声明同理,如第 34409 行的 `union efi_simple_text_input_protocol;`)是文件主体。挑 `task_struct`(274 个成员、9920 字节,本机实况)读两眼:成员间嵌套引用全靠 typedef 区与更早发射的 struct 定义闭合;成员顺序与偏移**忠实于本机内核**——CO-RE 要修正的「字段偏移漂移」,漂的就是这份布局与目标内核的差异。

### ⑤ 文件尾:成对出现的开关(实摘)

```c
#ifndef BPF_NO_PRESERVE_ACCESS_INDEX
#pragma clang attribute pop
#endif

#endif /* __VMLINUX_H__ */
```

与文件头的 `push` 成对;`#pragma clang attribute pop` 收回作用域,守卫宏收尾——整个文件无任何 `#include`,这就是「单头自洽」。

## 回收:ch07/ch09 构建链里的同款步骤

第 7、9 章每个示例的 Makefile 里都有这条规则(逐字摘自仓库文件):

```make
# 从当前内核 BTF 生成类型头文件(CO-RE 的基础,构建产物不入库)
vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@
```

- 第 7 章:`book/examples/chapter-07/01-hash-lookup-update/Makefile`、`02-percpu-counter/Makefile`、`03-event-output/Makefile` 三个示例同款;
- 第 9 章:`book/examples/chapter-09/01-hello-world/Makefile` 至 `09-netfilter-go/Makefile` 全部九个示例同款。

两章的行文都只把它当「构建链第一环」一笔带过,机制解释入口就在本目录:

1. **那条规则做了什么** = 本目录脚本的主干(见「生成方法」);产物即本目录导读的这份 3.5MB 头文件;
2. **为什么 BPF 程序必须用它**:BPF 目标下不能 `#include <linux/sched.h>`——内核头里满是内联函数、汇编与对编译器的假设,clang `-target bpf` 吃不下;vmlinux.h 用 BTF 还原的**纯数据类型**替身全部类型声明,单头自洽(上文的 typedef 兼容区就是为这事服务的);
3. **它在 CO-RE 链条中的位置**:include 了它,`preserve_access_index` 才有 record 可修饰,clang `-g` 才会把字段访问编成 `.BTF.ext` 重定位记录——第 7 章程序里每个 `BPF_CORE_READ`/普通字段访问能跨内核运行,第一环就是这里(编译期证据链见 `../03-core-read/` 与 `../04-relo-slicer/`)。

## 与章正文的对照

- BTF→C 的转换是「一、BTF:内核的类型说明书」的实证面;BTF 格式与 kind 权威定义在内核 uapi `include/uapi/linux/btf.h`(kind 枚举块 :60-79,v6.6.78,机检按符号);
- 文件头 `preserve_access_index` 是「三、CO-RE 编译期」的伏笔:`../03-core-read/` 编出带重定位的 `.bpf.o`,`../04-relo-slicer/` 切片指认每条记录;
- 类型 ID 的「先来后到」排放顺序 = `../01-btf-kinds/` 的编址观察放大到全内核;
- 本目录产物不入库,但生成可重复:任何时候 `rm vmlinux.h && ./gen-vmlinux-h.sh` 即可还原。

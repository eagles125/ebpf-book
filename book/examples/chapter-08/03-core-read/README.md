# 03-core-read —— 第一个 CO-RE 程序:读 task_struct 的 pid/comm

**验证状态:编译级已验证;加载运行为预期输出示例(运行级待验证,需 root)**——本机无 root 且 `kernel.unprivileged_bpf_disabled=2`,凡标「预期输出示例」的运行结果都不是本机实跑所得;编译级四步(`make` 全链、`llvm-objdump -h` 断言、`bpftool btf dump`、`bpftool gen skeleton`)为本机实跑实录(clang 18.1.3 + bpftool 7.7.0 实测)。

这是本章首个加载运行档的示例(其余资产均止于编译级):`core.bpf.c` 挂在 tracepoint(跟踪点)sched/sched_process_exec(任意进程成功 exec 时触发)上,用 helper(辅助函数)bpf_get_current_task_btf() 取当前任务的 `struct task_struct` 指针,用 BPF_CORE_READ 宏族读 pid/comm,放进事件后经 map(映射,类型为 ring buffer/环形缓冲区)发给 `loader.c` 打印。它是章正文「三、CO-RE 编译期」的实证面:`../01-btf-kinds/` 留下的「03 会看到 .BTF.ext 变大」在此兑现,`../02-vmlinux-h/` 拆解的 vmlinux.h 规则在此回收入链,重定位记录的逐条切片见 `../04-relo-slicer/`。

## 文件清单

| 文件 | 内容 |
|---|---|
| `core.bpf.c` | BPF 侧:tracepoint 上取 task_struct 指针、BPF_CORE_READ 读 pid/comm、`bpf_core_field_exists` 兼容分支,事件走 ring buffer |
| `loader.c` | 用户态加载器:skeleton(骨架)全链 open → load → attach → poll 一轮 → 销毁 |
| `Makefile` | ch09 bootstrap 模式四步链:vmlinux.h←bpftool btf dump;core.bpf.o←clang -g;core_bpf.skel.h←bpftool gen skeleton;core_read←gcc -lbpf -lelf -lz |
| 产物 `vmlinux.h`/`core_bpf.skel.h`/`core.bpf.o`/`core_read` | 均不入库:前三者被 `.gitignore` 全局规则(`book/examples/**/vmlinux.h`、`**/*.skel.h`、`**/*.o`)覆盖,可执行 core_read 由逐文件条目覆盖(本目录专属) |

## 快速上手(编译级,本机实跑实录)

```console
$ make
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -I/usr/lib/llvm-18/lib/clang/18/include -c core.bpf.c -o core.bpf.o
bpftool gen skeleton core.bpf.o > core_bpf.skel.h
cc -g -Wall loader.c -o core_read -lbpf -lelf -lz
$ echo $?
0
```

全链零告警(`exit=0`;vmlinux.h 本身有几处生成头的固有噪音,在 core.bpf.c 的 include 处局部静音,见源码注释)。四步各有编译级证据:

**① vmlinux.h 由当前内核 BTF 生成**——3.5MB、166914 行,与 `../02-vmlinux-h/` 同一产物(机制解释入口在那里)。

**② core.bpf.o 携带 `.BTF.ext`,且重定位记录在场**(clang 18 实测,`llvm-objdump -h core.bpf.o` 摘录——尺寸列为原文,省略 VMA 列;`#` 后是解读):

```console
$ llvm-objdump -h core.bpf.o
  3 tracepoint/sched/sched_process_exec     00000170 ...     # 程序本体:46 条指令
 16 .BTF                                    0000778a ...     # 类型图(30602 字节)
 18 .BTF.ext                                0000017c ...     # 380 字节 > 0:断言通过
 19 .rel.BTF.ext                            00000140 ...     # 320 字节
```

对照 `../01-btf-kinds/` 留下的悬念:kinds.bpf.o 的 `.BTF.ext` 只有 0x80=128 字节——那边没有任何 CO-RE 字段访问;本对象 0x17c=380 字节,多出的正是 core_relo 小节(内含下节的 4 条重定位记录)与更长的行号表。「03 会看到它变大」兑现。

`bpftool btf dump file core.bpf.o` 共 1498 行,task_struct 整张类型图被带进了对象:`[12] STRUCT 'task_struct' size=9920 vlen=274`(本机实况,其他内核数值不同;内核 BTF 里它叫 `[156855]`,对象 BTF 里重新编址为 `[12]`——类型 ID 是每份 BTF 自己的坐标,这正是加载期要按结构名重新对齐、而非照搬 ID 的原因,见 `../01-btf-kinds/` 的编址观察)。一个呼应 01 的细节:事件类型 `exec_event` 在对象 BTF 里 grep 不到(0 命中)——`bpf_ringbuf_reserve` 的 sizeof 在编译期折算成常数 28,没人再引用的类型图被 `-O2` 裁掉,「BTF 只描述活代码」。

**③ skeleton 生成**——`bpftool gen skeleton core.bpf.o > core_bpf.skel.h`,对象名默认取自文件名(core.bpf → core_bpf),头内守卫 `__CORE_BPF_SKEL_H__`,API 形如 `core_bpf__open()/__load()/__attach()`。

**④ 用户态链接**——`cc -g -Wall loader.c -o core_read -lbpf -lelf -lz`,零告警。

## 重定位记录长什么样(llvm-objdump -dr 摘录,clang 18 实测)

`llvm-objdump -dr core.bpf.o` 直接给答案——源码里 4 处字段访问,每处一条 CO-RE 重定位(3 条 `<byte_off>` + 1 条 `<field_exists>`,逐字摘录):

```asm
       9:	b7 01 00 00 f0 0a 00 00	r1 = 0xaf0
		0000000000000048:  CO-RE <byte_off> [12] struct task_struct::pid (0:94)
      18:	b7 01 00 00 10 0d 00 00	r1 = 0xd10
		0000000000000090:  CO-RE <byte_off> [12] struct task_struct::comm (0:129)
      25:	b7 01 00 00 01 00 00 00	r1 = 0x1
		00000000000000c8:  CO-RE <field_exists> [12] struct task_struct::kstack_offset (0:258)
      29:	b7 01 00 00 48 25 00 00	r1 = 0x2548
		00000000000000e8:  CO-RE <byte_off> [12] struct task_struct::kstack_offset (0:258)
```

逐条读:注解里 `[12]` 是对象 BTF 中 task_struct 的类型 ID,`(0:成员序)` 是访问链;三条 `<byte_off>` 的立即数正是**本机**字节偏移——0xaf0=2800(pid)、0xd10=3344(comm)、0x2548=9544(kstack_offset,成员序 258),加载时被 CO-RE 引擎改写成目标内核的对应值;第 25 条 `r1 = 0x1` 是 `bpf_core_field_exists` 编成的占位判定,加载时同样按目标内核改写成 1/0。

这就是「三、CO-RE 编译期」说的机制本体:**按本机偏移编译,同时把「本机偏移」登记成重定位记录**;`<field_exists>` 也是 uapi `include/uapi/linux/bpf.h` 里 `enum bpf_core_relo_kind` 的正式一员(`BPF_CORE_FIELD_EXISTS`,枚举值 2),不是 clang 的私货。顺手可认出各辅助函数的调用号:第 0 条 `call 0x9e` 即 158 号 `bpf_get_current_task_btf`。逐字节切片、逐记录判读是 `../04-relo-slicer/` 的事。

## 运行(预期输出示例 / 运行级待验证,需 root)

```console
$ sudo ./core_read
已挂上 tracepoint/sched/sched_process_exec
请在 5 秒内在另一个终端执行任意命令(如 ls、/bin/true)……
[core-read] pid=2314   comm=ls              kstack_offset_exists=1 kstack_offset=1849119156
poll 一轮结束:收到 1 条事件,销毁对象退出
```

(预期输出示例,运行级待验证:pid 与 kstack_offset 数值每次都不同;另一终端没有 exec 发生时,窗口收 0 条、打印「收到 0 条事件」后同样干净退出。)无 root 时的典型失败也是预期的一部分:

```console
$ ./core_read
FAIL skeleton load(err=-1):常见原因——权限不足(需 root)、内核过老(bpf_get_current_task_btf 需 5.11+)
```

## 字段漂移与兼容分支:bpf_core_field_exists

`task_struct` 的字段随内核版本漂移,这正是 CO-RE 的用武之地。本示例挑的演示字段 `kstack_offset` 给出了**双树本地实证**(两棵树都在手上,可自行核对):

- **本机内核(7.0)BTF**:`task_struct` 的成员,u32,字节偏移 9544、成员序 258(本机实况,`bpftool btf dump` 可复核);
- **6.6.78 走读树**:同名变量不是 task_struct 成员,而是 per-CPU 变量(`include/linux/randomize_kstack.h` :12 的 `DECLARE_PER_CPU(u32, kstack_offset)`;`sched.h` 的 task_struct 里 grep 0 命中)。

于是这条兼容分支(`core.bpf.c`)的三段故事是:

1. **编译期**:字段必须在本机 vmlinux.h(即本机 BTF)里存在才编得过——换成 6.6 的 vmlinux.h,`task->kstack_offset` 这行源码直接编译失败。这不是缺陷,而正是本章要教的工作流:**先看本机 BTF 有什么,再写代码**;
2. **加载期**:`bpf_core_field_exists(task->kstack_offset)`(宏定义见 6.6.78 树 `tools/lib/bpf/bpf_core_read.h` :132)编成的 `<field_exists>` 记录,libbpf 按目标内核 BTF 判定后把 1/0 常量写进寄存器(6.6.78 树 `relo_core.c` 的 `bpf_core_calc_field_relo`:有匹配 spec 返回 1,没有返回 0);
3. **死分支**:`if` 的「有字段」一侧引用了目标内核缺失的字段怎么办?libbpf 不报错,而是把那条指令**投毒**(`bpf_core_poison_insn`,替换成 `invalid func unknown#195896080`),而 verifier 只校验可达路径——常量化后的条件让死分支被淘汰。程序照样加载,永远只走「目标内核有啥读啥」的那条路。

落到输出上:本机(有该字段)打印 `kstack_offset_exists=1`;把这份 core.bpf.o 拿到 6.6 内核上跑(预期输出示例),会看到 `kstack_offset_exists=0 kstack_offset=0`(哨兵值)——同一份 .bpf.o,两种内核,各自读各自有的东西。

## Makefile:ch09 bootstrap 四步链的回收

`Makefile` 与 `book/examples/chapter-09/01-hello-world/` 同款(vmlinux.h 规则逐字一致),差异只在名字与产物:

| 步骤 | 规则 | 产物 |
|---|---|---|
| ① 类型源 | `bpftool btf dump file /sys/kernel/btf/vmlinux format c` | `vmlinux.h`(3.5MB,不入库) |
| ② 编译 BPF | `clang -g -O2 -target bpf -D__TARGET_ARCH_x86` | `core.bpf.o`(`-g` 是产 `.BTF`/`.BTF.ext` 的前提) |
| ③ 骨架 | `bpftool gen skeleton core.bpf.o` | `core_bpf.skel.h`(不入库) |
| ④ 链接 | `cc -g -Wall … -lbpf -lelf -lz` | `core_read`(逐文件条目不入库) |

第 ① 步为什么长这样、产物里有什么,机制解释入口在 `../02-vmlinux-h/`;第 ② 步产出的重定位记录如何被消费,见上文两节与 `../04-relo-slicer/`。

## 与章正文的对照

- BPF_CORE_READ 宏族的编译期展开:正文「三、CO-RE 编译期:从宏到重定位记录」,本目录给出第一条实编指令与立即数;
- 类型 ID 的对象内编址(内核 `[156855]` → 对象 `[12]`):`../01-btf-kinds/` 的编址观察;
- vmlinux.h 的来历与文件头 `preserve_access_index`:`../02-vmlinux-h/`;
- `.BTF.ext` 里 4 条记录的逐字节切片:`../04-relo-slicer/`;
- 加载期两台重定位引擎(libbpf `bpf_object__relocate_core` 与内核 `bpf_core_apply`)对这 4 条记录的接力处理:正文「四、两台重定位引擎」与 `../05-kernel-relo/`。

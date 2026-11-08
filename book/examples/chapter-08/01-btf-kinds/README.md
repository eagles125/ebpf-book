# 01-btf-kinds —— BTF kind 编译级剖析(自编对象指认 + vmlinux 抽样)

**验证状态:编译级已验证**;本示例仅编译 BPF 对象(kinds.bpf.o),不加载、不链接用户态、不触碰内核——全部验证动作(`bpftool btf dump`、`llvm-objdump -h`)都是对 ELF 文件的只读剖析,无 root 可复现。

本目录回答两个问题:①一个 C 源文件里的哪些构造会变成 BTF 的哪种 kind?②BTF 的类型 ID 是怎么编址的?做法刻意「简单粗暴」:写一个什么都是一点点的 `kinds.bpf.c`,编译出 `kinds.bpf.o`,用 `bpftool btf dump file` 把每一行 BTF 摘出来逐一指认;vmlinux 里有、自编对象里没有的剩余 kind,再从 `/sys/kernel/btf/vmlinux` 抽样。这是章正文「二、BTF kind 体系与类型 ID 编址」的实证面。

## 文件清单

| 文件 | 内容 |
|---|---|
| `kinds.bpf.c` | 刻意覆盖 11 类 BTF kind 的教学样例,每个覆盖项带中文行注;头部声明验证档位与「仅编译不加载」 |
| `Makefile` | `all` = 产 kinds.bpf.o(`clang -g -O2 -target bpf -D__TARGET_ARCH_x86`,仅编译不链接);`clean` 删产物 |
| `kinds.bpf.o` | 构建产物(6760 字节,本机实况),由 `.gitignore` 全局规则(`book/examples/**/*.o`)覆盖,不入库 |

## 快速上手

```console
$ make
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -c kinds.bpf.c -o kinds.bpf.o

$ llvm-objdump -h kinds.bpf.o        # 确认 .BTF 在场(本机实况,尺寸随工具链微差)
  15 .BTF                   00000515 ...
  17 .BTF.ext               00000080 ...
  18 .rel.BTF.ext           00000050 ...

$ bpftool btf dump file kinds.bpf.o  # 35 条类型记录,逐一指认见下表
```

`.BTF` 在场即「clang -g 把类型图编进了对象文件」;`.BTF.ext` 是按程序段组织的行号/重定位扩展信息(本对象没有 CO-RE 重定位,03-core-read 会看到它变大)。kind 枚举的权威定义在内核 uapi:`include/uapi/linux/btf.h` 的 kind 枚举块(:60-79,v6.6.78),共 20 种 kind(UNKN=0…ENUM64=19)。

## 覆盖表:源码项 → BTF kind → bpftool 输出行摘录

`bpftool btf dump file kinds.bpf.o` 全量输出 35 条类型记录(ID 1–35,物理 dump 行数含成员/取值行更多);下表按源码覆盖项摘录对应行(本机实况,clang 18.1.3 实测):

| # | 源码项 | BTF kind | bpftool 输出行摘录 |
|---|---|---|---|
| 1 | `struct demo_dev {...}` | STRUCT | `[5] STRUCT 'demo_dev' size=32 vlen=3`(成员行:`'ifindex' type_id=6 bits_offset=0`) |
| 2 | `union demo_value {...}` | UNION | `[22] UNION 'demo_value' size=8 vlen=2`(两个成员 `bits_offset=0`,共享偏移) |
| 3 | `enum demo_state {...}` | ENUM | `[1] ENUM 'demo_state' encoding=UNSIGNED size=4 vlen=3` |
| 4 | `typedef unsigned long long demo_u64;` | TYPEDEF | `[12] TYPEDEF 'demo_u64' type_id=10`(type_id 指向被别名类型) |
| 5 | `volatile const int demo_limit = 100;` | CONST | `[14] CONST '(anon)' type_id=15`→`[15] VOLATILE '(anon)' type_id=6`→`[6] INT 'int'` |
| 6 | `struct demo_dev *demo_dev_ptr;` | PTR | `[26] VAR 'demo_dev_ptr' type_id=25`→`[25] PTR '(anon)' type_id=5` |
| 7 | `int demo_ring[8];` | ARRAY | `[27] ARRAY '(anon)' type_id=6 index_type_id=9 nr_elems=8`(struct 内 `char name[16]` 是第二例:`[8]`) |
| 8 | 全局变量 `demo_counter` 等 | VAR + DATASEC | `[29] VAR 'demo_counter' type_id=15, linkage=global`;段表 `[33] DATASEC '.bss' size=0 vlen=5`、`[34] DATASEC '.data'`、`[35] DATASEC '.rodata'` |
| 9 | `demo_u64 demo_state_key(...)` | FUNC | `[13] FUNC 'demo_state_key' type_id=11 linkage=global` |
| 10 | (同上函数的签名) | FUNC_PROTO | `[11] FUNC_PROTO '(anon)' ret_type_id=12 vlen=2`(参数行:`'st' type_id=1`、`'dev' type_id=2`) |
| 11 | `__attribute__((btf_decl_tag(...)))` | DECL_TAG | `[21] DECL_TAG 'ch08-demo-decl-tag' type_id=20 component_idx=-1` |

三个「顺手带出」的 kind(不在 11 项覆盖清单里,但每次编译都会出现,指认时顺带认下):

| 源码项 | BTF kind | 输出行摘录 |
|---|---|---|
| 所有整数基类型(`int`/`char`/`long`/…) | INT | `[6] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED`;数组下标类型 `[9] INT '__ARRAY_SIZE_TYPE__'` |
| `volatile` 限定符 | VOLATILE | `[15] VOLATILE '(anon)' type_id=6`(与 CONST 同款「一行一限定符」链式结构) |
| `__attribute__((btf_type_tag(...)))` | TYPE_TAG | `[30] TYPE_TAG 'ch08-type-tag' type_id=5`(见下节:这是 vmlinux 抽样无命中后的本地补位) |

FUNC_PROTO 的参数二 `const demo_dev_t *dev` 是一张「限定符链」全图:FUNC_PROTO→PTR→CONST→TYPEDEF→STRUCT——kind 体系就是靠 `type_id` 把这串珠子穿起来的。

## 类型 ID 编址观察(自编对象 vs vmlinux,同一套规则)

- **从 1 连续递增,void=0 不出现**:本对象 35 个类型,ID 恰为 `[1]`…`[35]` 无空洞;ID 0 保留给 void(void 不占行,dump 里看不见)。
- **ID 是「类型区的排列序号」,与 kind 无关**:INT/STRUCT/FUNC/DATASEC 混在同一个编号空间里,先来后到(clang 按调试信息遇到的顺序发射,基类型去重共享——`int` 全文件只有一个 `[6]`,被 struct 成员、VAR、ARRAY 处处引用)。
- **交叉引用全靠 type_id 穿针**:FUNC(13)指向 FUNC_PROTO(11),FUNC_PROTO 指向参数/返回值类型,VAR 指向变量类型,DATASEC 末尾汇总各 VAR 及其段内偏移——`bpftool` 每行尾部的 `(VAR 'xxx')` 注释就是按这条链反查出来的。
- **DATASEC 收尾**:`[33]`…`[35]` 三个 DATASEC(`.bss`/`.data`/`.rodata`)排在编号末尾,与它们在对象文件里「汇总变量」的角色一致。
- **同一规则放大到 vmlinux**:编址方式一模一样,只是表大得多——`[156855] STRUCT 'task_struct' size=9920 vlen=274`(本机实况,其他内核数值不同)。类型 ID 因此是「目标内核 BTF 的坐标」,这正是 CO-RE 重定位里 type_id 修正的编址基础(正文三节)。

## 剩余 kind 的 vmlinux 抽样(FWD/FLOAT/ENUM64/TYPE_TAG)

自编对象没覆盖到的 kind,从运行内核的 BTF 抽样指认(命令对 vmlinux 全量 dump 约 40 万行做 grep;401978 行为本机实况)。**注意一个坑**:直接 `grep -m1 "FWD"` 命中的往往不是真实 kind 行,而是内核把自己的 kind 枚举编进了 BTF——`[5182] ENUM '(anon)' vlen=22` 的取值行(`'BTF_KIND_FWD' val=7`,枚举自 `include/uapi/linux/btf.h` 的匿名枚举,20 个 kind 值 + NR_BTF_KINDS + BTF_KIND_MAX 共 22 个枚举器,本机实况)。所以下面每类给两条:逐字值 grep(会先撞上枚举行)+ 锚定行首的 kind 行 grep(真正的指认)。

```console
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 "FWD"
	'BTF_KIND_FWD' val=7                      # ← 枚举行,不是 FWD 类型
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] FWD"
[82925] FWD 'POOL_ctx_s' fwd_kind=struct      # 本机实况:FWD=只声明未定义的占位(zstd 池结构)
```

```console
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] FLOAT"
[111308] FLOAT 'double' size=8                # 本机实况:vmlinux 仅此 1 行 FLOAT
```

```console
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] ENUM64"
[9733] ENUM64 '(anon)' encoding=UNSIGNED size=8 vlen=40   # 值超 32 位的枚举升级为 ENUM64
```

```console
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] TYPE_TAG"
(无命中,exit 1)                              # 本机实况:TYPE_TAG 0 行
```

各 kind 真实类型行计数(本机实况,`grep -cE "^\[[0-9]+\] <KIND>"`,其他内核数值不同):FWD 118、FLOAT 1、ENUM64 21、TYPE_TAG 0、DECL_TAG 0。也就是说本机 vmlinux(gcc 构建)没有 TYPE_TAG/DECL_TAG 行——**TYPE_TAG 改由自编对象本地指认**:`kinds.bpf.c` 末尾的 `btf_type_tag` 全局指针产出 `[30] TYPE_TAG 'ch08-type-tag' type_id=5`,类型链 VAR→PTR→TYPE_TAG→STRUCT;DECL_TAG 则由覆盖项 11 的 `[21]` 本地指认。这也顺便演示了一个事实:kind 的在场性取决于「源码写了什么 + 工具链产什么」,不是 BTF 格式的常量。

## 防优化:让 11 类覆盖项全落进 .BTF

`-O2` 只保留「活实体」的类型图,两个手法(源码头部注释同款说明):

1. **全部实体用外链接(不用 static)**:外链接的函数与变量 `-O2` 不删,其类型图随之保留;
2. **每个自定义类型都要有活引用**:`union demo_value` 若只声明不使用,连同 BTF 一起消失——所以源码里每个类型后面都跟着一个引用它的全局变量或函数签名(`demo_value_slot` 锚 union,`demo_state_key` 的参数/返回值锚 enum/typedef/struct)。

若把某个锚点改成 `static`(可自行实验),对应 kind 会从 dump 里消失——这正是「BTF 只描述活代码」的直接证据。

## 与章正文的对照

- kind 枚举权威定义:`include/uapi/linux/btf.h` kind 枚举块(:60-79,v6.6.78;机检按符号不按行号);
- 类型 ID 在 CO-RE 里的用途:正文「三、CO-RE 编译期」与 `04-relo-slicer` 的重定位记录 `type_id` 字段;
- 自编对象 vs vmlinux 的同构性:`02-vmlinux-h` 把后者的 40 万行变成 vmlinux.h。

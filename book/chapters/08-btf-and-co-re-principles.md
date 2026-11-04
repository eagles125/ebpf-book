# 第 8 章 BTF 与 CO-RE 原理

> 本章独立示例已落地到 [`book/examples/chapter-08/`](../examples/chapter-08/):[`00-env/`](../examples/chapter-08/00-env/) 环境自检、[`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/) 多 kind 剖析、[`02-vmlinux-h/`](../examples/chapter-08/02-vmlinux-h/) vmlinux.h 生成、运行型示例 [`03-core-read/`](../examples/chapter-08/03-core-read/) CO-RE 字段读取闭环、切片器 [`04-relo-slicer/`](../examples/chapter-08/04-relo-slicer/) 与 [`05-kernel-relo/`](../examples/chapter-08/05-kernel-relo/)、机检 [`scripts/`](../examples/chapter-08/scripts/)。本章是机制章,验证分三档:BTF 剖析与编译链(`bpftool btf dump`、`format c`、clang `-g` 编译)为编译级已验证(无 root 可复现);程序加载、attach 与字段修正后的实际读取一律标「预期输出示例 / 运行级待验证」(本机 `unprivileged_bpf_disabled=2`,无 root);DWARF→BTF 转换为方法论级(本机未装 pahole,只给流程与判读法)。行号引用一律以 v6.6.78 走读树实测为准。

## 学习目标

读完本章后,你应当能够:

- 说出 BTF(BPF Type Format,BPF 类型格式)的多重身份与「常驻内核」的代码证据:验证器 ctx 校验、`attach_btf_id`、字段级能力登记簿(第 7 章的 `btf_record`)三批内核内消费者,加上本章主线的 CO-RE 类型底座;据此解释「DWARF 大且只在编译期、BTF 紧凑且常驻内核」的取舍,并讲清 DWARF→BTF 转换在内核构建期由 pahole 完成(一节)。
- 对着 `struct btf_type` 与 19 种 kind(uapi `btf.h` 枚举)读出任意一条 BTF 记录:kind、vlen、size/type 三元组各管什么;解释类型 ID 从 1 顺序编址、void=0、跨 Section 共享命名空间的编址法,并用 `bpftool btf dump file` 对自编对象逐一指认 kind(二节)。
- 讲清 CO-RE 编译期的三件事:vmlinux.h 从哪来、`-g` 为什么是命门(没有 `-g` 就没有 `.BTF.ext`)、宏族(`BPF_CORE_READ` 系)如何把 C 表达式变成重定位记录;用切片器把一条 `.BTF.ext` 记录拆到四字段与访问字符串(三节)。
- 对比两台重定位引擎:libbpf 用户态引擎(服务普通骨架)与内核态引擎(服务轻骨架),说出各自的入口、候选查找、补丁对象与失败语义,并解释两套实现「逻辑同源、各自维护,接口对齐 uapi」(四节)。
- 沿一条 `BPF_CORE_READ` 的五层旅程(环境→生成→剖析→编译→加载补丁)全链走一遍,掌握缺失-BTF 场景的回退策略,守住「可移植≠绝对」的边界(五节)。

## 本章导读

第 5-7 章拆的是「程序怎么跑、依赖什么」:执行形态(第 6 章)、辅助函数与映射(第 7 章)。本章拆前七章反复留白的最后一块基础设施:BPF 程序凭什么「编译一次、多内核运行」——内核结构体每个版本都在增删调序,读 `task_struct->pid` 的程序换台机器凭什么读得对?答案的原料叫 BTF,消费方式叫 CO-RE(Compile Once - Run Everywhere,一次编译、处处运行)。定位一句话:**BTF 是内核的类型说明书,CO-RE 是消费这份说明书的方式**。路线:一节立起说明书本身;二节拆 kind 体系与类型 ID 编址;三节进编译期,看宏怎么变成重定位记录;四节摆两台重定位引擎;五节把五层实战连成一条线。

先把前文埋的预告逐条回收,共三点:

- 预告①(第 2 章 1.6 代码注释):「跨版本访问由 CO-RE 重定位在加载时修正」→ 三节兑现前半句(重定位记录怎么来),四节兑现后半句(加载时怎么补)——那句注释里的「修正」二字,本章给机制。
- 预告②(第 7 章总结):「vmlinux.h 从哪来、类型 ID 怎么编址、`bpf_core_read` 的重定位如何在加载期完成(本章示例构建链里的 `bpftool btf dump` 就是它的入口)」→ 本章正题:一节答「从哪来」,二节答「怎么编址」,三/四节答「怎么完成」。
- 预告③(第 7 章导读末):「第 8 章 BTF/CO-RE 之后,第 9 章起就是动手写程序了」→ 本章是动手篇前的最后一块地基:第 9 章起每个示例 Makefile 里 `bpftool btf dump` 生成 vmlinux.h 那一步,用的都是本章拆的机制。

六个示例资产各司其职,先对号再上车:

| 资产 | 演示什么 | 正文落点 |
|---|---|---|
| [`00-env/`](../examples/chapter-08/00-env/) | 环境自检:树/工具链/BTF 家族/CONFIG/pahole 五段 | 一节 1.5 |
| [`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/) | 自编对象覆盖多 kind,`btf dump` 逐一指认 | 二节 |
| [`02-vmlinux-h/`](../examples/chapter-08/02-vmlinux-h/) | `format c` 生成 vmlinux.h 与规模统计 | 一节 1.5、三节 |
| [`03-core-read/`](../examples/chapter-08/03-core-read/) | `BPF_CORE_READ` 读 task_struct 字段闭环 | 三节、五节 |
| [`04-relo-slicer/`](../examples/chapter-08/04-relo-slicer/) | python 切片器:`.BTF.ext` 五站 | 三节 |
| [`05-kernel-relo/`](../examples/chapter-08/05-kernel-relo/) | bash 切片器:内核引擎五站 | 四节 |

验证口径是本章的特色,三档先立好:编译级(本机无 root 实跑复现)、运行级(预期输出示例)、方法论级(pahole,给流程与判读法)。学法建议同前章:读到任何「为什么这么设计」,先去对应资产跑一眼现场再回来。

## 一、BTF:内核的类型说明书

### 1.1 一份「活」的说明书:BTF 是什么、常驻在哪

背景问题:写 BPF 程序要读 `task_struct->pid`,前提是知道 `pid` 排在结构体第几个字节——而这份布局每个内核版本都可能增删调序(第 2 章 1.6 的 tracepoint 字段漂移就是实例)。哪里有一份**权威、机器可读、随时可查**的类型说明书?

三种候选,前两种不合格:

- **源码头文件**(`include/linux/sched.h` 一类):权威,但 BPF 程序不能 include 内核头(类型依赖会拖进半个内核,第 2 章的 CO-RE 工作流因此只用 vmlinux.h);且源码树描述的是「这一版源码」的布局,不是「目标机正在跑」的布局。
- **DWARF**(调试信息标准格式,首现解释:编译器 `-g` 产出的调试数据,含类型、变量、行号、内联帧):全,但它是编译期产物,1.2 详谈内核为什么用不起。
- **BTF(BPF Type Format,BPF 类型格式)**:一份紧凑的类型图,编码在 `include/uapi/linux/btf.h`(kind 全集的枚举目录在 :60-79,v6.6.78),官方文档 `Documentation/bpf/btf.rst`(走读树在场)。名字里的 T(Type)是历史:内核文档开篇的原话是「The name BTF was used initially to describe data types」——它最初只为描述数据类型而生,后来才扩展出函数信息与行号信息(btf.rst:9-10);本章聚焦类型面。三种候选里只有它**常驻内核**。

「常驻」不是修辞,有最硬的代码证据。内核侧解析 vmlinux BTF 的函数 `btf_parse_vmlinux`(kernel/bpf/btf.c:5720,v6.6.78)开头没几行,就把数据指针指向了内核映像自己:

```c
/* kernel/bpf/btf.c:5734-5744(v6.6.78) */
	btf = kzalloc(sizeof(*btf), GFP_KERNEL | __GFP_NOWARN);
	if (!btf) {
		err = -ENOMEM;
		goto errout;
	}
	env->btf = btf;

	btf->data = __start_BTF;
	btf->data_size = __stop_BTF - __start_BTF;
	btf->kernel_btf = true;
	snprintf(btf->name, sizeof(btf->name), "vmlinux");
```

`__start_BTF`/`__stop_BTF`(:5577-5578 声明)是链接脚本圈出的 `.BTF` 段首尾——**说明书嵌在被描述者的映像里**:不依赖文件系统、不依赖用户态工具,内核一起来它就在。运行期在 `/sys/kernel/btf/vmlinux` 看到的文件(本机 7119843 字节、mode 444 全局可读;本机实况,其他内核数值不同)只是这段常驻数据的 sysfs 窗口——mode 444 意味着无特权的用户态也能把它读走,本机所有 `bpftool` 命令均为无 root 实跑。

常驻是为了喂给内核自己的消费者。BTF 因此有**三重内核身份**,外加本章主线的第四重:

1. **验证器 ctx 校验**:程序访问 `ctx->pid` 时,「这个偏移、这个宽度、读还是写」的合法性由 `btf_ctx_access`(kernel/bpf/btf.c:5926,v6.6.78)对照 ctx 类型的 BTF 判定——第 3 章 2.3 的「固定偏移读加载期静态放行、零运行时开销」,其登记册就是这份说明书;
2. **`attach_btf_id`**:tracing 类程序挂到某个内核函数上时,「函数参数类型对不对」由加载参数的 `attach_btf_id`(include/uapi/linux/bpf.h:1456,v6.6.78)指着 BTF 里的类型对账;
3. **字段级能力登记簿**:映射值里 `struct bpf_spin_lock` 在哪个偏移——第 7 章 2.3 的 `btf_record` 机制(建映射时校验值类型注解,`btf_record_has_field` 检查不过 -EINVAL)已经见过它,回指不展开;
4. **CO-RE 的类型底座**:编译期对本地 BTF、加载期对目标 BTF(二/三/四节)——本章正题,也是 BTF 最大的一单生意。

一句话立骨:BTF 不只服务 CO-RE;**正因为它要常驻内核喂验证器,才被设计得紧凑到可以常驻**——紧凑性与常驻性互为因果。这是理解后续一切设计的钥匙。

### 1.2 教师视角:为什么不用 DWARF

既然编译器本来就产全量类型信息(DWARF),为什么不直接用?课堂论证三个理由:一个「能不能」,两个「值不值」。

- **能不能:内核装不下 DWARF 解析器**。DWARF 多版本共存、结构复杂(缩写表、压缩表),内核内建解析器的标准是零外部依赖、可自我校验;`btf_parse_vmlinux` 全身只有 kzalloc 与逐段校验——`btf_parse_hdr`(:5746)、`btf_parse_str_sec`(:5752)、`btf_check_all_metas`(:5756)三步走完(btf.c,v6.6.78)。DWARF 解析链塞不进这个标准。
- **值不值一:体积与驻留**。DWARF 大半内容(行号表、变量位置、内联帧)是给人调试用的;验证器只需要「类型图」——类型、成员、偏移。BTF 按需裁剪再**去重**(Kconfig help 的原话:deduplicated),本机 vmlinux BTF 7119843 字节,这个尺寸才嵌得进内核映像(1.1 的 `__start_BTF`);DWARF 的量级嵌不起,也没必要跟着内核驻留。
- **值不值二:语义匹配**。BTF 的编码从头就是给程序化消费者设计的:`struct btf_type` 三元组加定长成员表,内核用数组下标寻址(二节拆编码);DWARF 的丰富度对验证器是纯负担。

一张表收束:

| | DWARF | BTF |
|---|---|---|
| 生成者 | 编译器(`-g`) | pahole 从 DWARF 提炼(内核映像);clang `-g` 直出(BPF 目标,1.3) |
| 内容 | 类型+行号+变量+内联,给人调试 | 类型图,给机器消费 |
| 去重 | 否(逐编译单元,同型重复) | 是(Kconfig help 明言 deduplicated) |
| 驻留 | 构建产物,留在磁盘 | 嵌入内核映像 + /sys/kernel/btf |
| 消费者 | 调试器等用户态工具 | 验证器/attach/登记簿/CO-RE |
| 编码 | 多版本、结构复杂 | 单一紧凑,uapi 固定 |

**教师视角**。打个比方:DWARF 是档案室里的全息病历,BTF 是贴身带的急救卡——病历全,但档案室不在现场;急救卡只写血型、过敏、紧急联系人,但**它跟人走**。两个常见误区顺手纠正:其一,「BTF 是 DWARF 的子集」——不是,它是按 BPF 需求**重新编码**的类型图,去重后全局类型 ID 编址(二节),结构上不是任何 DWARF 结构的投影;其二,「用户态能读 DWARF,内核也能」——位置不同:验证器在内核态,不能依赖用户态工具在场,BTF 的「自包含编码+常驻」正是为这个位置设计的。

比体积更根本的一条设计逻辑值得点破:**读者在哪,格式就得在哪**。BTF 的第一读者是 1.1 列的三批内核态消费者(验证器、attach 校验、登记簿),全是程序化消费者,格式必须能被内核里的几行 C 解开;调试器们至今读的仍是 DWARF——两份格式并不互相取代,它们服务两群住在不同位置的读者。

### 1.3 DWARF→BTF:转换发生在内核构建期(方法论级)

BTF 从哪来?答案藏在内核自己的构建开关里:`CONFIG_DEBUG_INFO_BTF`(lib/Kconfig.debug:375,v6.6.78),help 原文说得直白:「Generate deduplicated BTF type information from DWARF debug info...expects presence of pahole tool, which will convert DWARF type info into equivalent deduplicated BTF type info」(:382-384)。三个关键词:**从 DWARF**(转换,不是从头生成)、**去重**、**pahole**(DWARF→BTF 转换器,首现解释:dwarves 工具包成员)。

转换流程(方法论级:**本机未装 pahole,转换级验证需自备工具链**;流程与判读法如下):

1. **编译期**:每个 `.o` 由编译器带 `-g` 产出 DWARF;
2. **链接 vmlinux 时**:`scripts/link-vmlinux.sh` 的 `gen_btf()` 接手——先查 pahole 在场,连版本都有下限(低于 1.16 直接拒绝):

```sh
# scripts/link-vmlinux.sh:110-117(v6.6.78)
gen_btf()
{
	local pahole_ver

	if ! [ -x "$(command -v ${PAHOLE})" ]; then
		echo >&2 "BTF: ${1}: pahole (${PAHOLE}) is not available"
		return 1
	fi
```

   再一行调用完成提炼,产物 `.BTF` 段随后被塞回映像——1.1 的 `__start_BTF` 指向的就是它:

```sh
# scripts/link-vmlinux.sh:127-128(v6.6.78)
	info "BTF" ${2}
	LLVM_OBJCOPY="${OBJCOPY}" ${PAHOLE} -J ${PAHOLE_FLAGS} ${1}
```

3. **模块**:`CONFIG_DEBUG_INFO_BTF_MODULES`(lib/Kconfig.debug:406)打开后,`scripts/Makefile.modfinal`(:45)对每个模块跑 `pahole -J --btf_base vmlinux $@`——**split BTF**(分裂式 BTF,首现解释:模块只记相对 vmlinux 基底的增量类型),1.4 的模块文件因此才那么小。

转换有**两条来路**,别混。内核映像走 pahole——上面三步,DWARF→BTF 的提炼发生在构建机器上;BPF 目标文件走另一条:**clang 对 BPF 目标的 `-g` 直接产出 BTF,不经 pahole**。这条不是推断,本机可验:clang 18 编一个带结构体与函数的最小文件(`-g -O2 -target bpf`),`llvm-objdump -h` 里 DWARF 与 BTF **并存**——`.debug_info`/`.debug_line` 一族(DWARF,给人调试)与 `.BTF`/`.BTF.ext`(给机器消费)同场在场(本机实测:.BTF 0xf0 + .BTF.ext 0x50,clang 18 实测)。内核文档 llvm_reloc.rst 的示例同口径:「Compiled with `clang --target=bpf -O2 -g -c test.c`」之后列出的产物就带 .BTF/.BTF.ext(:206 一带)。一句话:**pahole 是内核映像的 BTF 源头,clang 是 BPF 目标的 BTF 源头**——三节要拆的重定位记录(.BTF.ext)正是从后者长出来的。

**判读法**(不需要 pahole,一枚运行中的内核就够):怎么确认「转换发生过」?三条证据,任一在案即说明这枚内核构建期完成了转换——①`/sys/kernel/btf/vmlinux` 在场(本机 7119843 字节);②`bpftool btf dump file /sys/kernel/btf/vmlinux` 有输出(1.5 实跑);③`CONFIG_DEBUG_INFO_BTF=y`(00-env ④ 段可查)。本机三条全中——发行版把转换做在了发行之前,这正是方法论级的妙处:**工具不在场,产物替它作证**。

教师视角补一句:`pahole -J` 是内核**构建期**工具,运行期不需要——「本机没装 pahole」与「本机没有 BTF」是两回事;真要给自己的内核开 BTF,装 dwarves 包即可(00-env ⑤ 段有提示),验收仍用上面三条证据。发行版 BTF 与构建指引见[附录 C](../appendix/c-distro-btf-build-guide.md)。

### 1.4 双层 BTF 实况:主说明书与模块增量

`/sys/kernel/btf/` 下是**双层**结构:一层主说明书(vmlinux),一层增量(每个已加载模块一个文件)。本机实况(其他内核数值不同):

- **vmlinux**:7119843 字节、mode 444 全局可读——CO-RE 的重定位跑在用户态 libbpf 里,说明书全局可读,无特权进程也读得到它;
- **模块**:每个已加载模块一个小文件,抽样 `aesni_intel` 仅 5408 字节——split BTF 只记增量,与 vmlinux 的 7119843 字节差三个数量级;
- **文件总数随已加载模块集浮动**:本章设计基线实测 110(1 个 vmlinux+109 个模块),成稿复查 109(1+108)——模块加载/卸载,窗口就变;00-env ③ 段每次实跑重新计数。这正是「本机实况」标注要防的事:把快照数当常数。

内核侧两层各有一个装载锚点。主说明书:`btf_parse_vmlinux`(btf.c:5720,1.1 已贴码)。模块层:`btf_parse_module`(kernel/bpf/btf.c:5789,v6.6.78),模块上线时经装载点调用(kernel/bpf/btf.c:7378)。模块 BTF 解析失败有一条值得记的分叉(btf.c:7382-7388):`CONFIG_MODULE_ALLOW_BTF_MISMATCH` 未设时模块**加载失败**(pr_warn "failed to validate module [%s] BTF");设了则放行加载、该模块无 BTF 可用(pr_warn_once mismatch)。本机未设该选项(00-env ④ 段),走严格档。

模块层同样可以 dump:`bpftool btf dump file /sys/kernel/btf/aesni_intel`(本机实跑 228 行),头一行是 `[170279] CONST '(anon)' type_id=105967`。盯住这个 ID:对 vmlinux 全表 `tail` 一眼,最大的一条是 `[170278] FUNC 'zswpout_show'`(本机实况,其他内核数值不同)——**模块的 ID 紧接在 vmlinux 之后编**,split BTF 的「接续」在 ID 编址上一眼可见:两层不是两本独立说明书,是一本接续编页的合订本(编址法二节展开)。

双层与本章主线的接口一句话:CO-RE 的「目标 BTF」就在目标机的这两层里——追内核类型的程序对 vmlinux 层;追模块内函数/类型的程序还要求模块层在场(五节的 `attach_btf_obj_fd` 回到这里)。

### 1.5 `bpftool btf dump`:入口三用法(学生动手)

说明书有了,怎么读?`bpftool btf dump` 是 BTF 世界的 cat,本章的三个用法各领一职:

- **用法一,读自编对象**:`bpftool btf dump file kinds.bpf.o`——[`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/) 的主战场,二节用它的输出逐一指认 kind;
- **用法二,读内核**:`bpftool btf dump file /sys/kernel/btf/vmlinux`——本机实跑 401978 行(本机实况,其他内核数值不同),头两行:

```text
[1] PTR '(anon)' type_id=3
[2] CONST '(anon)' type_id=1
```

  行格式先认三样:`[N]` 是**类型 ID**(从 1 起编址,void=0,编址法二节拆);`PTR`/`CONST` 是 **kind**(种类,下文沿用英文);`type_id=N` 指向被引用类型——BTF 的「图」就是这样一条条引用织出来的。顺着指针走两步试试:`[1]` PTR 指向 `type_id=3`,跳到 `[3]`;`[3]` 是 CONST,又指向 102965——两跳就落进了十万段位的大 ID,读 BTF 的基本功就此成形:看到 type_id 就跳过去,直到落到 INT/STRUCT 这类实体。全表里最该认得的一条,自己 grep 一把就有:`bpftool btf dump file /sys/kernel/btf/vmlinux | grep "STRUCT 'task_struct'"` 给出 `[156855] STRUCT 'task_struct' size=9920 vlen=274`(size=结构体字节数,vlen=成员数;这几个数怎么读,二节解码;本机实况,其他内核数值不同);
- **用法三,BTF 转 C**:`bpftool btf dump file /sys/kernel/btf/vmlinux format c`——把说明书翻译回 C 头文件,产物就是 vmlinux.h(本机 3500327 字节、166914 行、13897 个 struct 声明,本机实况)。第 7 章三个示例与第 9 章九个示例的 Makefile 里 `bpftool btf dump` 那条规则,机制本体就是这一行——[`02-vmlinux-h/`](../examples/chapter-08/02-vmlinux-h/) 把它拆成可复跑的脚本。

**学生动手**两步,均编译级、无 root:

```console
# ①环境自检:五段(树/工具链/BTF 家族/CONFIG/pahole)+ 三档结论
bash book/examples/chapter-08/00-env/check-env.sh
# ②亲手生成 vmlinux.h(产物不入库,可重复生成)
cd book/examples/chapter-08/02-vmlinux-h && ./gen-vmlinux-h.sh
```

预期看到:①四个走读文件在场、bpftool/clang/python3 探针逐个 OK(其中 bpftool 探针会打出 vmlinux 第 1 行,正是 1.5 认过的 `[1] PTR '(anon)' type_id=3`)、BTF 家族计数(随模块浮动,本机复查 109)、CONFIG 两 y 一未设、pahole 缺席声明与三档结论;②生成完成后的规模统计(字节/行数/struct 数,本机实况)。两步完整实录见各自 README。

**检查点**(自测后看提示):

1. BTF「常驻内核」在代码上最硬的证据是什么?——提示:1.1 贴码里 `btf->data` 指向哪段;说明书嵌在谁里面。
2. 三个「不用 DWARF」的理由,哪个是「能不能」、哪两个是「值不值」?「零外部依赖、可自我校验」的标准,1.2 里哪三步代码佐证?——提示:`btf_parse_vmlinux` 的校验三步。
3. 目标机没有 `/sys/kernel/btf/vmlinux`,装一个 pahole 能补出来吗?——提示:1.3 的转换发生在谁构建谁;判读法三条证据,第一条就能否掉。

**独立示例**:本节两件资产——环境自检 [`00-env/`](../examples/chapter-08/00-env/)(五段自检+三档结论)与 vmlinux.h 生成 [`02-vmlinux-h/`](../examples/chapter-08/02-vmlinux-h/)(`format c` 实录与规模统计)。

## 二、BTF kind 体系与类型 ID 编址

一节把 BTF 当说明书整体看,本节下到字节层:1.5 认过的每一行 dump 输出,在文件里都对应一段定长二进制记录。本节拆三件事——一条记录怎么编码(`struct btf_type` 三元组)、19 种 kind 各管什么、类型 ID 怎么编址;它们直接服务三/四节——CO-RE 重定位记录里填的就是 type_id,两台重定位引擎按 ID 查的就是这份编址。

### 2.1 `struct btf_type`:一条记录 12 字节

类型区的每条记录都是一个三元组,定义在 uapi 头文件里(include/uapi/linux/btf.h:31-53,v6.6.78):

```c
/* include/uapi/linux/btf.h:31-53(v6.6.78) */
struct btf_type {
	__u32 name_off;
	/* "info" bits arrangement
	 * bits  0-15: vlen (e.g. # of struct's members)
	 * bits 16-23: unused
	 * bits 24-28: kind (e.g. int, ptr, array...etc)
	 * bits 29-30: unused
	 * bit     31: kind_flag, currently used by
	 *             struct, union, enum, fwd and enum64
	 */
	__u32 info;
	/* "size" is used by INT, ENUM, STRUCT, UNION, DATASEC and ENUM64.
	 * "size" tells the size of the type it is describing.
	 *
	 * "type" is used by PTR, TYPEDEF, VOLATILE, CONST, RESTRICT,
	 * FUNC, FUNC_PROTO, VAR, DECL_TAG and TYPE_TAG.
	 * "type" is a type_id referring to another type.
	 */
	union {
		__u32 size;
		__u32 type;
	};
};
```

4+4+4,一条记录 12 字节。三个字段各管一摊:

- **`name_off`**:类型名字符串在字符串表(BTF 头里规划的另一段,与类型区平行)里的字节偏移。没有名字的记录(匿名结构体、CONST/PTR 这类包装层)`name_off` 指向空串,dump 里的 `'(anon)'`(匿名)即由此来,1.5 头两行全是;
- **`info`**:一个 32 位位域打包,拆法写在源码注释里:低 16 位是 **vlen**(成员/参数/枚举值的个数),第 24-28 位是 **kind**,最高位是 **kind_flag**(位域标记,2.3 用)。三个读取宏各一行(:55-57):`BTF_INFO_KIND`、`BTF_INFO_VLEN`、`BTF_INFO_KFLAG`;
- **`union { size; type; }`**:一鱼两吃。INT/ENUM/STRUCT/UNION/DATASEC/ENUM64 这六种「有实体尺寸」的 kind 用 **size**(字节数);PTR/TYPEDEF/VOLATILE/CONST/RESTRICT/FUNC/FUNC_PROTO/VAR/DECL_TAG/TYPE_TAG 这十种「引用别的类型」的 kind 用 **type**(指向被引用类型的 type_id)。同一记录按 kind 只会用其一,共用 4 字节不亏——这正是 1.2 那把「紧凑性」钥匙落到字段级的样子(例外是 ARRAY 与 FWD:前者的元素数在附加表 `btf_array` 里,后者连 struct/union 之分都借 kind_flag 一位来记,tools/lib/bpf/btf.c:2270)。

解码示范,兑现 1.5 欠的那笔账。`[156855] STRUCT 'task_struct' size=9920 vlen=274`(本机实况,其他内核数值不同)读成:name_off 指向字符串表里的 `task_struct`;info 拆出 kind=4(STRUCT)、vlen=274;union 按 kind 取 size=9920。一行人话底下,是一次三元组解码加一次 union 取舍。

三元组不是全部:某些 kind 在 12 字节之后还跟着**附加数据**(uapi 注释原话:struct btf_type is immediately followed by extra data,:85-87)。INT 跟一个 u32(编码:有符号/字符/布尔、位偏移、位宽,`BTF_INT_ENCODING`/`BTF_INT_OFFSET`/`BTF_INT_BITS` 三个宏 :92-94);STRUCT/UNION 跟 vlen 个成员表 `btf_member`(2.3);FUNC_PROTO 跟 vlen 个参数 `btf_param`(:145-148);ARRAY 跟一个三字段的 `btf_array`(元素类型/下标类型/元素个数,:111-115);DATASEC 跟 vlen 个 `btf_var_secinfo`(变量在段内的偏移与尺寸)。**定长头+变长附表**:解析器先读 12 字节定长头,再按 kind 与 vlen 决定附表多长。1.2 说的「`struct btf_type` 三元组加定长成员表」就是这套布局,而它在 2.4 还会送出更大的好处——定长,才能数组寻址。

### 2.2 19 种 kind:四组分工

kind 的全集是一段匿名枚举(uapi `btf.h:60-79`,1.1 已预告):`BTF_KIND_UNKN = 0` 占 0 号(未知,占位不用),真正的 kind 是 1–19 共 **19 种**;紧随其后的哨兵 `NR_BTF_KINDS`(:81)恰为 20。19 种按职责分四组:

| 组 | kind(编号) | 一句话职责 |
|---|---|---|
| 基础(8) | INT(1)/PTR(2)/ARRAY(3)/STRUCT(4)/UNION(5)/ENUM(6)/ENUM64(19)/FWD(7) | 类型图的实体节点:整数、指针、数组、复合体、枚举(值超 32 位升级 ENUM64)、只声明未定义的前向占位 |
| 修饰(3) | VOLATILE(9)/CONST(10)/RESTRICT(11) | 限定符,一行一个,`type` 指向被限定的类型 |
| 功能(3) | TYPEDEF(8)/FUNC(12)/FUNC_PROTO(13) | 类型别名、函数实体、函数签名 |
| 扩展(5) | VAR(14)/DATASEC(15)/FLOAT(16)/DECL_TAG(17)/TYPE_TAG(18) | 全局变量与其所在段、浮点、挂在声明/类型上的标签 |

四组有个共同的读法:**基础组是「节点」,其余三组多是「边」**——修饰、TYPEDEF、FUNC 都只是一行指向别人的记录,结构信息长在 STRUCT/UNION/FUNC_PROTO 这些节点上。修饰组最典型:`[2] CONST '(anon)' type_id=1`(1.5 读过)就是一行 CONST 指向 type_id=1,再套 volatile 就再垫一行;01 资产覆盖表第 10 项的参数链(FUNC_PROTO→PTR→CONST→TYPEDEF→STRUCT)把这种链式堆叠演全了——kind 体系就是靠 type 字段把一串行穿成图。

一个防混淆必须先钉死:本章有**两套 kind,互不相干**。本节这套是 BTF 类型 kind(uapi `btf.h:60-79`),回答「这条类型记录是什么」;三节还会遇到 CO-RE 重定位 kind(枚举 `bpf_core_relo_kind`,uapi `bpf.h:7262`),回答「这条重定位记录要补什么」。两套枚举分住两个 uapi 头文件,编号空间各自独立——三节展开后者,不再回来重讲。

枚举注释里 kind_flag 那个 bit 31,原话「currently used by struct, union, enum, fwd and enum64」:只有带成员表/取值表的五种 kind 用它,用途一个——告诉解析器附表里的 offset 按哪种读法解释。这正是 2.3 的入口。

### 2.3 成员表与位域:offset 按「位」记账

STRUCT/UNION 的成员表是最常用的一张附表(include/uapi/linux/btf.h:122-132,v6.6.78):

```c
/* include/uapi/linux/btf.h:122-132(v6.6.78) */
struct btf_member {
	__u32	name_off;
	__u32	type;
	/* If the type info kind_flag is set, the btf_member offset
	 * contains both member bitfield size and bit offset. The
	 * bitfield size is set for bitfield members. If the type
	 * info kind_flag is not set, the offset contains only bit
	 * offset.
	 */
	__u32	offset;
};
```

每个成员三个字段:名字、类型、偏移。偏移的单位是**位**,不是字节——这就是 dump 里 `bits_offset` 的底细。kind_flag 两种读法,配套两个宏(:138-139):

- **kind_flag 未置**(结构体无位域成员):offset 是纯位偏移,值=字节偏移×8;
- **kind_flag 已置**:offset 拆两段——高 8 位是位域宽度(`BTF_MEMBER_BITFIELD_SIZE`),低 24 位是位偏移(`BTF_MEMBER_BIT_OFFSET`)。

01 实物先看无位域的一面。`bpftool btf dump file kinds.bpf.o` 里 STRUCT 'demo_dev' size=32 vlen=3 的成员行(clang 18 实测):

```text
[5] STRUCT 'demo_dev' size=32 vlen=3
	'ifindex' type_id=6 bits_offset=0
	'name' type_id=8 bits_offset=32
	'caps' type_id=10 bits_offset=192
```

三个成员全按字节对齐,bits_offset 0/32/192 除以 8 正是字节偏移 0/4/24:name 在 ifindex 后第 4 字节,caps 在第 24 字节。对齐成员同样按位记账,只是值恰为 8 的倍数。

位域成员才见真章。一个小探针:`struct my_type { int a; int b:3; int c:5; unsigned long long d; }`,再配一个外链接全局变量把它锚进 `.BTF`(01 资产「防优化」的同款手法),clang `-g -O2 -target bpf` 编译后 `bpftool btf dump file`,clang 18 实测:

```text
[1] STRUCT 'my_type' size=16 vlen=4
	'a' type_id=2 bits_offset=0
	'b' type_id=2 bits_offset=32 bitfield_size=3
	'c' type_id=2 bits_offset=35 bitfield_size=5
	'd' type_id=3 bits_offset=64
```

`b` 在位偏移 32、宽 3 位,`c` 在位偏移 35——**紧贴 b 的结尾排布**,这就是按位记账的本意;此刻 kind_flag 已置,bpftool 把原始 offset 拆成 bitfield_size/bits_offset 两段展示。内核文档 btf.rst 给过同款示例(struct t 三个位域成员,:994 一带),vmlinux 里也遍地都是:`bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m2 bitfield_size`(本机实况,其他内核数值不同)。

位域为什么值得单讲一拍?它给 CO-RE 添了活:读位域不能只补字节偏移,还得知道补完后的左/右移位数——三节的重定位 kind 里专门备了一对,届时重定位记录会与这个 offset 字段逐位对账。

### 2.4 类型 ID 编址:数组下标,不是哈希

编址规则,btf.rst :68-70 原话:「The type id 0 is reserved for void type...starting from id 1」——**void 占 0 号、类型区顺序解析、ID 从 1 依次递增**。01 资产是小尺寸实证:dump 35 行,ID 恰为 [1]…[35] 无空洞;vmlinux 是大尺寸实证:401978 行 dump,最大 ID `[170278]`(本机实况,其他内核数值不同)。

ID 不只是行号,它还是**数组下标**:解析时类型区被拆成一条条记录按序装进数组,ID N 就是数组第 N 格。内核按 ID 取类型的入口是 `btf_type_by_id`(include/linux/btf.h:488 声明),实现在 kernel/bpf/btf.c:825(v6.6.78):

```c
/* kernel/bpf/btf.c:825-834(v6.6.78) */
const struct btf_type *btf_type_by_id(const struct btf *btf, u32 type_id)
{
	while (type_id < btf->start_id)
		btf = btf->base_btf;

	type_id -= btf->start_id;
	if (type_id >= btf->nr_types)
		return NULL;
	return btf->types[type_id];
}
```

`return btf->types[type_id]`——数组下标寻址,O(1),无哈希无查找。1.2 埋的那句「内核用数组下标寻址(二节拆编码)」谜底就在这一行:12 字节定长头换来的正是随机访问的能力——记录不定长就只能顺序遍历,定长才能按下标直达。

那个 while 值得停一下:`type_id < btf->start_id` 时换成 `btf->base_btf`,再减 `start_id` 才下标——这是 **split BTF** 的接续(1.3 的模块增量)。模块 BTF 解析的关键赋值(kernel/bpf/btf.c:5816-5818,v6.6.78):

```c
/* kernel/bpf/btf.c:5816-5818(v6.6.78) */
	btf->base_btf = base_btf;
	btf->start_id = base_btf->nr_types;
	btf->start_str_off = base_btf->hdr.str_len;
```

模块第一个 ID = 基底(vmlinux)的类型总数:1.4 实况里模块首行 `[170279]` 紧接 vmlinux 最大 `[170278]`,机制就是这行——**两层不是两本独立说明书,是一本接续编页的合订本**,ID 命名空间跨层连号。

「跨 Section 共享命名空间」是同一件事的小一号版:**一个 BTF blob 就是一套 ID 命名空间**,不按 ELF Section 分册。01 资产里 INT/PTR/STRUCT/VAR/DATASEC 混在 [1]…[35] 同一编号空间,`.rodata` 段变量与 FUNC 签名引用同一个 `int`;三个 DATASEC 排在编号末尾,引用的类型 ID 却散布全表。去重也挂在这套编址上:同一类型全 blob 只发一个 ID——kinds.bpf.o 里 `int` 只有一行 [6],被 struct 成员、VAR、ARRAY 处处引用。1.2 说的「去重后全局类型 ID 编址」到此闭环。

编址还有边界:ID 20 位,上限 `BTF_MAX_TYPE = 0xfffff`(uapi btf.h:25),单个 blob 至多约百万类型;vlen 16 位(`BTF_MAX_VLEN = 0xffff`,:29)。本机 170278 离上限尚远,但「顺序解析、依次递增」意味着:**ID 只对所属 blob 有意义**——换内核换份 BTF,同一类型的编号就不同。156855 是本机坐标,不是宇宙常数(本机实况,其他内核数值不同)。

### 2.5 教师视角:为什么 ID 而不是名字

课堂论证,三个理由由弱到强:

- **名字不可靠**。匿名结构体、CONST/PTR 这类包装记录一律 `'(anon)'`,无法按名索引;去重后的全表共享一个字符串表,同名 typedef 并不罕见。按 ID 寻址天然无歧义;
- **名字不高效**。按名找类型要进字符串表取串逐字节比较,按 ID 找是数组下标一次命中(2.4 的 `btf->types[type_id]`);验证器处理指令时反复查类型,O(1) 与 O(名字长度) 的差距在这个位置是设计级的;
- **记录要紧凑**。三节的重定位记录要在 `.BTF.ext` 里成千上万条,每条装一个 u32 足矣;改装名字就得配字符串偏移、加载期再做字符串比较——体积与解析成本双输。

边界同样要讲:**ID 是 blob 内坐标,不是跨内核常数**。学生最容易把「task_struct id=156855」当可移植事实抄走——它只是本机这份 BTF 的顺序编号。这个「不可移植」不是缺陷而是分工:ID 负责 blob 内 O(1) 寻址,跨内核的类型对应交给 CO-RE 按名字与结构重建(三节的重定位、四节的候选查找)——两套机制各管一段,合起来才是「编译一次、处处运行」。

**学生动手**,在 01 资产把本节内容逐一指认(均编译级,无 root):

```console
$ cd book/examples/chapter-08/01-btf-kinds && make
$ bpftool btf dump file kinds.bpf.o     # 35 行逐行指认 kind,对照 README 覆盖表
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] ENUM64"
[9733] ENUM64 '(anon)' encoding=UNSIGNED size=8 vlen=40   # 本机实况
```

第一条对照 2.2 的四组表,把自编对象刻意覆盖的 11 类逐一认领;第三条从 vmlinux 抽样自编对象没有的 kind——ENUM64,枚举值超 32 位时的升级形态(size=8、vlen=40:40 个 64 位取值)。FWD/FLOAT/TYPE_TAG 的抽样命令与「别 grep 到 kind 枚举自身」那个坑,README 有完整交代。

**检查点**(自测后看提示):

1. `struct btf_type` 敢用一个 union 让 size 与 type 共用同一 4 字节,依据是什么?——提示:2.2 的四组表里,用 size 的六种与用 type 的十种是否互斥;再想想 1.2 立的那把「紧凑性」钥匙。
2. 探针里 `'c'` 的 bits_offset=35、bitfield_size=5:这两个数的单位是什么?若把 bpftool 展示前的原始 offset 字段拿来,该怎么用 2.3 的两个宏拆开?——提示:位记账;高 8 位取宽度、低 24 位取偏移。
3. 类型 ID 换个内核就变,三节的 CO-RE 重定位记录里为什么还敢记 type_id?——提示:2.4 的「blob 内坐标」;记录里的 type_id 是起点不是终点,四节的候选查找接下一棒。

**独立示例**:本节实证面全在 [`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/)——多 kind 自编对象的逐行指认、类型 ID 编址观察、剩余 kind 的 vmlinux 抽样。

<!--
## 三、CO-RE 编译期:从宏到重定位记录
-->

<!--
## 四、两台重定位引擎
-->

<!--
## 五、实战五层:一条 BPF_CORE_READ 的完整旅程
-->

<!--
## 可视化:CO-RE 全链与两引擎对照
-->

<!--
## 常见问题与排错
-->

<!--
## 课后扩展任务
-->

<!--
## 总结
-->

<!--
## 本章参考来源
-->

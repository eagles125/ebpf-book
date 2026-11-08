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

- **vmlinux**:7119843 字节、mode 444 全局可读——普通骨架(常规 skeleton 加载路径)的 CO-RE 重定位跑在用户态 libbpf 里,说明书全局可读,无特权进程也读得到它;轻骨架的补丁由内核完成,见四节;
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

- **`name_off`**:类型名在字符串表(BTF 头里的另一段)中的字节偏移。没有名字的记录(匿名结构体、CONST/PTR 这类包装层)`name_off` 指向空串,dump 的 `'(anon)'`(匿名)即由此来,1.5 头两行即此;
- **`info`**:一个 32 位位域打包,拆法写在源码注释里:低 16 位是 **vlen**(成员/参数/枚举值的个数),第 24-28 位是 **kind**,最高位是 **kind_flag**(位域标记,2.3 用)。三个读取宏各一行(:55-57):`BTF_INFO_KIND`、`BTF_INFO_VLEN`、`BTF_INFO_KFLAG`;
- **`union { size; type; }`**:一鱼两吃。INT/ENUM/STRUCT/UNION/DATASEC/ENUM64 这六种「有实体尺寸」的 kind 用 **size**(字节数);PTR/TYPEDEF/VOLATILE/CONST/RESTRICT/FUNC/FUNC_PROTO/VAR/DECL_TAG/TYPE_TAG 这十种「引用别的类型」的 kind 用 **type**(指向被引用类型的 type_id)。同一记录按 kind 只会用其一,共用 4 字节不亏——1.2 的「紧凑性」钥匙落到了字段级(例外是 ARRAY 与 FWD:前者的元素数在附加表 `btf_array` 里,后者连 struct/union 之分也借 kind_flag 一位来记,见 2.2)。

解码示范,兑现 1.5 欠的那笔账。`[156855] STRUCT 'task_struct' size=9920 vlen=274`(本机实况,其他内核数值不同)读成:name_off 指向字符串表里的 `task_struct`;info 拆出 kind=4(STRUCT)、vlen=274;union 按 kind 取 size=9920。一行人话底下是一次解码加一次 union 取舍。

三元组不是全部:某些 kind 在 12 字节之后还跟着**附加数据**(uapi 注释原话:struct btf_type is immediately followed by extra data,:85-87)。INT 跟一个 u32(编码:有符号/字符/布尔、位偏移、位宽,`BTF_INT_ENCODING`/`BTF_INT_OFFSET`/`BTF_INT_BITS` 三个宏 :92-94);STRUCT/UNION 跟 vlen 个成员表 `btf_member`(2.3);FUNC_PROTO 跟 vlen 个参数 `btf_param`(:145-148);ARRAY 跟一个三字段的 `btf_array`(元素/下标类型与个数,:111-115);DATASEC 跟 vlen 个 `btf_var_secinfo`(变量在段内的偏移与尺寸)。**定长头+变长附表**:解析器先读 12 字节定长头,再按 kind 与 vlen 决定附表多长。1.2 说的「`struct btf_type` 三元组加定长成员表」就是这套布局,而它在 2.4 还会送出更大的好处——定长,才能数组寻址。

### 2.2 19 种 kind:四组分工

kind 的全集是一段匿名枚举(uapi `btf.h:60-79`,1.1 已预告):`BTF_KIND_UNKN = 0` 占 0 号(占位不用),真正的 kind 是 1–19 共 **19 种**;紧随其后的哨兵 `NR_BTF_KINDS`(:81)恰为 20。19 种按职责分四组:

| 组 | kind(编号) | 一句话职责 |
|---|---|---|
| 基础(8) | INT(1)/PTR(2)/ARRAY(3)/STRUCT(4)/UNION(5)/ENUM(6)/ENUM64(19)/FWD(7) | 类型图的实体节点:整数、指针、数组、复合体、枚举(值超 32 位升级 ENUM64)、只声明未定义的前向占位 |
| 修饰(3) | VOLATILE(9)/CONST(10)/RESTRICT(11) | 限定符,一行一个,`type` 指向被限定的类型 |
| 功能(3) | TYPEDEF(8)/FUNC(12)/FUNC_PROTO(13) | 类型别名、函数实体、函数签名 |
| 扩展(5) | VAR(14)/DATASEC(15)/FLOAT(16)/DECL_TAG(17)/TYPE_TAG(18) | 全局变量与其所在段、浮点、挂在声明/类型上的标签 |

四组有个共同的读法:**基础组是「节点」,其余三组多是「边」**——修饰、TYPEDEF、FUNC 都只是一行指向别人的记录,结构信息长在 STRUCT/UNION/FUNC_PROTO 上。修饰组最典型:`[2] CONST '(anon)' type_id=1` 再套 volatile 就再垫一行;01 资产覆盖表第 10 项的参数链(FUNC_PROTO→PTR→CONST→TYPEDEF→STRUCT)把链式堆叠演全——kind 体系靠 type 字段穿图。

一个防混淆先钉死:本章有**两套 kind,互不相干**。本节这套是 BTF 类型 kind(uapi `btf.h:60-79`),回答「这条类型记录是什么」;三节还会遇到 CO-RE 重定位 kind(枚举 `bpf_core_relo_kind`,uapi `bpf.h:7262`),回答「这条重定位记录要补什么」。两套枚举分住两个 uapi 头文件,编号空间各自独立——三节展开后者,不再回来重讲。

枚举注释里 kind_flag 那个 bit 31,原话「currently used by struct, union, enum, fwd and enum64」——五种 kind 用它,却是三种用途:STRUCT/UNION 定成员表 offset 的读法(2.3 的入口);FWD 记前向的是 struct 还是 union(tools/lib/bpf/btf.c:2270);ENUM/ENUM64 记取值有无符号——01 资产的 demo_state 显示 `encoding=UNSIGNED` 即标志未置(bpftool 判定在 tools/bpf/bpftool/btf.c:214),vmlinux 里另有 `encoding=SIGNED` 的 ENUM 行([1998],本机实况)。

### 2.3 成员表与位域:offset 按「位」记账

STRUCT/UNION 的成员表是最常用的附表(include/uapi/linux/btf.h:122-132,v6.6.78):

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

三个成员全按字节对齐,bits_offset 0/32/192 除以 8 正是字节偏移 0/4/24。对齐成员同样按位记账,只是值恰为 8 的倍数。

位域成员才见真章。一个小探针:`struct my_type { int a; int b:3; int c:5; unsigned long long d; }`,配一个外链接全局变量锚进 `.BTF`(01 资产「防优化」同款手法),clang `-g -O2 -target bpf` 编译后 `bpftool btf dump file`,clang 18 实测:

```text
[1] STRUCT 'my_type' size=16 vlen=4
	'a' type_id=2 bits_offset=0
	'b' type_id=2 bits_offset=32 bitfield_size=3
	'c' type_id=2 bits_offset=35 bitfield_size=5
	'd' type_id=3 bits_offset=64
```

`b` 在位偏移 32、宽 3 位,`c` 在位偏移 35——**紧贴 b 的结尾排布**,这就是按位记账的本意;此刻 kind_flag 已置,bpftool 把原始 offset 拆成 bitfield_size/bits_offset 两段展示。btf.rst 给过同款示例(struct t 三个位域成员,:994 一带),vmlinux 里也遍地都是:`bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m2 bitfield_size`(本机实况,其他内核数值不同)。

位域为什么值得单讲一拍?它给 CO-RE 添了活:读位域不能只补字节偏移,还得知道左/右移位数——三节的重定位 kind 里专门备了一对,届时与此 offset 字段逐位对账。

### 2.4 类型 ID 编址:数组下标,不是哈希

编址规则,btf.rst :68-70 原话:「The type id 0 is reserved for void type...starting from id 1」——**void 占 0 号、类型区顺序解析、ID 从 1 依次递增**。01 资产是小尺寸实证:35 行 dump,ID 恰为 [1]…[35] 无空洞;vmlinux 是大尺寸实证:401978 行,最大 ID `[170278]`(本机实况,其他内核数值不同)。

ID 不只是行号,它还是**数组下标**:类型区按序拆进数组,ID N 即数组第 N 格。内核按 ID 取类型的入口是 `btf_type_by_id`(include/linux/btf.h:488 声明),实现在 kernel/bpf/btf.c:825(v6.6.78):

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

模块第一个 ID = 基底(vmlinux)的类型总数:1.4 实况里 `[170279]` 紧接 `[170278]`,机制就是这行——**两层不是两本独立说明书,是一本接续编页的合订本**,ID 命名空间跨层连号。

「跨 Section 共享命名空间」是同一件事的小一号版:**一个 BTF blob 就是一套 ID 命名空间**,不按 ELF Section 分册。01 资产里 INT/PTR/STRUCT/VAR/DATASEC 混在 [1]…[35] 同一编号空间:`.bss` 的 demo_counter 经 [29]→[15] 落到 `int` [6],ARRAY [27] 直接挂 type_id=6;三个 DATASEC 排在编号末尾,引用的类型 ID 却散布全表。去重也挂在这套编址上:同一类型全 blob 只发一个 ID——`int` 只有一行 [6],被 struct 成员、VAR、ARRAY 处处引用。1.2 说的「去重后全局类型 ID 编址」到此闭环。

编址还有边界:ID 20 位,上限 `BTF_MAX_TYPE = 0xfffff`(uapi btf.h:25),单个 blob 至多约百万类型;vlen 16 位(`BTF_MAX_VLEN = 0xffff`,:29)。本机 170278 离上限尚远,但「顺序解析、依次递增」意味着:**ID 只对所属 blob 有意义**——换内核换份 BTF,同一类型的编号就不同。156855 是本机坐标,不是宇宙常数(本机实况,其他内核数值不同)。

### 2.5 教师视角:为什么 ID 而不是名字

课堂论证,三个理由由弱到强:

- **名字不可靠**。匿名结构体、CONST/PTR 这类包装记录一律 `'(anon)'`,无法按名索引;去重后的全表共享一个字符串表,同名 typedef 并不罕见。按 ID 寻址天然无歧义;
- **名字不高效**。按名找类型要进字符串表取串逐字节比较,按 ID 找是下标一次命中(2.4 的 `btf->types[type_id]`);验证器反复查类型,O(1) 与 O(名字长度) 的差距在这个位置是设计级的;
- **记录要紧凑**。三节的重定位记录要在 `.BTF.ext` 里成千上万条,每条装一个 u32 足矣;装名字就得配字符串偏移、加载期再比字符串——体积与解析成本双输。

边界同样要讲:**ID 是 blob 内坐标,不是跨内核常数**。学生最容易把「task_struct id=156855」当可移植事实抄走——它只是本机这份 BTF 的顺序编号。这个「不可移植」不是缺陷而是分工:ID 负责 blob 内 O(1) 寻址,跨内核的类型对应交给 CO-RE 按名字与结构重建(三节的重定位、四节的候选查找)——两套机制各管一段,合起来才是「编译一次、处处运行」。

**学生动手**,在 01 资产把本节内容逐一指认(均编译级,无 root):

```console
$ cd book/examples/chapter-08/01-btf-kinds && make
$ bpftool btf dump file kinds.bpf.o     # 35 行逐行指认 kind,对照 README 覆盖表
$ bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 -E "^\[[0-9]+\] ENUM64"
[9733] ENUM64 '(anon)' encoding=UNSIGNED size=8 vlen=40   # 本机实况
```

第一条对照 2.2 四组表,把自编对象刻意覆盖的 11 类逐一认领;第三条从 vmlinux 抽样自编对象没有的 kind——ENUM64,枚举值超 32 位时的升级形态(size=8、vlen=40)。FWD/FLOAT/TYPE_TAG 的抽样命令与「别 grep 到 kind 枚举自身」那个坑,README 有完整交代。

**检查点**(自测后看提示):

1. `struct btf_type` 敢用一个 union 让 size 与 type 共用同一 4 字节,依据是什么?——提示:2.2 的四组表里,用 size 的六种与用 type 的十种是否互斥;再想想 1.2 立的那把「紧凑性」钥匙。
2. 探针里 `'c'` 的 bits_offset=35、bitfield_size=5:这两个数的单位是什么?若把 bpftool 展示前的原始 offset 字段拿来,该怎么用 2.3 的两个宏拆开?——提示:位记账;高 8 位取宽度、低 24 位取偏移。
3. 类型 ID 换个内核就变,三节的 CO-RE 重定位记录里为什么还敢记 type_id?——提示:2.4 的「blob 内坐标」;记录里的 type_id 是起点不是终点,四节的候选查找接下一棒。

**独立示例**:本节实证面全在 [`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/)——多 kind 自编对象的逐行指认、类型 ID 编址观察、剩余 kind 的 vmlinux 抽样。

## 三、CO-RE 编译期:从宏到重定位记录

一、二节拆的是说明书本身(2.1-2.4)。本节进编译期,兑现导读预告①的前半句——「重定位记录怎么来」。一句话立骨,后文为它作证:**clang 按本机 BTF 的偏移编译每一处字段访问,同时把「我用了哪个类型的哪个字段」登记成一条重定位记录,存进对象的 `.BTF.ext` 段**——偏移写进指令,含义写进记录;编译在本机做,修正留给加载期(四节)。

### 3.1 问题陈述:老路是「每次启动重编」

CO-RE 之前,跨内核跑 BPF 的主流是 BCC(BPF Compiler Collection,BPF 编译器集合):源码以文本发给目标机,现场调 clang 连着内核头一起编。老路三个代价:**头文件依赖**(内核头描述的是「这台机器装的源码树」的布局,错位就编不过或读错)、**每次启动重编**(编译在运行期,每次加载都付时延,目标机还得装 clang 与内核头)、**产物不可复用**(换内核重来)。CO-RE 的目标是一次清掉三件事:编译一次、处处运行。办法不是写死偏移、也不是搬走编译,而是**把偏移依赖显式化**:编译期照本机 BTF 编,同时记下每处访问的机器可读含义;加载期拿记录对目标 BTF 重算、补进改写量。记录怎么来是本节;怎么补,四节。

### 3.2 工作流第一件:vmlinux.h——类型从哪来

「照本机 BTF 编」的前提,是本机 BTF 变成 C 编译器吃得下的形态。1.5 用法三已给答案:`bpftool btf dump file /sys/kernel/btf/vmlinux format c` 把整本说明书翻译回一个 C 头文件——vmlinux.h(本机 3500327 字节、166914 行、13897 个 struct 声明,本机实况,其他内核数值不同;结构导读见 [`02-vmlinux-h/`](../examples/chapter-08/02-vmlinux-h/))。第 7 章三个示例与第 9 章九个示例的 Makefile 里同款 `bpftool btf dump` 规则,机制本体就是这一行——两章当「构建链第一环」用,02 资产拆它的机制。

而这份头文件对 CO-RE 的真正意义在**文件头六行**:

```c
/* book/examples/chapter-08/02-vmlinux-h/vmlinux.h:1-6(本机生成实物) */
#ifndef __VMLINUX_H__
#define __VMLINUX_H__

#ifndef BPF_NO_PRESERVE_ACCESS_INDEX
#pragma clang attribute push (__attribute__((preserve_access_index)), apply_to = record)
#endif
```

`preserve_access_index` 修饰**所有** record(结构体/联合体)——include 之后,每处 `task->pid` 都被 clang 自动配上「字段访问」重定位,无须手写标注(机制 3.4 拆)。第 2 章排错表给「换内核后字段读到错位数据」开的处方——「坚持 vmlinux.h + CO-RE 工作流,加载时由 libbpf 依据目标内核 BTF 重定位字段偏移」——「工作流」三个字,从本节起逐件兑现。

### 3.3 工作流第二件:`-g`——没有它就没有 .BTF.ext(实证探针)

类型有了,编译还得带开关:`-g`。内核文档原话:「The llvm is able to generate .BTF and .BTF.ext directly with -g for bpf target only」(Documentation/bpf/btf.rst:999,v6.6.78)——对 BPF 目标,llvm 靠 `-g` **直出** `.BTF` 与 `.BTF.ext`,不经 pahole(1.3 的「两条来路」)。不加 `-g` 会怎样?一枚最小探针见分晓:

```c
/* probe.c:最小探针,一个结构体、一次字段访问 */
struct my_type { int a; int b; };

int probe(struct my_type *t)
{
	return t->a + t->b;
}
```

```console
$ clang -O2 -target bpf -c probe.c -o probe_nog.o  # 不带 -g
$ clang -g -O2 -target bpf -c probe.c -o probe_g.o  # 带 -g
$ llvm-objdump -h probe_nog.o | tail -5            # 段表全部内容:
  0               00000000 0000000000000000 
  1 .strtab       00000033 0000000000000000 
  2 .text         00000020 0000000000000000 TEXT
  3 .llvm_addrsig 00000000 0000000000000000 
  4 .symtab       00000048 0000000000000000 
$ llvm-objdump -h probe_g.o                        # 摘录:其余 .debug_* 段略
  4 .debug_info            0000005f 0000000000000000 DEBUG   # DWARF 也在
 11 .BTF                   000000c4 0000000000000000         # 类型图
 12 .BTF.ext               00000080 0000000000000000         # 记录的家
 13 .rel.BTF.ext           00000050 0000000000000000         # 作用于 .BTF.ext 自身的 ELF 重定位
```

(clang 18 实测;数值随版本浮动。)不带 `-g`:段表里除代码与符号表一无所有——**没有 `.BTF`,更没有 `.BTF.ext`,重定位记录无处安放**。带 `-g`:DWARF 一族与 BTF 家族并存(1.3 见过),三个段各司其职:`.BTF` 类型图(二节)、`.BTF.ext` 记录容器(3.5 拆内部)、`.rel.BTF.ext`(llvm 产、作用于 `.BTF.ext` **自身**的 ELF 重定位,本章不展开)。

注意层级差:**`-g` 给的是容器,CO-RE 访问才填记录**。探针没有 CO-RE 访问,`.BTF.ext`(128 字节)里装的是 func_info/line_info,core_relo 小节长 0——拿 [`04-relo-slicer/`](../examples/chapter-08/04-relo-slicer/) 切它,站 3 直接 FAIL:「core_relo 小节仅 0 字节……记录数=0」(exit 1,clang 18 实测)。03 的 core.bpf.o 同样带 `-g`,`.BTF.ext` 却是 380 字节——多出的正是 core_relo 小节(3.8 实物);「漏了 `-g`」因此不报错不告警,可移植性在源头就被抽走。

### 3.4 工作流第三件:内建与宏族——一行源码如何变成记录

登记这个动作,机器做的是内建(builtin,编译器内置的特殊函数,不经头文件声明),人写的是宏;两头在 `tools/lib/bpf/bpf_core_read.h` 会师(v6.6.78)——**宏族每个成员都是某个内建的用户界面**。先认三个内建:

- `__builtin_preserve_access_index(表达式)`:给整个字段访问表达式配重定位——`bpf_core_read` 的定义(:246-247)即一例:`bpf_probe_read_kernel(dst, sz, (const void *)__builtin_preserve_access_index(src))`;
- `__builtin_preserve_field_info(访问, 信息项)`:捕捉字段某一方面的信息(偏移/大小/存在性/符号性/位域移位),信息项由宏选定;头文件开头的 `__CORE_RELO`(:41)是它的薄封装;
- `__builtin_preserve_type_info` 与 `__builtin_preserve_enum_value`:类型级与枚举值级同族,3.6 的类型组/枚举值组各归其位。

vmlinux.h 文件头的 pragma 等价于给全部 record 套上 `__attribute__((preserve_access_index))`——llvm_reloc.rst 原话:「Field offset relocations are generated automatically when ``__attribute__((preserve_access_index))`` is used」(:446-447,v6.6.78)。于是字段访问有**两条来路**:直接访问走 pragma「批发」,每处自动产记录;宏族走「零售」,显式包裹并经 `bpf_probe_read_kernel`(helper,辅助函数)读取,适合不能直接解引用的程序类型。03 的 `bpf_get_current_task_btf()` 给的是可信指针,直接读也合法;示例统一走宏族,记录显性可见。

宏族逐个点名(行号均在 bpf_core_read.h,v6.6.78):

| 宏 | 干什么 | 对应 kind(3.6) |
|---|---|---|
| `bpf_core_read`(:246)/`BPF_CORE_READ_INTO`(:351)/`BPF_CORE_READ`(:443) | 读值;链式访问逐段分解(:427-436 注释给了四步:逐段换根,末段取值) | FIELD_BYTE_OFFSET(0) |
| `bpf_core_read_str`(:257)/`BPF_CORE_READ_STR_INTO`(:387) | 读字符串,NUL 截断拷贝 | FIELD_BYTE_OFFSET(0) |
| `bpf_core_field_exists`(:132) | 字段在目标内核是否存在(两种形式:`p->f` 与 `struct t, f`) | FIELD_EXISTS(2) |
| `bpf_core_type_exists`(:185)/`bpf_core_type_matches`(:195)/`bpf_core_type_size`(:205) | 类型存在/类型匹配/类型大小 | TYPE_EXISTS(8)/TYPE_MATCHES(12)/TYPE_SIZE(9) |
| `bpf_core_enum_value_exists`(:216)/`bpf_core_enum_value`(:227) | 枚举值存在/枚举值取值 | ENUMVAL_EXISTS(10)/ENUMVAL_VALUE(11) |
| `BPF_CORE_READ_BITFIELD_PROBED`(:70)/`BPF_CORE_READ_BITFIELD`(:88) | 读位域:前者经 probe 读(不支持直接内存读的程序类型),后者直接读;BYTE_OFFSET 定位、BYTE_SIZE 定宽、SIGNED 与移位量拼值 | 0/1/3/4/5 成组 |

位域一行回收 2.3 的伏笔——「读位域不能只补字节偏移,还得知道左/右移位数」:五个 kind 成对出动,`BPF_CORE_READ_BITFIELD` 的实现(:88-113)逐行演明:定位、定宽、左移、按符号右移。

埋线回收(第 2 章排错表第三组:「字段在编译期不存在→用 CO-RE 的字段存在性查询构造兼容分支」):`bpf_core_field_exists`(:132)就是那个查询,03 的兼容分支是本地实物——`kstack_offset` 在本机内核 BTF 里是 task_struct 成员(成员序 258、字节偏移 9544,本机实况),在 6.6.78 走读树里同名变量是 per-CPU 变量而非 task_struct 成员(03 资产考证);同一份 core.bpf.o 在两种内核上各自判、各走各的分支(运行行为预期档,03 README)。工作流至此三件齐:类型(vmlinux.h)、开关(`-g`)、表达(内建/宏族);第四件(记录怎么被消费)是四节的主题,接口钉住:**记录是登记不是修正,修正发生在加载期**。

### 3.5 记录的家:.BTF.ext 与四字段

记录长什么样?先看容器。btf.rst 4.2 节定义了 `.BTF.ext`(:726 起):「The .BTF.ext section encodes func_info, line_info and CO-RE relocations which needs loader manipulation before loading into the kernel」(:729-730,v6.6.78)。头部是 magic(0xEB9F,与 `.BTF` 同门牌)+版本+hdr_len,其后三个信息小节(func_info/line_info/core_relo);**core_relo 两字段是头的可选部分**——04 站 2 实物:core_relo_off=272、len=76;3.3 探针对象 len=0。头内偏移一律相对**头末尾**,读段易踩的坑。

记录本体,uapi 一锤定音(include/uapi/linux/bpf.h:7326-7331,v6.6.78):

```c
/* include/uapi/linux/bpf.h:7326-7331(v6.6.78) */
struct bpf_core_relo {
	__u32 insn_off;
	__u32 type_id;
	__u32 access_str_off;
	enum bpf_core_relo_kind kind;
};
```

16 字节四字段(uapi 注释 :7282-7296 逐条交代):

- **`insn_off`**:要补丁的指令在哪——ELF 层是段内**字节**偏移(72/8=9,正是 03 README 摘录里 `9: r1 = 0xaf0` 那条;内核 API 层按指令条数计,四节);
- **`type_id`**:访问链根实体的类型 ID,**对象自己 BTF 里的坐标**——03 对象里 task_struct 是 `[12]`,内核 BTF 里是 `[156855]`(本机实况);2.4 的「blob 内坐标」在此兑现:加载期按名字与结构重建对应,不照搬 ID;
- **`access_str_off`**:访问链字符串的偏移,**指向主 `.BTF` 的字符串区**——`.BTF.ext` 没有自己的字符串区(btf.rst:「Instead of type/string section, it contains func_info, line_info and core_relo sub-sections」,:754-756;uapi 同口径,:7287)。实证:libbpf 解析失败路径用 `btf__name_by_offset(local_btf, relo->access_str_off)` 取串(relo_core.c:1308,v6.6.78),`local_btf` 就是对象的主 `.BTF`;
- **`kind`**:这条记录要补什么,3.6 的枚举。

### 3.6 十三种 kind 与访问字符串

kind 全集是 uapi 的又一段枚举(include/uapi/linux/bpf.h:7262-7276,v6.6.78),编号 0–12 共 **13 种**。先把 2.2 的防混淆再敲一遍:这不是 BTF 类型 kind(uapi `btf.h:60-79`,19 种,答「这条类型记录是什么」),而是 CO-RE 重定位 kind(答「这条记录要补什么」)——两个枚举分住两个 uapi 头,编号空间独立,数量也不同(19 对 13)。13 种按记录对象分三组:

| 组 | kind(编号) | 一句话职责 |
|---|---|---|
| 字段类(6) | FIELD_BYTE_OFFSET(0)/FIELD_BYTE_SIZE(1)/FIELD_EXISTS(2)/FIELD_SIGNED(3)/FIELD_LSHIFT_U64(4)/FIELD_RSHIFT_U64(5) | 字段字节偏移/大小/存在/符号性/位域左移/位域右移——3.4 的读值、读串、兼容分支、位域全落这组 |
| 类型类(5) | TYPE_ID_LOCAL(6)/TYPE_ID_TARGET(7)/TYPE_EXISTS(8)/TYPE_SIZE(9)/TYPE_MATCHES(12) | 本机类型 ID/目标类型 ID/类型存在/类型大小/类型匹配;TYPE_MATCHES 是较新内核追加的匹配语义(:7275) |
| 枚举值类(2) | ENUMVAL_EXISTS(10)/ENUMVAL_VALUE(11) | 枚举值存在/枚举值整数值 |

04 切片器的名称表(站 4)按这份枚举打印,断言口径从严取 [0,11](出现 12 按越界 FAIL 并点名)——TYPE_MATCHES 知道在 13 号位即可。

字段类记录怎么表达「访问了哪个字段」?访问字符串(access string):冒号分隔的下标链(:7289-7292),首段是根下标(指针根解引用一次),其后每段是成员序或数组下标。uapi 三例(:7307-7310):

```text
&s->a      → "0:0"      (a 是字段 #0)
&s->b[5]   → "0:1:0:5"  (匿名结构体 #1,b 是其中 #0,取元素 5)
&s[10]->b  → "10:1"     (指针当数组用,根下标 10)
```

类型类记录的串恒为 `"0"`(:7293),枚举值类是值在枚举里的序号(:7294-7295)。解析语义在 libbpf 的 `bpf_core_parse_spec`(relo_core.c:263,v6.6.78):拆下标链、沿 BTF 逐层核对、累计偏移,产出访问规约(spec,访问链的机器可读形态);给人看的反方向是 `bpf_core_format_spec`(:1156),输出形如 `[<type-id>] (<type-name>) + <raw-spec> => <offset>@<spec>`。04 站 5 按同一语义解码:`"0:94"` → task_struct 成员 #94=pid。

一条记录的一生至此凑齐:编译期 clang 发射,加载期按目标 BTF 重算、补进改写量。补**哪个字段**按指令类决定(llvm_reloc.rst :260-264,v6.6.78):ALU/LD 类补 imm(立即数,指令内直接编码的常数),LDX/STX/ST 类补 offset,JMP 类不补。03 的四条记录全是 `r1 = <imm>` 形态——工作流第四件(加载期补丁),四节开讲。

### 3.7 教师视角:为什么记录在 .BTF.ext,而不是普通 ELF 重定位

**教师视角**。llvm_reloc.rst 先把答案框住:「These relocation records are not related to ELF relocations and are encoded in .BTF.ext section」(:250-252,v6.6.78)。课堂论证三层:

- **语义装不下**。普通 ELF 重定位补「把符号地址填进格子」;CO-RE 补的是语义值:字段偏移、存在性判定、类型大小、枚举取值,多数连着访问链一起算——四字段+下标链是自描述的小协议,塞进 ELF 的符号/加数模型两边都读不懂;
- **消费者不同**。CO-RE 记录的第一读者是加载器与验证器,要的是「按 kind 分派、沿链重算」的结构化数据;`.BTF.ext` 把同类记录按程序段聚拢,加载器一遍收集;
- **生命周期不同**。`.rel.BTF.ext` 恰好反衬主次:llvm 借它把「对象内部的未定值」填进记录;填好之后 CO-RE 的舞台就与 ELF 重定位无关——前者管对象内部自洽,后者管与目标内核对接。

再点一层:记录让编译期与运行期**彻底解耦**——编译器只保证「按本机 BTF 是对的」,对不对得起目标内核,加载期对账;3.1 的目标落到格式层,就是这个段。

### 3.8 实物判读:03 对象的四条记录(学生动手)

**学生动手**,两步,编译级、无 root:

```console
$ cd book/examples/chapter-08/03-core-read && make  # 四步链零告警(逐条见 03 README)
$ llvm-objdump -dr core.bpf.o | grep -F "CO-RE"  # 注解版:4 条记录
		0000000000000048:  CO-RE <byte_off> [12] struct task_struct::pid (0:94)
		0000000000000090:  CO-RE <byte_off> [12] struct task_struct::comm (0:129)
		00000000000000c8:  CO-RE <field_exists> [12] struct task_struct::kstack_offset (0:258)
		00000000000000e8:  CO-RE <byte_off> [12] struct task_struct::kstack_offset (0:258)
$ cd ../04-relo-slicer && python3 slice-relos.py ../03-core-read/core.bpf.o | tail -4
RELOS=4
kind 分布:BPF_CORE_FIELD_BYTE_OFFSET×3、BPF_CORE_FIELD_EXISTS×1
断言:记录数 > 0(实测 4)PASS;全部 kind ∈ [0,11] PASS
```

注解版给「哪条指令挂了哪条记录」;切片器给同一批记录的字节版(站 3 摘录,本机实况):

| # | insn_off | type_id | access_str_off | kind | 访问链 | 源码 |
|---|---|---|---|---|---|---|
| 0 | 72 | 12 | 11446 | 0 | 0:94 | core.bpf.c:81 读 pid |
| 1 | 144 | 12 | 11487 | 0 | 0:129 | :83 读 comm |
| 2 | 200 | 12 | 11540 | 2 | 0:258 | :100 问存在 |
| 3 | 232 | 12 | 11540 | 0 | 0:258 | :102 读 kstack_offset |

读三处细节:①四条 type_id 全是 12——同一根类型,三条链分指成员;#2 与 #3 链相同,一条问存在、一条取偏移;②立即数:0/1/3 号是本机字节偏移(0xaf0=2800、0xd10=3344、0x2548=9544),2 号是占位的 1——存在性补的是「有则 1、无则 0」,补什么值编译期无从知道,这正是 kind 2 存在的意义;③13 种 kind 本对象只用了 2 种:**没读位域就没有 4/5**。切片器其余三站补注解版没有的:站 1 段布局(`.BTF.ext` 380 字节,对照 3.3 探针的 128 字节,多出的是 core_relo 小节)、站 2 段头(core_relo_off=272/len=76)、站 5 逐条解码(位偏移 22400=字节 2800,与立即数对账)。

**检查点**(自测后看提示):

1. 只删 `-g`、源码一字不动,会发生什么?在哪一步最先暴露?——提示:3.3 探针;无 `.BTF.ext` 即无记录可补,证据编译期就有(`llvm-objdump -h`)。
2. 记录里 `access_str_off=11540` 指向的字符串住在哪?`.BTF.ext` 自己有字符串区吗?——提示:3.5;主 `.BTF` 的字符串区;没有(btf.rst :754;relo_core.c:1308 取串用 local_btf)。
3. 记录里 type_id=12,内核 BTF 里 task_struct 是 156855——加载期凭什么对上?——提示:2.4 的「blob 内坐标」;12 只是本地根类型坐标,按名字与结构重建对应是四节的事。

**独立示例**:本节两件资产——运行型示例 [`03-core-read/`](../examples/chapter-08/03-core-read/)(`BPF_CORE_READ` 闭环与兼容分支,编译级全链)与切片器 [`04-relo-slicer/`](../examples/chapter-08/04-relo-slicer/)(`.BTF.ext` 五站切片,`RELOS=4`)。

## 四、两台重定位引擎

三节结尾钉了一句:**记录是登记不是修正,修正发生在加载期**。本节兑现它:导读预告①的后半句(第 2 章 1.6 「跨版本访问由 CO-RE 重定位在加载时修正」的「加载时」)与第 7 章总结的「重定位何时完成」,答案是**加载期**。同一个加载动作里两台引擎轮值:普通骨架由 libbpf 在用户态补;轻骨架(light skeleton,把加载流程本身编成 BPF 程序的生成式骨架方案)由内核在验证期补。

两台引擎吃的记录同一种(三节的四字段 `struct bpf_core_relo`),通道同一条(uapi),两侧解算逻辑同源、各自维护,接口对齐 uapi——差异只在壳层:谁来收集、缓存、接线。

### 4.1 用户态引擎:collect 与 relocate

libbpf 引擎两站分居两阶段:collect 在**对象打开期**:`bpf_object_open`(libbpf.c:7380,v6.6.78)于 :7457 调 `bpf_object__collect_relos`(:6736),分发点 :6763;relocate 嵌在 `bpf_object__load` 加载链(:7923 起,调用点 :7945):

- **collect**:`bpf_object__collect_prog_relos`(libbpf.c:4236,v6.6.78)收集程序节的 ELF 重定位;三节拆的 `.BTF.ext` 记录同在打开期先行备好:`btf_ext__new`(:2849)解析出 `core_relo_info`(:2861),relocate 时按节取用;
- **relocate**:`bpf_object__relocate_core`(libbpf.c:5783,v6.6.78),由 `bpf_object__relocate`(:6519)调用(:6526)。进门先看 `core_relo_info.len`,为 0 直接返回(:5796);有则建候选缓存(:5808),逐节逐条处理。

分叉在循环体内(libbpf.c:5861-5878,v6.6.78):先 `record_relo_core`(:5861)把记录挂上程序,随后——

```c
/* tools/lib/bpf/libbpf.c:5868-5869(v6.6.78) */
			if (prog->obj->gen_loader)
				continue;
```

`gen_loader` 不在场(普通骨架)→ 当场 resolve(:5871 `bpf_core_resolve_relo`)、当场 patch(:5878 `bpf_core_patch_insn`),`bpf_object__load` 返回时指令已不是编译期那批字节;在场(轻骨架)→ 登记完 `continue`(:5868),补丁权让给内核(4.4)——「谁补」分流的全部代码证据。

### 4.2 补丁四步与失败语义

`bpf_core_resolve_relo`(libbpf.c:5739)总装四步,解算核心在 relo_core.c(v6.6.78,下列行号均该文件):

1. **候选查找** `bpf_core_find_cands`(libbpf.c:5604):目标 BTF 先 vmlinux 后模块(:5631/:5649);按「本质名」匹配——剥掉 `___flavor` 式后缀再比(:5623);
2. **兼容检查** `__bpf_core_types_are_compat`(relo_core.c:146):递归逐成员比对——STRUCT/UNION 看成员布局,PTR 沿指向下钻,FUNC_PROTO 比参数表,同名不等于同构;按名匹配的宽口径 `__bpf_core_types_match`(:1544);
3. **计算** `bpf_core_calc_relo_insn`(relo_core.c:1280):`bpf_core_parse_spec`(:263,三节 3.6)拆访问字符串成本地规约,逐候选套算;多候选必须同答案,偏移或决策有歧义即失败(:1365/:1375);TYPE_ID_LOCAL 例外,直接取本地类型 ID(:1319-1327);
4. **补丁** `bpf_core_patch_insn`(relo_core.c:1024):按指令类改写 imm 或 offset(3.6 的 llvm_reloc.rst 口径);`poison` 在案则把指令改写成非法 helper 调用(:1033-1042)。

失败语义(预期口径,全部在加载期暴露):

- **目标 BTF 找不到 → -ESRCH**:`btf__load_vmlinux_btf` 遍历完候选路径一无所获,pr_warn "failed to find valid kernel BTF" 后返回 -ESRCH(tools/lib/bpf/btf.c:4803-4804,v6.6.78);带 CO-RE 记录的对象必触发装载——`obj_needs_vmlinux_btf` 见 core_relo 记录即真(libbpf.c:3045,判定 :3053);
- **记录对不上本地 BTF → -EINVAL**:type_id 越界、根类型无名字、访问串解析失败(libbpf.c:5617-5622;relo_core.c:1301-1313);
- **候选列表为空 → 不算错,按 kind 分流**:查找成功但目标 BTF 无同名类型,relo_core.c:1395-1412 的注释把这定性为「可能是预期」:EXISTS 类补 0,其余把指令 poison(投毒,libbpf 术语:改写成调用非法 helper 的指令,让验证器在日志里点名)——死代码被验证器消灭,活代码加载失败并指到指令号。

### 4.3 内核态引擎:attr 载体、verifier 收件窗口与 bpf_core_apply

记录怎么进内核?uapi 载体,`BPF_PROG_LOAD` attr 尾部的三字段(include/uapi/linux/bpf.h:1463-1466,v6.6.78):

```c
/* include/uapi/linux/bpf.h:1463-1466(v6.6.78) */
		__u32		core_relo_cnt;	/* number of bpf_core_relo */
		__aligned_u64	fd_array;	/* array of FDs */
		__aligned_u64	core_relos;
		__u32		core_relo_rec_size; /* sizeof(struct bpf_core_relo) */
```

与 func_info/line_info 同款「计数+指针+单条尺寸」三件套。为什么是**条数+每条尺寸**、不写死数组类型?uapi 结构体会演进,`core_relo_rec_size` 是**协商字段**:用户态自报每条多大,内核按自己认识的尺寸取交集——`expected_size = sizeof(struct bpf_core_relo)`,`ncopy = min(expected_size, rec_size)`(verifier.c:15746-15747);多出的尾部扩充字段必须全零(`bpf_check_uarg_tail_zero` :15754,注释原话 "future proofing when sizeof(bpf_core_relo) changes" :15753),非零则 -E2BIG、内核把认识的尺寸写回 attr;上限 252 字节(:15716,与 func_info 同帽)。

收件窗口 `check_core_relo`(kernel/bpf/verifier.c:15718,v6.6.78):从 `prog->aux->btf` 组 `bpf_core_ctx`(verifier 日志+程序 BTF),计数为 0 直接放行(:15733-15735);注释(:15750-15752)点破差别:**取一条、补一条**(原话 "copy and apply each CO-RE relocation record one at a time")——每条补丁直接改写正在验证的指令,出错必须立刻停。循环体内三道门:尾部校验(:15754)、insn_off 须为 8 的倍数且不越程序长度(:15771,否则 -EINVAL),然后递给引擎(:15778);总接线在 `check_btf_info`(:15817)。

引擎本体 `bpf_core_apply`(kernel/bpf/btf.c:8423,v6.6.78),一条记录三步走完:

- **先验**:除 TYPE_ID_LOCAL 外都要候选(:8426);按 3 份 spec 开约 4k 工作区(:8433-8437);type_id 查不到即 -EINVAL(:8440-8445,"bad type id");
- **候选**:持 `cand_cache_mutex` 调 `bpf_core_find_cands`(:8453;函数头 :8336)——先 `bpf_get_btf_vmlinux()`(:8347),按本质名在 vmlinux 找同名(:8360),落空再遍历模块 BTF(:8391/:8400);锁从查缓存持到 BTF 指针抄进候选列表(:8475,防模块 BTF 半路卸载);
- **算与补**:`bpf_core_calc_relo_insn`(:8482)、`bpf_core_patch_insn`(:8487)——注意传的是 `relo->insn_off / 8`:三节 3.5 埋的「内核 API 层按指令条数计」在此对账——attr 里的 insn_off 仍是字节坐标(uapi 注释原话 "instruction offset (in bytes)",bpf.h:7283),但必须落在指令边界,内核进门即折成指令条数,此后全程按条记账;轻骨架反向,登记时把下标乘 8 折回字节(4.4)。

兼容判定内核侧是 `bpf_core_types_are_compat`(:8094),递归深度上限 2(:8073);libbpf 侧同名包装传 32(libbpf.c:5686)。同一套比对规则与 kind 语义,两侧各自实现、各自调参,靠 uapi 记录结构对齐行为——「逻辑同源、各自维护,接口对齐 uapi」的注脚。

### 4.4 谁在用内核态:轻骨架与 gen_loader

「内核也要一台」不是理论需求,轻骨架是唯一用户。libbpf 的 gen_loader 能把**加载流程本身**编成一枚 BPF 程序——加载器程序(loader program):建映射、载 BTF、提交程序,全由它在内核里经 `bpf_sys_bpf`(helper,辅助函数)发起(gen_loader.c:272 `BPF_FUNC_sys_bpf`;BPF_PROG_LOAD 发射点 :1018)。开关是 `bpf_object__gen_loader`(libbpf.c:8508)。加载器程序一跑,用户态 libbpf 早已不在环上——补丁只能内核做。

CO-RE 记录在轻骨架路径的四站:

1. **登记**:4.1 的 `record_relo_core`(libbpf.c:5705)先把记录挂上程序;加载前 `bpf_program_record_relos`(libbpf.c:7263)逐条转交 `bpf_gen__record_relo_core`(:7296),insn_off 在此从指令下标乘 8 折回字节(:7290 `.insn_off = relo->insn_idx * 8`);
2. **攒货**:gen 侧数组追加(gen_loader.c:851);
3. **提交**:`bpf_object_load_prog` 提交点分叉(libbpf.c:6953-6959)——gen 在场不真发系统调用,改调 `bpf_gen__prog_load`(gen_loader.c:934):attr 三连在 :969-972(rec_size= `sizeof(struct bpf_core_relo)`、cnt=条数、数组装入数据 blob);attr 存 blob 内偏移而非内核指针,`emit_rel_store`(:990)编成运行时存储指令;
4. **内核补丁**:加载器程序运行,`bpf_sys_bpf(BPF_PROG_LOAD, attr)` 一发,记录从 4.3 收件窗口进来,由内核引擎补**目标程序**的指令。

一句口径收束:**普通骨架 libbpf 补丁,轻骨架内核补丁**。记录同一种,通道同一条,补丁对象不同——前者改 libbpf 将要提交的指令数组,后者改加载器程序 blob 里的目标程序指令。

### 4.5 教师视角:为什么内核也要一台

**教师视角**。学生最常问:libbpf 明明能补,内核何必再造一台?论证两层:

- **场景**:轻骨架的卖点是加载路径极简——骨架产物缩到几十 KB、不带 libbpf 全套依赖;补丁的活总要有人干,只能在内核接;
- **时序**:加载器程序在内核里运行时,目标程序的字节已在内核手里;`bpf_sys_bpf` 是同步调用,attr 进门就是 verifier 的地界,「先回用户态补一趟」在时序上不存在。引擎长在内核,是时序决定的,不是偏好。

纠正一个误读:「两台引擎」不是两套语义——kind 表一张(uapi bpf.h:7262)、记录结构一个(:7326)、失败口径一套;差异只在壳层与调参(4.3 的深度 32 对 2)。**协议统一,实现分治**。

### 4.6 学生动手:05 五站实录

**学生动手**,走读内核引擎五站(只读树,无 root;每站附 ≤25 行代码段与职责注,完整实录见 [`05-kernel-relo/`](../examples/chapter-08/05-kernel-relo/) README):

```console
$ bash book/examples/chapter-08/05-kernel-relo/trace-kernel-relo.sh
# 走读树:WALK_TREE=/work/linux/linux-6.6.78(头部只读声明行略)

## 站 1:内核引擎入口 bpf_core_apply
### 定位(grep -n 实测;v6.6.78 校准)
8423:int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,
8453:		cc = bpf_core_find_cands(ctx, relo->type_id);

## 站 2:候选查找 bpf_core_find_cands 与兼容判定 bpf_core_types_are_compat
### 定位(grep -n 实测;v6.6.78 校准)
8336:bpf_core_find_cands(struct bpf_core_ctx *ctx, u32 local_type_id)
8094:int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,

## 站 3:verifier 接线 check_core_relo
### 定位(grep -n 实测;v6.6.78 校准)
15718:static int check_core_relo(struct bpf_verifier_env *env,
15739:	rec_size = attr->core_relo_rec_size;
15778:		err = bpf_core_apply(&ctx, &core_relo, i,
15817:	err = check_core_relo(env, attr, uattr);

## 站 4:UAPI 载体 union bpf_attr 的 core_relo 三字段
### 定位(grep -n 实测;v6.6.78 校准)
1463:		__u32		core_relo_cnt;	/* number of bpf_core_relo */
1465:		__aligned_u64	core_relos;
1466:		__u32		core_relo_rec_size; /* sizeof(struct bpf_core_relo) */

## 站 5:轻骨架提交 bpf_gen__prog_load
### 定位(grep -n 实测;v6.6.78 校准)
851:void bpf_gen__record_relo_core(struct bpf_gen *gen,
970:	attr.core_relo_cnt = gen->core_relo_cnt;
990:	emit_rel_store(gen, attr_field(prog_load_attr, core_relos), core_relos);

== 汇总 ==
TREE_VERSION=v6.6.78 STATIONS=5
```

五站对号:站 1=4.3 引擎本体(「查候选→算结果→改指令」);站 2=候选与兼容;站 3=收件窗口(「取一条、补一条」);站 4=uapi 载体;站 5=轻骨架提交。正文引用的行号与脚本 grep 出的同源——**正文与实物同一坐标**。

**检查点**(自测后看提示):

1. 普通骨架与轻骨架,CO-RE 补丁各发生在哪一步?分流的代码证据是哪一行?——提示:4.1 的 `continue`(:5868)。
2. `core_relo_rec_size` 为什么是协商字段?新用户态带扩充字段对旧内核,协商以什么收场?——提示:4.3;`bpf_check_uarg_tail_zero`;-E2BIG 加写回内核认识的尺寸。
3. 加载器程序运行后,libbpf 为什么再无机会补丁?引擎长在内核,是场景决定还是时序决定?——提示:4.4/4.5;`bpf_sys_bpf` 是同步调用,attr 进门即 verifier。

**独立示例**:本节实物是内核引擎切片器 [`05-kernel-relo/`](../examples/chapter-08/05-kernel-relo/)——五站走读内核态重定位引擎(入口/候选与兼容/verifier 接线/uapi 载体/轻骨架提交),只读树、无 root 可复跑。

## 五、实战五层:一条 BPF_CORE_READ 的完整旅程

一至四节把机制拆到了字节层,本节把楼板铺回地面:五层各认领一份资产、一条命令,连成一次全链走读——①环境(00-env 定三档)→②生成(02 产 vmlinux.h)→③剖析(01 指认自编对象 BTF)→④编译与记录(03 四步链+04 切片)→⑤加载补丁(03 的 loader 五步,预期口径)。前四节的机制在层⑤合龙;走完主线再补两块实战拼图:目标机没有 BTF 怎么办(5.6 回退策略),模块自己的说明书怎么用(5.7)。

### 5.1 层① 环境:一次自检,三档定音

动手前让 [`00-env/check-env.sh`](../examples/chapter-08/00-env/) 替你问五个问题:树在不在、工具链通不通、BTF 家族齐不齐、CONFIG 开没开、pahole 在不在。本机实跑摘录(①②⑤三段与部分降级提示行略,重跑脚本即得全貌):

```console
$ bash book/examples/chapter-08/00-env/check-env.sh        # 摘录:③④两段+末尾结论
OK    /sys/kernel/btf 共 109 个 BTF 对象(1 个 vmlinux + 其余为模块 BTF,数目随本机加载的模块而变)
OK    /sys/kernel/btf/vmlinux 可读(7132199 字节,mode 444 全局可读)——CONFIG_DEBUG_INFO_BTF=y 的落地产物
OK    配置源:/boot/config-7.0.0-42-generic
OK    CONFIG_DEBUG_INFO_BTF=y——vmlinux 内嵌 BTF(③的 /sys/kernel/btf/vmlinux 由此而来,CO-RE 的「目标 BTF」前提)
OK    CONFIG_DEBUG_INFO_BTF_MODULES=y——模块各自携带 BTF(③的模块 BTF 抽样由此而来)
OK    CONFIG_MODULE_ALLOW_BTF_MISMATCH 未设——模块 BTF 与 vmlinux 严格对齐(缺符号才降级,默认严格档)
== 结论(本章三档验证口径,正文与章级 README 逐档引用) ==
[1] 编译级可跑            PASS  走读树 v6.6.78(四走读文件齐备)+ bpftool btf dump 探针 + clang -g -target bpf 探针 + vmlinux BTF 在场——01~05 资产编译级可复现(无 root)
[2] 加载级需 root         预期  非特权用户且 kernel.unprivileged_bpf_disabled=2,非特权加载彻底禁用——03 的加载/attach/读取均为预期输出示例(运行级待验证,需 root)
[3] 方法论级 pahole       方法论  本机未装 pahole,转换级验证需自备工具链——一节按方法论档给流程与判读法
```

(本机实况,其他内核数值不同;1.4 成稿快照 7119843 字节,本次实跑 7132199 字节——数字随开机状态浮动,机制不变。)这层产出的不是代码,是**验证档位**:后面每层输出按哪个档读,自检说了算。

### 5.2 层② 生成:整本说明书压进一个头文件

层①确认说明书在场,层②把它变成 clang 吃得下的形态——机制 1.5 用法三与 3.2 已拆,这里只答「为什么能生成数万行声明」:vmlinux BTF 的类型区是一张从 1 顺序编址的大表(本次实跑最大 ID `[170550]`,本机实况,其他内核数值不同),`format c` 逐 ID 还原——STRUCT 展开成员逐行、TYPEDEF 还原一行别名、ENUM 展开取值表,task_struct(现 `[157112]`)一条不落。规模统计(02 资产实跑):

```console
$ cd book/examples/chapter-08/02-vmlinux-h && ./gen-vmlinux-h.sh   # 摘录:规模统计段
== 规模统计(本机实况,其他内核数值不同) ==
  字节: 3505236(wc -c)
  行数: 167155(wc -l)
  struct 声明数: 13911(grep -c '^struct')
```

数万行不是虚胖,是「一个类型都不许缺」的必然:3.2 的文件头 pragma 给全部 record 配重定位,缺一个类型,引用它的程序就编不过;typedef 兼容区(02 README ③)把引用链闭合,全文件零 `#include` 单头自洽。3.2 成稿快照 3500327 字节,本次 3505236——与 5.1 同一次重启的漂移,再次印证 2.4「数字只对所属 blob 有意义」。

### 5.3 层③ 剖析:自编对象先当一次小说明书

读 40 万行的大书之前,先用 [`01-btf-kinds/`](../examples/chapter-08/01-btf-kinds/) 编一本 35 行的小书练手(二节 2.5 已逐行指认,此处回放最该认得的一行):

```console
$ cd book/examples/chapter-08/01-btf-kinds && make
$ bpftool btf dump file kinds.bpf.o | grep -A3 -F "STRUCT 'demo_dev'"
[5] STRUCT 'demo_dev' size=32 vlen=3
	'ifindex' type_id=6 bits_offset=0
	'name' type_id=8 bits_offset=32
	'caps' type_id=10 bits_offset=192
```

(demo_dev 三成员 bits_offset 0/32/192,除以 8 正是字节偏移 0/4/24——2.3 的按位记账。)层③的意义在「控制变量」:小书里每个 kind 都是源码亲手写的,指认有疑可以对着源码查;有了这个手感,再去读 vmlinux 里 274 个成员的 task_struct(9920 字节,本机实况)才不迷路。

### 5.4 层④ 编译与记录:四步链与四条记录

层②的头文件加 `-g`(3.3 的命门),03 四步链一次走完(本机实跑,零告警):

```console
$ cd book/examples/chapter-08/03-core-read && make
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -I/usr/lib/llvm-18/lib/clang/18/include -c core.bpf.c -o core.bpf.o
bpftool gen skeleton core.bpf.o > core_bpf.skel.h
cc -g -Wall loader.c -o core_read -lbpf -lelf -lz
$ echo $?
0
```

四步对号机制:vmlinux.h=层②产物入链;clang 按**本机**偏移编译同时发记录(3.4);skeleton 把对象打包;gcc 链出加载器。产物里的记录,注解版与切片版各看一眼(3.8 已逐条判读,此处验收全链):

```console
$ llvm-objdump -dr core.bpf.o | grep -F "CO-RE"
	0000000000000048:  CO-RE <byte_off> [12] struct task_struct::pid (0:94)
	0000000000000090:  CO-RE <byte_off> [12] struct task_struct::comm (0:129)
	00000000000000c8:  CO-RE <field_exists> [12] struct task_struct::kstack_offset (0:258)
	00000000000000e8:  CO-RE <byte_off> [12] struct task_struct::kstack_offset (0:258)
$ cd ../04-relo-slicer && python3 slice-relos.py ../03-core-read/core.bpf.o | tail -3
RELOS=4
kind 分布:BPF_CORE_FIELD_BYTE_OFFSET×3、BPF_CORE_FIELD_EXISTS×1
断言:记录数 > 0(实测 4)PASS;全部 kind ∈ [0,11] PASS
```

三条 `<byte_off>` 的立即数 0xaf0/0xd10/0x2548(2800/3344/9544)就是本机偏移;`<field_exists>` 占位 imm=1,补什么值编译期无从知道(3.8 之②)。**这批字节就是五层旅程的「行李」**——下一层换台机器,被改写的正是它们。

### 5.5 层⑤ 加载补丁:open→load→attach(预期口径)

[`loader.c`](../examples/chapter-08/03-core-read/) 五步走完生命周期(:57/:65/:75/:93/:100):**open** 解析骨架内嵌对象——4.1 的 collect 在这一步(对象打开期);**load** 是重头戏:4.1 的 relocate 先跑(`bpf_object__relocate_core` 按**目标机** BTF 逐条重算 5.4 的四条记录,`bpf_core_patch_insn` 改写 imm/offset),补完才轮到 verifier 校验与系统调用提交;**attach** 按 SEC 名挂 tracepoint;**poll** 收事件;**destroy** 释放。本机无 root 且 `unprivileged_bpf_disabled=2`,真实加载属运行级待验证,输出按预期档引用(03 README):

```console
$ sudo ./core_read                      # 预期输出示例 / 运行级待验证(需 root,非本机实跑)
已挂上 tracepoint/sched/sched_process_exec
请在 5 秒内在另一个终端执行任意命令(如 ls、/bin/true)……
[core-read] pid=2314   comm=ls              kstack_offset_exists=1 kstack_offset=1849119156
poll 一轮结束:收到 1 条事件,销毁对象退出
```

字段修正演示就在末字段:`kstack_offset_exists=1` 是 `bpf_core_field_exists` 在**目标内核**判「有」;同一份 .bpf.o 拿到 6.6 内核,预期输出 `kstack_offset_exists=0 kstack_offset=0`(哨兵值)——6.6.78 走读树里 kstack_offset 是 per-CPU 变量、不是 task_struct 成员(3.4 的双树考证),同一份对象两种内核各走各的分支。诚实补一句:本机演示中三条偏移「改写前后」数值相同(编译机与目标机同机),修正量只在跨内核时显形——届时 imm 被重算成目标内核的偏移,后续按该偏移的读取才落得到字段。

### 5.6 缺失-BTF 回退策略:三情形,三选项

层⑤的前提是目标机有「目标 BTF」;不在场时先分三种情形(00-env ③④ 两段正是为此而设):

- **情形一:旧内核**。既没有 `/sys/kernel/btf/vmlinux` 导出,也没有 `CONFIG_DEBUG_INFO_BTF` 开关——判别不查版本表,用 1.3 的三条证据(文件在场/dump 有输出/CONFIG=y),三条全空即此情形;
- **情形二:CONFIG 未开**。内核够新但构建没编 BTF——代码证据:`btf_vmlinux_init`(kernel/bpf/sysfs_btf.c:31,v6.6.78)开头先量 `__stop_BTF - __start_BTF`(:33),量出 0 直接 `return 0`(:35-36)——说明书没嵌进映像,sysfs 不注册,`/sys/kernel/btf` 整个不存在;
- **情形三:可见性被掩蔽**。宿主机上文件在,你的视图里没有——容器/沙箱可能掩蔽 `/sys` 子树或收窄权限;判别:到宿主命名空间 `ls /sys/kernel/btf/` 对照。

确认缺了再谈回退,三选项按序评估:

1. **升级内核/换带 BTF 的内核 = 正解**。一劳永逸,CO-RE 全链复活;发行版没给就按[附录 C](../appendix/c-distro-btf-build-guide.md) 自行构建。变体:内核版本相同、只是没编 BTF——把**同版本**内核的 BTF 文件喂给 libbpf(open 选项 `btf_custom_path`,tools/lib/bpf/libbpf.h:142;带 CO-RE 记录的对象本会在加载期强制装载内核 BTF,给了自定义路径即不强制,libbpf.c:3053)——前提是 BTF 与目标内核同源,错版本的说明书比没有更危险;
2. **BCC 运行期编译 = 旧路回退**。不要求目标 BTF,现场对目标内核编译——代价是 3.1 立案的三件事原样回来:头文件依赖、每次启动重编、产物不可复用。过渡或一次性任务可用,当架构选是倒退;
3. **固定偏移裸读 = 危险末选**。把本机偏移写死进程序——把 2.4「ID 是 blob 坐标」的教训换成字节坐标重犯一遍。第 2 章排错表早有点名:「按固定偏移硬读会读错位置」(ch02:851,该行开的处方正是 vmlinux.h+CO-RE 工作流);CO-RE 的存在就是为了让这条路消失。

### 5.7 模块 BTF 与 attach_btf_obj_fd:第二层说明书的用法

1.4 的双层实况在此落地:模块自带 split BTF(增量,接续 vmlinux 的 ID 编址——本机模块抽样首行 `[170551]`,恰在 vmlinux 最大 ID `[170550]` 之后,本机实况)。谁消费它?追**模块内**符号的程序:fentry/fexit 挂到某模块的函数时,attach 目标的类型 ID 要在**该模块的 BTF** 里解析,加载参数用 `attach_btf_obj_fd`(include/uapi/linux/bpf.h:1461,v6.6.78)带走这份 BTF 的 fd——uapi 注释原话「or valid module BTF object fd or 0 to attach to vmlinux」(:1460):0 给 vmlinux,fd 给模块。链路三站:libbpf 侧 `btf__load_module_btf`(tools/lib/bpf/btf.c:4809)按名拼 `/sys/kernel/btf/<模块名>`(:4813),解析出以 vmlinux 为基底的 split BTF,fd 缓存进 `prog->attach_btf_obj_fd`(libbpf.c:6875);内核侧加载时 `btf_get_by_fd(attr->attach_btf_obj_fd)` 取回(kernel/bpf/syscall.c:2641),并校验确属内核 BTF(`btf_is_kernel`,:2644)。它与四节的补丁通道并行不悖:追内核类型的 CO-RE 重定位不受影响——4.3 的候选查找本来就先 vmlinux 后模块(:8391/:8400)。

### 5.8 教师视角与全链清单

**教师视角**。学生容易把五层当五道仪式,要点破的是**依赖链逐层兑现**:层①证明原料与工具在场,层②把说明书翻译成编译器形态,层③训练人读说明书的手感,层④消费说明书产出记录,层⑤拿记录对目标机对账。每层把守一个失败面:环境缺项在层①暴露(③④的 WARN),类型缺失在层④编译期暴露(ch02:853,3.4 已回收),布局错位在层⑤加载期暴露(4.2 的 -ESRCH/-EINVAL)——排错节的三类故障正按「在哪层暴露」归位。

**学生动手**(全链一遍,各层实录见 5.1-5.5 与四节 4.6;汇总成表):

| 层 | 命令(在 book/examples/chapter-08/ 下) | 资产 | 档位 |
|---|---|---|---|
| ① | `bash 00-env/check-env.sh` | [00-env](../examples/chapter-08/00-env/) | 编译级 |
| ② | `cd 02-vmlinux-h && ./gen-vmlinux-h.sh` | [02-vmlinux-h](../examples/chapter-08/02-vmlinux-h/) | 编译级 |
| ③ | `cd 01-btf-kinds && make` 后 `btf dump file kinds.bpf.o` | [01-btf-kinds](../examples/chapter-08/01-btf-kinds/) | 编译级 |
| ④ | `cd 03-core-read && make`;`cd ../04-relo-slicer && python3 slice-relos.py ../03-core-read/core.bpf.o` | [03](../examples/chapter-08/03-core-read/)/[04](../examples/chapter-08/04-relo-slicer/) | 编译级 |
| ⑤ | `sudo 03-core-read/core_read` | [03-core-read](../examples/chapter-08/03-core-read/) | 预期输出示例/运行级待验证 |
| 走读 | `bash 05-kernel-relo/trace-kernel-relo.sh` | [05-kernel-relo](../examples/chapter-08/05-kernel-relo/) | 编译级(只读树) |

**检查点**(自测后看提示):

1. 五层里哪几层在编译机、哪几层在目标机?跨机携带的「行李」具体是哪些字节?——提示:层④的产物;`.BTF`/`.BTF.ext` 随 .bpf.o 走,目标 BTF 不随行、由层⑤在目标机现取。
2. 目标机没有 `/sys/kernel/btf/vmlinux`,先做什么判别、再按什么顺序选回退?哪个选项不是可移植、只是碰运气?——提示:5.6;三条证据定情形;升级→BCC→固定偏移,末选重犯 2.4 的教训。
3. fentry 挂到模块内函数,attach 目标的类型 ID 在哪本说明书里解析、fd 走哪个加载参数字段?——提示:5.7;模块 BTF(split BTF);`attach_btf_obj_fd`(uapi bpf.h:1461)。

**独立示例**:本节把六件资产连成一线——[00-env/](../examples/chapter-08/00-env/) 环境自检、[01-btf-kinds/](../examples/chapter-08/01-btf-kinds/) kind 剖析、[02-vmlinux-h/](../examples/chapter-08/02-vmlinux-h/) vmlinux.h 生成、[03-core-read/](../examples/chapter-08/03-core-read/) CO-RE 闭环、[04-relo-slicer/](../examples/chapter-08/04-relo-slicer/) 记录切片、[05-kernel-relo/](../examples/chapter-08/05-kernel-relo/) 内核引擎走读。

## 可视化:CO-RE 全链与两引擎对照

**图 1:CO-RE 全链四段图(矩阵⑥)。**

```text
      编译机(一次)                            目标机(每次加载)
────────────────────────                  ────────────────────────
① 编译期 BTF                              ③ 运行期目标 BTF
   /sys/kernel/btf/vmlinux                 /sys/kernel/btf/vmlinux+各模块
   └ bpftool btf dump format c             (目标内核真实布局,随版本漂移)
     → vmlinux.h [02 资产生成]              libbpf:btf__load_vmlinux_btf(btf.c:4767)
        │ #include + clang -g(3.3 命门)     [00-env ③ 探测]
        ▼                                       ▲
② 重定位记录 .BTF.ext(行李)               │ 按名字+结构重建对应(四节)
   随 .bpf.o 跨机        [03 编译产物]      │ 候选查找+兼容检查(先 vmlinux 后模块)
   struct bpf_core_relo 四字段 [04 站 3]    ④ 补丁后访问:imm/offset 逐条改写
   13 种 kind(uapi bpf.h:7262-7276)        普通骨架:libbpf 补(relo_core.c:1024)
                                            轻骨架:内核补(bpf_core_apply btf.c:8423)[05 五站]
```

**解读(≥150 字)**:四段的接力就是本章的目录:段①②在编译机各跑一次(02 资产产头文件;03/04 资产把每处字段访问登记成四字段记录入 `.BTF.ext`);段③④在目标机每次加载重跑(00-env 探测目标 BTF;补丁逐条改写 imm/offset)。行李里没有说明书:跨机携带的只有 .BTF/.BTF.ext,目标布局永远现场读、现场算,「编译一次、处处运行」的分界线就在段②与段③之间。

**图 2:两引擎对照表图(四节的收束)。**

```text
                libbpf 引擎(用户态)            内核引擎(verifier 收件后)
入口函数        bpf_object__relocate_core       bpf_core_apply
                (libbpf.c:5783)                 (btf.c:8423)
候选查找        bpf_core_find_cands(:5604)     bpf_core_find_cands(:8336)
                目标 BTF:vmlinux→模块           目标 BTF:vmlinux→模块
补丁对象        libbpf 即将提交的指令数组        加载器程序 blob 里的目标程序指令
使用者          普通骨架(skeleton)            轻骨架(gen_loader)
失败语义        -ESRCH(libbpf btf.c:4803);候选空       同一套 uapi 记录与 kind 口径
                按 kind 分流(relo_core.c:1395)  兼容判定各自调参(递归深度 32 对 2)
```

**解读(≥150 字)**:两台引擎吃的记录同一种、通道同一条(uapi)、候选顺序同为 vmlinux→模块,解算逻辑同源、各自维护,接口对齐 uapi——「协议统一,实现分治」收束成这张表。差异集中在两行:补丁对象行决定「谁必须在场」——普通骨架补用户态内存里的指令数组,libbpf 天然在环上;轻骨架的补丁发生在加载器程序于内核内运行之时,libbpf 已不在环,只能内核接(4.5)。「候选空按 kind 分流(EXISTS 补 0、其余 poison)」两列同口径:死代码让验证器淘汰,活代码加载失败并指到指令号。

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

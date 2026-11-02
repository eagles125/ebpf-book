# 第 8 章完整教学章节技术设计(BTF 与 CO-RE 原理,机制章 + CO-RE 全链编译级实证)

日期:2026-11-02|前置:第 5 章(源码地图)、第 6 章(JIT)、第 7 章(helper/映射)已收口|走读树:/work/linux/linux-6.6.78

## 1. 背景与目标

### 1.1 矩阵任务(openspec tasks.md「Chapter 8: BTF and CO-RE Principles」,8 条)

1. 完整章节正文:BTF 目标、DWARF→BTF 转换、类型 ID、字段偏移、CO-RE 重定位;
2. 剖析:内核 BTF 文档(btf.rst)与 libbpf 重定位行为;
3. 集成示例:用 `vmlinux.h` 读内核结构体字段的 CO-RE 示例;
4. `book/examples/chapter-08/` 独立示例;
5. 讲清 `CONFIG_DEBUG_INFO_BTF`、`pahole`、`bpftool btf dump` 与缺失-BTF 回退策略;
6. 文本可视化:编译期 BTF→对象重定位记录→运行期目标 BTF→补丁后访问;
7. 排错、总结、参考;
8. 主审:CO-RE 边界条件 + 非绝对可移植措辞专项。

### 1.2 前章预告回收清单(正文必须兑现)

| 埋线 | 位置 | 本章兑现 |
|---|---|---|
| 「跨版本访问由 CO-RE 重定位在加载时修正」 | 第 2 章一节代码注释(02:95) | 三节:重定位记录怎么来;四节:加载期怎么补 |
| 「换内核字段错位→vmlinux.h+CO-RE 工作流」 | 第 2 章排错表(02:851) | 三节工作流+五节实战;排错表回收该条 |
| 「字段编译期不存在→CO-RE 存在性查询兼容分支」 | 第 2 章排错表(02:853) | 三节宏族(bpf_core_field_exists :132)+练习② |
| 「BTF 不只服务 CO-RE,也是运行期字段级能力登记簿」 | 第 7 章(07:255,spin_lock 的 btf_record) | 一节:BTF 的多重身份;回指不展开 |
| 「vmlinux.h 从哪来/类型 ID 怎么编址/重定位何时完成/bpftool btf dump 是入口」 | 第 7 章总结(07:603) | 二节编址、三节生成、四节时机——本章正题 |
| 「第 8 章之后进入动手篇」 | 第 7 章导读(07:36) | 总结展望:第 9 章起全部依赖本章机制 |
| 构建链 `bpftool btf dump → vmlinux.h` 步骤 | ch07/ch09 示例 Makefile | 02-vmlinux-h 资产 + 三节:该步骤的机制解释 |

### 1.3 定位与本章独有的验证口径

定位:机制章(第 5–7 章讲「程序怎么跑、依赖什么」,本章讲「程序怎么做到编译一次、多内核运行」——BTF 是类型说明书,CO-RE 是消费方式)。验证口径三档:

- **编译级已验证**(本机可复现,无 root):`bpftool btf dump file /sys/kernel/btf/vmlinux` 实跑(401978 行,task_struct id=156855 size=9920 vlen=274);`format c` 生成 vmlinux.h 3500327 字节;clang 18 `-g -target bpf` 产出 `.BTF`(0x17a)+`.BTF.ext`(0x9c)+`.rel.BTF.ext`(0x60)实跑;bpftool 读自编对象 BTF(STRUCT 'my_type' size=24 vlen=3 带位偏移)实跑;python 解析 .BTF.ext 可行;切片器/机检/只读 CONFIG 实跑。
- **运行级待验证**(预期输出示例):skeleton 加载、CO-RE 字段修正后的实际读取、attach——`unprivileged_bpf_disabled=2` 无 root,一律「预期输出示例 / 运行级待验证」。
- **方法论级**(本机不可复现,给方法与判读法):DWARF→BTF 转换(**pahole 不在 PATH**——`pahole -J vmlinux` 流程讲清即可,标注「本机未装 pahole,转换级验证需自备工具链」)。

工具链实证:clang 18.1.3、llvm-objdump 18、bpftool 7.7.0、libbpf 1.3.0、python3 3.12.3;/sys/kernel/btf/ **110 个文件**(vmlinux 7119843 字节 mode 444 全局可读 + 109 模块);CONFIG_DEBUG_INFO_BTF=y、CONFIG_DEBUG_INFO_BTF_MODULES=y、MODULE_ALLOW_BTF_MISMATCH 未设。

## 2. 示例资产设计(方案 A:6 资产全链,用户 2026-11-02 拍板)

目录(book/examples/chapter-08/):

| 目录 | 类别 | 内容与要点 |
|---|---|---|
| `00-env/` | 环境自检 | check-env.sh:走读树+本章走读文件(btf.c/relo_core.c/bpf_core_read.h/btf.h uapi)→ 工具链(bpftool btf dump 探针、clang -g 探针、python3)→ BTF 家族(/sys/kernel/btf 文件数、vmlinux 大小、模块 BTF 抽样)→ CONFIG 三项 → **pahole 缺席声明**(方法论档);无 root 可跑,三档结论 |
| `01-btf-kinds/` | 编译级剖析 | kinds.bpf.c(刻意覆盖多 kind:struct/union/enum/typedef/const/ptr/array/func/func_proto/全局变量 VAR+DATASEC/btf_decl_tag)+Makefile+README:`bpftool btf dump file kinds.bpf.o` 逐一指认 kind 与类型 ID 编址;剩余 kind(FWD/FLOAT/ENUM64/TYPE_TAG 等)从 vmlinux BTF 抽样指认 |
| `02-vmlinux-h/` | 编译级生成 | gen-vmlinux-h.sh(`bpftool btf dump file /sys/kernel/btf/vmlinux format c`)+README:3.5MB 规模统计、头文件结构导读、**回收 ch07/ch09 构建链**(那两章 Makefile 里的同款步骤) |
| `03-core-read/` | 运行型示例(编译级验证) | 四件套:core.bpf.c(tracepoint sched/sched_process_exec,`bpf_get_current_task_btf()` + `BPF_CORE_READ` 读 task_struct 字段 pid/comm,含一个 `bpf_core_field_exists` 兼容分支演示)、loader.c(libbpf skeleton 全链)、Makefile(沿用 ch09 bootstrap 模式)、README;编译级全链零告警;读取输出=预期档标注 |
| `04-relo-slicer/` | 切片器(python) | slice-relos.py 五站:①ELF 段布局(.BTF/.BTF.ext/.rel.BTF.ext 尺寸,llvm-objdump -h)②btf_ext 头(magic/长度/各段偏移)③core_relo 记录解析(insn_off/type_id/access_str_off/kind)④kind 对照 uapi 枚举(bpf.h:7262)⑤access string 解码与源码字段对照;对 03 的 core.bpf.o 实跑,记录数>0 断言 |
| `05-kernel-relo/` | 切片器(bash) | trace-kernel-relo.sh 五站:①内核引擎入口 `bpf_core_apply`(btf.c:8423)②候选查找 `bpf_core_find_cands`(:8336)与兼容 `bpf_core_types_are_compat`(:8094)③verifier 接线 `check_core_relo`(verifier.c:15718,调用点 :15778/:15817)④UAPI 载体(attr core_relo_cnt/core_relos/core_relo_rec_size bpf.h:1463-1466)⑤轻骨架 `gen_loader.c`(:970 attr.core_relo_cnt = gen->core_relo_cnt)——两引擎对比站 |
| `scripts/` | 机检 | verify-citations.sh(同 ch05/06/07 契约,数据源=章级树根 citations.txt) |
| `citations.txt` | 锚点清单 | 树根,格式「文件\|符号\|说明」,约 40 行五组:①BTF 基础与解析(btf.c :5720 btf_parse_vmlinux 等)②kind 体系(uapi btf.h :60-79 等)③CO-RE 编译期(bpf_core_read.h :70/:88/:132/:185/:216/:246/:351;uapi bpf.h :7262/:7326)④libbpf 引擎(relo_core.c :146/:263/:1024/:1156/:1280/:1544;libbpf.c :4236/:5783)⑤内核引擎+UAPI(btf.c :8094/:8336/:8423;verifier.c :15718/:15778/:15817;bpf.h :1463-1466;gen_loader.c :970) |
| `exercises/` | 练习 | 01-relo-kind-detect(给 .bpf.o 用 04 切片器判读 kind 语义,check 脚本断言)、02-core-compat(bpf_core_field_exists 跨版本兼容分支参考实现+机检),防剧透结构 |

命名与编号沿用 ch05/06/07 惯例(00-env 起,剖析/生成居前、运行型居中、切片器居后,scripts/exercises 收尾)。

## 3. 章节正文设计

### 3.1 结构(完整模板十一小节,五段为正文主轴)

```text
# 第 8 章 BTF 与 CO-RE 原理
## 学习目标
## 本章导读
## 一、BTF:内核的类型说明书            ← 矩阵①⑤(目标/DWARF→BTF/CONFIG/pahole)
## 二、BTF kind 体系与类型 ID 编址      ← 矩阵①(19 种 kind/三元组/字段偏移与位域)
## 三、CO-RE 编译期:从宏到重定位记录   ← 矩阵①②③(intrinsics/宏族/BTF.ext/12 种 kind)
## 四、两台重定位引擎                   ← 矩阵②(libbpf 用户态 vs 内核态+轻骨架)
## 五、实战五层:一条 BPF_CORE_READ 的完整旅程 ← 矩阵③④⑤(含缺失-BTF 回退)
## 可视化:CO-RE 全链与两引擎对照        ← 矩阵⑥
## 常见问题与排错                        ← 矩阵⑦
## 课后扩展任务
## 总结
## 本章参考来源
```

各段主轴末「**独立示例**」链接(一→00-env/02-vmlinux-h;二→01-btf-kinds;三→03-core-read/04-relo-slicer;四→05-kernel-relo;五→全部资产)。

### 3.2 各节要点

**一、BTF:内核的类型说明书(8–10k)**:为什么需要 BTF(DWARF 大且只在编译期;BTF 紧凑且**常驻内核**——验证器 ctx 校验、attach_btf_id、字段级能力登记簿[回指 ch07:255 spin_lock]三重身份);DWARF→BTF 转换=方法论级(pahole -J 流程+CONFIG_DEBUG_INFO_BTF 在内核构建期完成,**本机 pahole 缺席**标注);vmlinux+模块双层 BTF 实况(110 文件/6.8MB 主+模块各自小文件;`btf_parse_vmlinux` btf.c:5720 内核侧装载);`bpftool btf dump` 三个用法预告(file 对象/file /sys/format c);教师视角(为什么不用 DWARF)/学生动手(00-env 自检+02 生成)/检查点 3 问。

**二、BTF kind 体系与类型 ID 编址(7–9k)**:`struct btf_type` 三元组(name_off/info:kind+vlen 位域/size);19 种 kind 分四组讲(uapi btf.h :60-79):基础(INT/PTR/ARRAY/STRUCT/UNION/ENUM/ENUM64/FWD)、修饰(VOLATILE/CONST/RESTRICT)、功能(TYPEDEF/FUNC/FUNC_PROTO)、扩展(VAR/DATASEC/FLOAT/DECL_TAG/TYPE_TAG);STRUCT 的成员表 btf_member 与 **bits_offset 位域偏移**(01-btf-kinds 实物:'my_type' size=24 vlen=3 带 bits_offset);类型 ID 编址:从 1 顺序递增、void=0、跨 Section 共享命名空间(task_struct id=156855 实况);`btf_type_by_id`(include/linux/btf.h);教师视角(为什么 ID 而不是名字)/学生动手/检查点。

**三、CO-RE 编译期(9–11k)**:问题陈述(BCC 旧路:运行期编译、头文件依赖、每次启动重编;CO-RE 目标=编译一次处处运行);工作流四件:vmlinux.h 生成(02 实物 3.5MB,回收 ch07/ch09 构建链)→ `-g` 编译(**没有 -g 就没有 .BTF.ext**,实证探针)→ clang 重定位内建 `__builtin_preserve_access_index`(宏族背后)→ 加载期补丁(预告四节);宏族逐个(bpf_core_read.h :246/:351/:132/:185/:216/:88/:70)——读值/读入/存在性查询/位域;**BTF.ext 重定位记录**:uapi `struct bpf_core_relo` :7326 四字段、`enum bpf_core_relo_kind` :7262 十二种(0–11:字段字节偏移/大小/存在/符号/左右移/本地类型 ID/目标类型 ID/类型存在/类型大小/枚举值存在/枚举值)分组讲解;访问字符串 `0:1:2` 语义与 spec 三元组(类型 ID:字段索引:数组下标,`bpf_core_parse_spec` relo_core.c:263);04-relo-slicer 五站实物(记录数>0、kind 落在 0–11);教师视角(为什么记录在 .BTF.ext 而不是普通 ELF 重定位)/学生动手/检查点。

**四、两台重定位引擎(6–8k)**:libbpf 用户态引擎:collect(`bpf_object__collect_prog_relos` libbpf.c:4236,解析 .BTF.ext :6763)→ relocate(`bpf_object__relocate_core` :5783,调用点 :6526;候选查找/兼容检查/计算/补丁四步:`bpf_core_calc_relo_insn` relo_core.c:1280、`bpf_core_patch_insn` :1024;兼容判定 `__bpf_core_types_are_compat` :146/`__bpf_core_types_match` :1544);**内核态引擎**:uapi 载体(attr core_relos bpf.h:1465)→ verifier `check_core_relo`(verifier.c:15718,接线 :15817)→ `bpf_core_apply`(btf.c:8423,候选 `bpf_core_find_cands` :8336);谁在用内核态:**轻骨架 gen_loader**(libbpf.c:7296 记录→gen_loader.c:970 提交)——「普通骨架 libbpf 补丁,轻骨架内核补丁」;两引擎共享逻辑(relo_core.c 与 btf.c 内核侧实现**逻辑同源、各自维护**——保守措辞);失败语义:候选找不到→-ESRCH/-EINVAL[预期口径];教师视角(为什么内核也要一台)/学生动手(05 切片器五站)/检查点。

**五、实战五层(7–9k)**:层①环境(00-env:110 文件/CONFIG 三项/三档结论)→层②生成(02:vmlinux.h 结构导读——为什么能生成 2 万+行声明)→层③剖析(01:自编对象 BTF 指认)→层④编译与记录(03:core.bpf.c 全链编译零告警+04:记录切片)→层⑤加载补丁[预期口径](skeleton 加载流程、字段修正演示的**预期输出示例**、attach、读取);**缺失-BTF 回退策略**(矩阵⑤):/sys/kernel/btf/vmlinux 不存在的三种情形(旧内核/CONFIG 未开/容器掩蔽)→回退选项逐个评估(升级内核=正解;BCC 运行期编译=旧路回退;固定偏移裸读=危险末选——回指 ch02:851);模块 BTF 与 attach_btf_obj_fd(uapi bpf.h:1458 一带);教师视角/学生动手(全链跑一遍)/检查点。

**可视化(1.5–2k)**:图①**CO-RE 全链四段图**(矩阵⑥:编译期 BTF[vmlinux.h/.BTF]→对象重定位记录[.BTF.ext 十二种 kind]→运行期目标 BTF[/sys/kernel/btf]→补丁后访问[imm/offset 改写],标注各段所在文件与本章资产对应);图②两引擎对照表图(libbpf vs kernel:入口函数/候选查找/补丁对象/使用者[普通骨架 vs 轻骨架]/失败语义);各配 ≥150 字解读。

**排错(2.5–3.5k)**:编译类(漏 -g→.BTF.ext 空/字段在本机 BTF 不存在→clang 报错=编译期暴露,回指 ch02:853/类型冲突 vmlinux.h 与自定义头重复定义)、加载类(目标机无 BTF→libbpf 重定位失败[预期口径]/CO-RE 候选不兼容→-ESRCH 语义/模块 BTF 缺失)、走读类(切片器锚点漂移/机检三类,回指 ch05)、环境类(/sys/kernel/btf 可读性差/CONFIG_DEBUG_INFO_BTF 未开→回退策略)。

**课后(1–1.5k)+总结(5 点呼应目标)+参考**:练习① 01-relo-kind-detect(判读 kind 语义);练习② 02-core-compat(存在性查询兼容分支,回收 ch02:853)。总结五点:BTF 三重身份/19 种 kind 与 ID 编址/BTF.ext 十二种 kind/两台引擎/缺失-BTF 回退。参考:源码树(btf.c/btf.h uapi/relo_core.c/bpf_core_read.h/gen_loader.c)、Documentation/bpf/btf.rst 与 clang-notes.rst、llvm_reloc.rst、uapi bpf.h;回指 ch02 1.6/ch07 三处/ch09 构建链。

### 3.3 篇幅预算(总 40–55k,wc -m,五段扩展口径,用户拍板)

目标+导读 2–2.5k|一 8–10k|二 7–9k|三 9–11k|四 6–8k|五 7–9k|可视化 1.5–2k|排错 2.5–3.5k|课后 1–1.5k|总结+参考 1.5–2k。分节服从章级。

### 3.4 课后练习设计(2 项,防剧透+机检)

- 练习① `exercises/01-relo-kind-detect/`:给一个预置 .bpf.o(含 ≥3 种 kind 的重定位记录),用 04-relo-slicer 输出判读每条记录「补丁什么、怎么补」;check 脚本断言切片器输出含指定 kind 值与记录数。
- 练习② `exercises/02-core-compat/`:仿 03 写 compat.bpf.c——用 `bpf_core_field_exists`/`bpf_core_type_exists` 构造跨版本兼容分支(参考实现=完整可编译+逐行讲解+机检锚点)。

## 4. 验证策略

- 已验证(硬性):机检实跑全绿;01/02/03 **编译级**全过(kinds.bpf.o BTF dump 指认+vmlinux.h 生成+core 全链 clang/bpftool gen skeleton/gcc 零告警);04 切片器 python 实跑(记录数>0、kind∈[0,11]);05 切片器 bash 实跑全站 NOT FOUND=0+负路径 exit 1;只读 CONFIG/BTF/ELF 段读取;`llvm-objdump -h` 段尺寸断言(.BTF.ext>0)。所有「编译级已验证」结论均以实跑输出为证。
- 预期(运行级待验证,需 root):skeleton 加载、attach、字段修正后实际读取、compat 分支运行时行为——全部「预期输出示例」标注。
- 方法论级(本机不可复现):pahole DWARF→BTF 转换——给流程与判读法,标注「本机未装 pahole」。
- 保守措辞:零「已验证通过」式运行级表述;relo_core.c 双实现写「逻辑同源、各自维护」。
- 主审矩阵⑧专项:CO-RE 边界逐条(位域/数组/指针链/union/跨类型 ID)+「可移植≠绝对」措辞(版本窗口、字段存在≠语义一致)+ relo kind 名称对照 uapi。

## 5. 技术风险与边界

1. **内核 BTF 随版本漂移**:类型 ID(task_struct id=156855)、结构尺寸(9920)、/sys/kernel/btf 文件数(110)全是**本机 7.0.0-38 实况**,其他内核必不同——正文统一标注「本机实况,其他内核数值不同」,机检按符号不按数值。
2. **vmlinux.h 3.5MB 不入库**:.gitignore 全局规则已覆盖(沿用 ch09/ch07 模式);02 资产 README 记录生成方法与规模统计。
3. **clang/llvm 行为敏感**:.BTF.ext 记录布局随 clang 大版本稳定,但探针输出(段尺寸)标注「clang 18 实测」。
4. **relo_core.c 双实现**:不得写「内核拷贝 libbpf」或「libbpf 拷贝内核」;写「逻辑同源、各自维护,接口对齐 uapi」。
5. **运行级=预期档**(同前章口径);方法论级(pahole)单独标注,不冒充已验证。
6. **不重复前章**:vmlinux.h 构建链用法(ch07/ch09 已示范)只解释机制不重讲用法;BTF 字段登记簿(btf_record)回指 ch07:255 不展开;验证器 BTF 用途回指 ch03。
7. **03 示例的字段选择**:task_struct 字段随版本漂移正是 CO-RE 的用武之地——README 明示「字段在本机 BTF 存在才可编译;这正是本章要教的工作流」。

## 6. tasks.md 回写清单

「Chapter 8: BTF and CO-RE Principles」8 条全 [x]:①T 正文五段(BTF 目标/DWARF→BTF/kind/字段偏移/CO-RE)②T 三/四节+05 切片器(内核 BTF 文档+libbpf 重定位行为)③T 三节/五节+03 示例(vmlinux.h 读内核字段)④T1–T7 独立示例资产⑤T 一节+五节(CONFIG/pahole/bpftool btf dump/缺失-BTF 回退)⑥T 可视化(编译期 BTF→记录→目标 BTF→补丁)⑦T 排错/总结/参考⑧T 主审专项(CO-RE 边界+非绝对可移植措辞)。

## 7. 测试策略汇总

每任务硬性:脚本 `bash -n` + 实跑断言;python 脚本 `-m py_compile` + 实跑断言;示例 `make` 全链真实构建(零告警);切片器全站 NOT FOUND=0 + 负路径 exit 1;机检 FAIL=0;正文 wc -m 分节与章级双查(40–55k);保守措辞 grep(「预期输出示例/运行级待验证/方法论级」三标注);主审含 CO-RE 边界专项 + kind 名称对照 uapi + 行号抽检 ≥15 处。

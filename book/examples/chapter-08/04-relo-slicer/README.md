# 04-relo-slicer:.BTF.ext 重定位记录切片器(python 五站)

## 这是什么

`slice-relos.py` 把 clang 编出的 BPF 目标文件(object)里的 `.BTF.ext` 段逐字节切开。
上一站(`../03-core-read/`)用 `llvm-objdump -dr` 看到「4 条 CO-RE 重定位」的**注解版**;
本切片器给出同一批记录的**字节版**:魔数、头、小节、每条记录的四个字段、访问链解码,
最后回源码对上行——让你确认「源码一行 `BPF_CORE_READ`,对应 `.BTF.ext` 一条记录」
不是口号,而是可以逐字节点数的机械事实。

走读树的坐标:`Documentation/bpf/btf.rst`「4.2 .BTF.ext section」定义了 `.BTF.ext` 的
头与三个信息小节;记录本体是 uapi `include/uapi/linux/bpf.h` :7326 的
`struct bpf_core_relo`;访问链的解析语义在 `tools/lib/bpf/relo_core.c`
(`bpf_core_parse_spec`,:263 起)——本切片器就是这三处规范的 stdlib 实现。

## 调用方式

```bash
python3 slice-relos.py <path/to/.bpf.o>
# 本章标准输入(上一站的产物,留盘存在):
python3 slice-relos.py ../03-core-read/core.bpf.o
```

输出为五个「`## 站 n:…`」小节 + 末尾「`## 汇总`」;末尾有一行机器可读的
`RELOS=<记录数>`,可用 `grep '^RELOS='` 取用。站内各行为 `OK/PASS/WARN/FAIL`
前缀分档(与 `../00-env/check-env.sh` 同款口径)。

## 依赖

- **python3(仅标准库)**:`struct` 逐字节解析、`subprocess` 调 `llvm-objdump -h`、
  `os/sys/re`;无任何第三方包。
- **llvm-objdump**:只用于站 1 的段表展示与交叉验证;缺失或调用失败时站 1 降级为
  脚本自解析段表并打 `WARN`,不改变退出码。
- 源码对照素材 `core.bpf.c`:与输入对象同目录;不在场只降级,站 5 跳过对照。

安全:只读输入与源码,不写盘、不加载内核对象、无网络;subprocess 固定参数且
不经 shell,输入路径不会被解释执行。

## 五站逐站解读(以下摘录均为本机实跑,clang 18 + llvm-objdump 18 + BTF v1;
数值是本机实况,其他内核/构建不同)

### 站 1:ELF 段布局(.BTF/.BTF.ext/.rel.BTF.ext 尺寸)

llvm-objdump 与脚本自解析两条路各报一次三段尺寸,互为交叉验证:

```
llvm-objdump -h 摘录(尺寸列为 16 进制,原文如下):
   16 .BTF                                    0000778a 0000000000000000
   18 .BTF.ext                                0000017c 0000000000000000
   19 .rel.BTF.ext                            00000140 0000000000000000
PASS  交叉校验:llvm-objdump 与自解析的尺寸一致(3 个段两源都在场)
```

读法:`.BTF.ext` 只有 0x17c=380 字节——重定位元数据小得与它的体量相称;对照
`../01-btf-kinds/` 的悬念(无 CO-RE 访问的 kinds.bpf.o 只有 128 字节 `.BTF.ext`),
多出的正是 core_relo 小节。`.rel.BTF.ext` 是 llvm 产、作用于 `.BTF.ext` **自身**的
ELF 重定位(`llvm_reloc.rst` :233 的 R_BPF_64_NODYLD32 一族),切片器只量尺寸不解析。

### 站 2:btf_ext 头(magic 0xEB9F/长度/各段偏移)

```
OK    magic   = 0xeb9f(BTF_MAGIC,BTF 家族统一门牌)
OK    version = 1(BTF_VERSION=1)
OK    flags   = 0(libbpf 要求为 0)
OK    hdr_len = 32(头的字节数;其后各段偏移一律相对头【末尾】——读 .BTF.ext 的关键)
OK    func_info_off=   0 len=  20(函数信息;定位展示,本切片器不解析)
OK    line_info_off=  20 len= 252(行号信息;同上)
OK    core_relo_off= 272 len=  76(CO-RE 重定位小节——站 3 的原料)
PASS  自洽:头 + 三段首尾相接共 380 字节 = .BTF.ext 段尺寸(尾部余量 0;…)
```

两个关键读法:其一,`hdr_len=32` 说明头部带着**可选的** core_relo 两个字段
(`hdr_len=24` 的老头没有,切片器据此判「记录数=0」);其二,头内偏移全部相对
**头末尾**,绝对位置要加一遍 `hdr_len`(core_relo 小节实际从 32+272=304 字节处开始)。

### 站 3:core_relo 记录解析(insn_off/type_id/access_str_off/kind)

```
OK    core_relo_rec_size = 16(uapi struct bpf_core_relo = insn_off/type_id/access_str_off/kind 四个 __u32)
OK    小节 "tracepoint/sched/sched_process_exec"(sec_name_off=68):4 条记录
      #0 insn_off=  72(0x0048) type_id= 12 access_str_off=11446 kind=0(BPF_CORE_FIELD_BYTE_OFFSET)
      #1 insn_off= 144(0x0090) type_id= 12 access_str_off=11487 kind=0(BPF_CORE_FIELD_BYTE_OFFSET)
      #2 insn_off= 200(0x00c8) type_id= 12 access_str_off=11540 kind=2(BPF_CORE_FIELD_EXISTS)
      #3 insn_off= 232(0x00e8) type_id= 12 access_str_off=11540 kind=0(BPF_CORE_FIELD_BYTE_OFFSET)
PASS  断言:记录总数 = 4 > 0
```

与 `../03-core-read/README.md` 的 `llvm-objdump -dr` 摘录逐条对上:insn_off 72/144/200/232
就是那边 0x48/0x90/0xc8/0xe8 四条注解,type_id 12 是对象 BTF 里 `struct task_struct`
的 ID。注意 `insn_off` 在 ELF API 里是段内**字节**偏移(内核 API 才按指令条数计),
72/8=9 → 第 9 条指令,与 03 README「第 25 条」(200/8)的算法一致。

### 站 4:kind 对照 uapi 枚举(0–11 名称表)

打印权威名称表(走读树 `include/uapi/linux/bpf.h` :7262 起 `enum bpf_core_relo_kind`,
中文含义取自同行注释),再对每条记录做判定与分布统计:

```
  0  BPF_CORE_FIELD_BYTE_OFFSET   字段字节偏移(field byte offset)
  …
 11  BPF_CORE_ENUMVAL_VALUE       枚举值整数值(enum value integer value)
PASS  断言:全部 kind ∈ [0,11];分布:BPF_CORE_FIELD_BYTE_OFFSET×3、BPF_CORE_FIELD_EXISTS×1
```

口径说明:同一 uapi 文件在 11 之后还定义了 12 = `BPF_CORE_TYPE_MATCHES`(较新内核
追加);本章切片器的断言口径是 **[0,11]**,记录里出现 12 会按越界 FAIL 并点名。

### 站 5:access string 解码与源码字段对照

先给一个**重要辨析**:记录里的 `access_str_off` 相对**主 .BTF 的字符串区**起点——
`.BTF.ext` 自身没有字符串区(btf.rst 4.2 节未定义任何字符串段,段尾即最后一条记录;
libbpf `relo_core.c` :1308 也是 `btf__name_by_offset(local_btf, …)` 取串)。本机实况:
字符串区起点 = hdr_len 24 + str_off 18092 = 18116,长度 12486 字节。

每条记录:解出 `0:1:2` 形式的访问链 → 按链走本机 BTF 拿成员名与位偏移 → 对照指令
立即数 → 回源码找调用行:

```
  #0 access_str_off=11446 → "0:94"
      根下标 0(根实体取 [0]:指针语义即解引用一次;位偏移 +0)
      第 1 层:STRUCT task_struct 成员 #94 = pid(位偏移 +22400)
      汇:本机位偏移 22400 = 字节 2800(本机实况,其他内核数值不同——…)
      指令立即数:insn_off=72 处 imm=2800(0xaf0)= 本机字节偏移,PASS 一致
      源码对照:core.bpf.c:81  e->pid = BPF_CORE_READ(task, pid);
```

四条记录的对照结果(本机实况):`0:94`=pid@字节 2800、`0:129`=comm@3344、
`0:258`=kstack_offset@9544(出现两次:一次 `<field_exists>` 占位 imm=1,一次
`<byte_off>` imm=9544)——成员序/偏移与 03 README 的「本机实况」数值一致。链首的
「根下标 0」是 libbpf 语义里的根实体下标(指针访问即解引用一次,见
`bpf_core_parse_spec`)。

## 失败模式(退出码)

| 退出码 | 含义 |
|---|---|
| 0 | 五站全过,末尾 `RELOS=<n>`(n>0) |
| 1 | 切片失败(打印 FAIL 行):输入文件不存在/不可读;非 64 位小端 ELF;无 `.BTF.ext`;有 `.BTF.ext` 却无主 `.BTF`;头非法(magic/版本/截断);头无 core_relo 字段或记录数=0;`rec_size≠16`;`num_info=0`;kind 越界([0,11] 口径);字符串偏移越界 |
| 2 | 用法错误(参数个数不对,打印用法) |

降级(不改变退出码):llvm-objdump 缺失 → 站 1 仅自解析段表(`WARN`);同目录
`core.bpf.c` 缺失 → 站 5 跳过源码对照(`WARN`)。

## 快速验证(本章标准四连)

```bash
python3 -m py_compile slice-relos.py
python3 slice-relos.py ../03-core-read/core.bpf.o   # exit 0,RELOS=4,五站齐全
python3 slice-relos.py /nonexistent.o               # exit 1
echo 'int x;' | gcc -c -x c - -o /tmp/plain.o && python3 slice-relos.py /tmp/plain.o  # exit 1(无 .BTF.ext)
```

## 与章内其他资产的关系

- 上游:`../03-core-read/` 的 `core.bpf.o`(本切片器的标准输入;那边 README 的
  重定位注解在本站逐字节兑现);
- 同轨:`05-kernel-relo/` 的 bash 切片器切**内核源码**一侧(任务矩阵的另一半),
  本目录切**自编对象**一侧,两轨同一走读口径;
- 章正文:「三、CO-RE 编译期」讲记录怎么来,「四、两台重定位引擎」讲记录怎么被
  消费——本切片器是两节之间的实证底座。

# 练习① 重定位记录判读:从 demo.bpf.o 反推「补丁什么、怎么补」

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己切一刀、把判读表填完,再看 [`check-relo-kind.sh`](check-relo-kind.sh) 的断言口径与后部「参考思路」。

## 任务

先 `make` 出 demo.bpf.o([`relo-demo.bpf.c`](relo-demo.bpf.c) 是预置源码,刻意含多种 CO-RE 重定位记录),再用 04 切片器切它:

```bash
make
python3 ../../04-relo-slicer/slice-relos.py demo.bpf.o
```

对切片器站 3/站 4 给出的**每一条记录**,写一张判读表,回答两问——**补丁什么、怎么补**:

1. **补丁什么**:记录四字段(insn_off/type_id/access_str_off/kind)各是什么?kind 编号对应 uapi 的哪个枚举名?访问链(`0:2` 一类)解码到哪个类型哪个成员?
2. **怎么补**:加载期 CO-RE 引擎按这个 kind 会把目标指令的**哪个字段**(imm 还是 offset?)改写成**什么量**?占位立即数是多少、为什么编译期填不出真值?

## 提示

- 先跑完切片器**五站**再判读(站 3 逐条记录、站 4 kind 对照、站 5 访问链解码各领一职);kind 的权威对照口径是 **13 种(uapi bpf.h:7262-7276,v6.6.78,枚举名与行注逐字对照)**——切片器站 4 的断言口径 [0,11] 是本章从严取的子集,枚举名表里没有的 12 号(TYPE_MATCHES)另见枚举块注;
- 「怎么补」的指令类依据:补 imm 还是 offset 由指令类决定(正文 3.6 末引 llvm_reloc.rst);各 kind 的语义值:kind 0 补目标内核的字节偏移、kind 2 补 1/0(补什么值编译期无从知道)、kind 9 补目标内核里该类型的字节数;
- 源码里 `BPF_NO_PRESERVE_ACCESS_INDEX` 是刻意的:关掉 vmlinux.h 的「批发」通道(正文 3.2),让每条记录都能对到一行显式宏族调用——判读时每条记录都该能指出源码行;
- 站 5 的源码对照素材名写死为 `core.bpf.c`,本目录没有该文件,切片器会打一行 `WARN 源码对照素材不在场` 并跳过源码对照(既定降级,不影响退出码)——源码对照自己做,正好是练习的一部分;
- `bash check-relo-kind.sh` 的判定不替你写判读表:它只断言「切片器跑得通、记录够判、三种目标 kind 都在场」,判读质量自己把关。

## 验收

```bash
bash check-relo-kind.sh
# 预期:TOTAL=8 PASS=8 FAIL=0,exit 0
# (python3 依赖/切片器在场/对象在场/切片器退出码 0/记录数 ≥3/kind 0·2·9 三名逐个在场)
# 负路径(实测):OBJ=/nonexistent/demo.bpf.o 或 SLICER=/nonexistent/slice-relos.py
# 重跑 → FAIL≠0 → exit 1
```

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己切一刀、把判读表填完</summary>

- **make 两步**:vmlinux.h(bpftool btf dump format c)→ demo.bpf.o(`clang -g -O2 -target bpf`;`-g` 是命门,漏了连 `.BTF.ext` 都没有,切片器站 1 即 FAIL)。本对象用 `BPF_NO_PRESERVE_ACCESS_INDEX` 关掉批发通道后,`.BTF.ext` 的 core_relo 小节只装 5 条记录,每条都对应下方源码行。
- **判读表**(clang 18 实测,insn_off/type_id/偏移数值随工具链与内核微差,kind 集合不变):

| # | insn_off | type_id | kind(uapi 名) | 访问链 | 源码行 | 怎么补 |
|---|---|---|---|---|---|---|
| 0 | 8 | 5(demo_config) | 0 BPF_CORE_FIELD_BYTE_OFFSET | 0:0(mode) | relo-demo.bpf.c:67 `BPF_CORE_READ(cfg, mode)` | 目标内核下 mode 的字节偏移 → 改写 imm(占位 imm=0) |
| 1 | 72 | 5 | 2 BPF_CORE_FIELD_EXISTS | 0:2(extra_mode) | :71 `bpf_core_field_exists(cfg->extra_mode)` | 1/0 → 写 imm(占位 imm=1;补什么值编译期无从知道) |
| 2 | 88 | 5 | 0 BPF_CORE_FIELD_BYTE_OFFSET | 0:2(extra_mode) | :73 `BPF_CORE_READ(cfg, extra_mode)` | 目标字节偏移 → 改写 imm(本机 8) |
| 3 | 152 | 6(demo_event) | 9 BPF_CORE_TYPE_SIZE | 0(类型类恒 "0") | :78 `bpf_core_type_size(struct demo_event)` | 目标内核该类型字节数 → 改写 imm |
| 4 | 168 | 5 | 0 BPF_CORE_FIELD_BYTE_OFFSET | 0:1(flags) | :80 `BPF_CORE_READ(cfg, flags)` | 目标字节偏移 → 改写 imm(本机 4) |

- **kind 9 的口径纠偏**:题目口径「类型 ID」若答 TYPE_ID_LOCAL(6)不算错方向,但 uapi 实测 kind 9 = TYPE_SIZE(:7272)、kind 6 才是 TYPE_ID_LOCAL(:7269)——本对象用公开宏 `bpf_core_type_size` 产 kind 9;TYPE_ID_LOCAL/TYPE_ID_TARGET 没有公开宏,属引擎内部使用,知道语义即可。
- **批零对照**:若去掉 `BPF_NO_PRESERVE_ACCESS_INDEX`,vmlinux.h 文件头 pragma 会让每处直接字段访问都自动配记录(「批发」);关掉后只有宏族(「零售」)产记录——两条来路的机制见正文 3.2/3.4。
- **为什么不加载**:类型全自编,不在任何目标内核 BTF 里,真加载会「候选列表为空」——EXISTS 类补 0、其余指令被 poison(正文 4.2 的失败语义),程序加载失败并指到指令号。这是**预期失败**,也正是 CO-RE 边界的一部分:重定位服务于「目标内核也有的类型」,自编类型本就不在该协议的射程内(运行级待验证,本机无 root;本练习也只到切片为止)。
- **自查三件事**:`make` 零告警;切片器五站全 PASS、`RELOS=5`;判读表每行的 kind 名与 uapi bpf.h:7262-7276 逐字一致。

</details>

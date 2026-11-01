# 02-jit-x86:x86_64 JIT 六站切片器

沿「选通入口 → do_jit 两趟 → emit_prologue 与寄存器映射 → lowering 逐例 → helper/tail call → 致盲与映像分配」的编译路径,把走读树里 x86_64 JIT 的六段关键代码按「站」切出来。正文《二、x86_64 JIT:从字节码到机器码的六站》逐站引用本输出的行号与代码段——**本输出的行号即引用事实**。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。**只读源码树,不构建、不加载**——脚本只做 `grep -n` 定位与 `sed -n 'A,Bp'` 区间提取,对源码树零写入。
- 站点地图:站 1 core.c 选通 + x86 入口;站 2 do_jit 与两趟;站 3 寄存器映射与序言;站 4 switch 分派四例;站 5 helper 地址解析与两种尾调用;站 6 常量致盲与 bpf_prog_pack。

## 用法

```bash
# 默认走读树
bash trace-jit-x86.sh
# 指定走读树(位置参数优先;也兼容环境变量 WALK_TREE)
bash trace-jit-x86.sh /path/to/linux-6.6.78
```

## 预期输出示例(本机实跑摘录,首尾各 10 行)

```
# trace-jit-x86.sh —— x86_64 JIT 六站切片(第 6 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(core.c/bpf_jit_comp.c),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:选通与入口(解释器→JIT)
### 定位(grep -n 实测;v6.6.78 校准)
2357:struct bpf_prog *bpf_prog_select_runtime(struct bpf_prog *fp, int *err)
2384:		fp = bpf_int_jit_compile(fp);
2754:struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
```

```
	list_for_each_entry(pack, &pack_list, list) {
		pos = bitmap_find_next_zero_area(pack->bitmap, BPF_PROG_CHUNK_COUNT, 0,
						 nbits, 0);
		if (pos < BPF_PROG_CHUNK_COUNT)
			goto found_free_area;
职责:致盲把用户可控常量拆成随机异或对(缩跳板攻击面,与 bpf_jit_harden 联动);pack 把多个程序的映像集中成只读大页。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=6
```

完整实跑为六站、exit 0、无 `STATION n: NOT FOUND`。每站结构固定:`## 站 N:站名` → `### 定位`(grep -n 实测)→ 若干 `### 代码段`(标明 sed 区间命令与一行中文说明)→ `职责:` 一行注。行号用「锚点 + 偏移」两步法:树小幅漂移时区间随锚点自动平移。

## 失败模式

| 现象 | 原因 | 处置 |
|---|---|---|
| `走读树目录不存在:...` | WALK_TREE 指错目录或源码未解压 | 指向正确的 6.6.78 树重跑(退出码 1) |
| `树内关键文件缺失:...` | 树版本不符或目录不完整 | 核对树版本;关键文件为 core.c 与 bpf_jit_comp.c |
| `STATION n: NOT FOUND` | 锚点漂移(内核改名/移动/内联) | 回树 grep 新位置,改脚本该站锚点与偏移(同步更新校准注释)后重跑 |
| stderr 出现 `WARN: 树版本 ... 不符` | 换了树版本 | 行号仅供参考,正文引用以 v6.6.78 实测为准 |

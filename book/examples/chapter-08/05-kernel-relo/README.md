# 05-kernel-relo:内核态重定位引擎五站切片器

04 号示例([`../04-relo-slicer/`](../04-relo-slicer/))从编译产物里切出「重定位记录」(relocation record:记下哪条指令的立即数要按目标内核改写、记的是哪个类型哪个字段、记什么量);本切片器补上另一半——这些记录进入内核后由谁接手、按什么规则改写指令。沿「引擎入口 → 候选查找与兼容判定 → verifier 接线 → UAPI 载体 → 轻骨架提交」的路径,把走读树里内核态 CO-RE 引擎的五段关键代码按「站」切出来,供正文《四、两台重定位引擎》与《五、实战五层:一条 BPF_CORE_READ 的完整旅程》逐站引用其行号与代码段——**本输出的行号即引用事实**。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。**只读源码树,不构建、不加载**——脚本只做 `grep -n` 定位与 `sed -n 'A,Bp'` 区间提取,对源码树零写入。
- 站点地图:站 1 `bpf_core_apply` 引擎总入口(查候选→算结果→改指令);站 2 `bpf_core_find_cands` 候选查找 + `bpf_core_types_are_compat` 兼容判定(递归深度上限 2,btf.c:8073);站 3 `check_core_relo` verifier 接线(逐条拷贝、逐条改写)与两个调用点(:15778/:15817);站 4 `union bpf_attr` 的 `core_relo_cnt`/`core_relos`/`core_relo_rec_size` 三字段;站 5 `bpf_gen__prog_load` 轻骨架把记录塞进同一条 UAPI 通道。
- 与 libbpf 引擎的关系:两台引擎逻辑同源、各自维护,接口对齐 uapi(libbpf 侧由 03/04 的资产走读,不在本切片器范围)。

## 用法

```bash
bash trace-kernel-relo.sh                              # 默认走读树
bash trace-kernel-relo.sh /path/to/linux-6.6.78        # 指定走读树(位置参数优先)
WALK_TREE=/path/to/tree bash trace-kernel-relo.sh      # 环境变量形式(位置参数优先于它)
```

## 预期输出示例(本机实跑摘录,首尾各约 10 行)

```
# trace-kernel-relo.sh —— 内核态重定位引擎五站切片(第 8 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(btf.c/verifier.c/bpf.h/gen_loader.c),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:内核引擎入口 bpf_core_apply
### 定位(grep -n 实测;v6.6.78 校准)
8423:int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,
8453:		cc = bpf_core_find_cands(ctx, relo->type_id);
8482:	err = bpf_core_calc_relo_insn((void *)ctx->log, relo, relo_idx, ctx->btf, &cands, specs,
```

```
	libbpf_strlcpy(attr.prog_name, prog_name, sizeof(attr.prog_name));
	prog_load_attr = add_data(gen, &attr, attr_size);
### 代码段:sed -n '989,990p' tools/lib/bpf/gen_loader.c —— attr 里存的不是内核指针而是 blob 内偏移:emit_rel_store 往生成的加载器程序里编入存储指令,运行时把偏移写进 prog_load_attr
	/* populate union bpf_attr with a pointer to core_relos */
	emit_rel_store(gen, attr_field(prog_load_attr, core_relos), core_relos);
职责:轻骨架(light skeleton)的同款提交——gen_loader 生成的加载器程序在运行时自己组装 BPF_PROG_LOAD 的 attr,CO-RE 记录走同一条 UAPI 通道(站 4);记录先经 bpf_gen__record_relo_core(:851)逐条登记。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=5
```

完整实跑为五站、exit 0、无 `STATION n: NOT FOUND`。输出结构与失败模式同第 7 章同款接口的切片器 [`../../chapter-07/04-helper-dispatch/`](../../chapter-07/04-helper-dispatch/)(六站),不再重复。

## 失败模式

| 现象 | 处置 |
|---|---|
| 树不在:`WALK_TREE=/nonexistent` 实跑 → stderr「走读树目录不存在:/nonexistent」,exit 1 | 把 WALK_TREE 指向正确的 6.6.78 树后重跑 |
| 锚点漂移:该站打印 `STATION n: NOT FOUND`,整体 exit 1(其余站继续) | 回树重新 grep 校准该站锚点模式与偏移(同步更新脚本内校准注释)后重跑 |
| 版本不符:换树后 stderr 打 WARN,继续切片 | 行号漂移属预期;正文引用一律以 v6.6.78 实测为准 |

## 与示例的对读

- [`../04-relo-slicer/`](../04-relo-slicer/) 切出记录本身(编译期),本切片器切出消费记录的内核引擎(加载期):两份输出对照读——04 站 3 的一条记录四字段(insn_off/type_id/access_str_off/kind),正是本切片器站 1 入参 `struct bpf_core_relo`、站 4 UAPI 载体三字段所搬运的内容;
- [`../03-core-read/`](../03-core-read/) 的 `BPF_CORE_READ` 宏在编译期生成记录(04 的输入),加载时经本切片器站 3 接线递入、站 1 引擎改写指令——正文五节「一条 BPF_CORE_READ 的完整旅程」的内核侧两站即此。

# 04-helper-dispatch:helper 分发五站切片器

沿「基址与解释器出口 → 公共兜底白名单 → 验证期解析链 → 类型白名单实例 → 原型结构」的路径,把走读树里 helper 分发机制的五段关键代码按「站」切出来。正文《一、辅助函数:从 call 指令到白名单》逐站引用本输出的行号与代码段——**本输出的行号即引用事实**。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。**只读源码树,不构建、不加载**——脚本只做 `grep -n` 定位与 `sed -n 'A,Bp'` 区间提取,对源码树零写入。
- 站点地图:站 1 `__bpf_call_base` 基址与解释器 CALL_ARGS 出口;站 2 `bpf_base_func_proto` 公共兜底;站 3 验证期解析链(`env->ops->get_func_proto`)与失败出口;站 4 tracing 族白名单实例;站 5 `struct bpf_func_proto` 原型结构。

## 用法

```bash
bash trace-helpers.sh                              # 默认走读树
bash trace-helpers.sh /path/to/linux-6.6.78        # 指定走读树(位置参数优先)
```

## 预期输出示例(本机实跑摘录,首尾各 10 行)

```
# trace-helpers.sh —— helper 分发五站切片(第 7 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(core.c/helpers.c/verifier.c/bpf_trace.c/bpf.h),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:基址与解释器出口(__bpf_call_base)
### 定位(grep -n 实测;v6.6.78 校准)
1500:noinline u64 __bpf_call_base(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
1976:		BPF_R0 = (__bpf_call_base_args + insn->imm)(BPF_R1, BPF_R2,
2240:void bpf_patch_call_args(struct bpf_insn *insn, u32 stack_depth)
```

```
			enum bpf_arg_type arg4_type;
			enum bpf_arg_type arg5_type;
		};
		enum bpf_arg_type arg_type[5];
	};
职责:helper 的「说明书」——验证器按 arg*_type 逐参检查(第 3 章参数校验的依据),pkt_access 决定能否直接摸包,might_sleep 决定能否在原子上下文调用。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=5
```

完整实跑为五站、exit 0、无 `STATION n: NOT FOUND`。每站结构固定:`## 站 N:站名` → `### 定位`(grep -n 实测)→ 若干 `### 代码段`(sed 区间 + 一行中文说明)→ `职责:` 一行注。行号「锚点 + 偏移」两步法,树小幅漂移时区间随锚点自动平移。

## 失败模式

| 现象 | 原因 | 处置 |
|---|---|---|
| `走读树目录不存在:...` | WALK_TREE 指错目录或源码未解压 | 指向正确的 6.6.78 树重跑(退出码 1) |
| `树内关键文件缺失:...` | 树版本不符或目录不完整 | 核对树版本;五个关键文件见脚本头部 |
| `STATION n: NOT FOUND` | 锚点漂移(内核改名/移动/内联) | 回树 grep 新位置,改该站锚点与偏移(同步更新校准注释)后重跑 |
| stderr `WARN: 树版本 ... 不符` | 换了树版本 | 行号仅供参考,正文引用以 v6.6.78 实测为准 |

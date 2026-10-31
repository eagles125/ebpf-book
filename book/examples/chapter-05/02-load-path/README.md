# 02-load-path —— 加载路径切片器(trace-load-path.sh)

## 一、用途

沿 `BPF_PROG_LOAD` 的加载路径,在 eBPF 走读源码树上按「站」切出六段关键代码,每段
≤25 行,并给出 grep -n 定位行与一行中文职责注。它是第 5 章正文《二、加载路径》的
配图式产物:**正文逐站引用本脚本输出的行号与代码段**。行号基于 v6.6.78 树实测校准。

六站一览(锚点为 v6.6.78 实测值,均已在本机树复核):

| 站 | 站名 | 锚点(实测行号) |
|---|---|---|
| 1 | 系统调用入口与分发 | `SYSCALL_DEFINE3(bpf,` syscall.c:5567;`__sys_bpf` :5426;`case BPF_PROG_LOAD` :5464(第二处 :5592 在 `bpf_sys_bpf` 白名单) |
| 2 | bpf_prog_load 签名与属性处理 | `bpf_prog_load(` 定义 syscall.c:2586、分发调用 :5465 |
| 3 | 验证调用点 | `bpf_check(&prog` syscall.c:2743;定义 verifier.c:20222 |
| 4 | JIT 选择 | `bpf_prog_select_runtime` 调用点 syscall.c:2747;定义 core.c:2357(文档 :2347、EXPORT :2408);内部 `bpf_int_jit_compile` 调用 :2384、`__weak` 回退桩 :2884 |
| 5 | fd 分配 | `bpf_prog_new_fd(` 定义 syscall.c:2293、load 内调用 :2773;`prog_idr` :53;`bpf_prog_alloc_id` :2099/2751 |
| 6 | 释放链 | `__bpf_prog_put` syscall.c:2181;`bpf_prog_put` :2195;deferred :2168、noref :2147、rcu :2136;fd 关闭入口 `bpf_prog_release` :2201 |

## 二、调用方法

```bash
bash trace-load-path.sh [WALK_TREE]      # 位置参数可选,默认 /work/linux/linux-6.6.78
WALK_TREE=/path/to/tree bash trace-load-path.sh   # 也可用环境变量(位置参数优先)
```

- **只读源码树**:仅以 grep/sed 读取 `kernel/bpf/syscall.c`、`core.c`、`verifier.c`;
  **不构建、不加载**,对源码树零写入;结果全部输出到 stdout。
- 本脚本不涉及任何 bpf() 系统调用、程序加载或特权查询,因此没有需要区分
  「运行级待验证」的输出;下节「预期输出示例」为本机 6.6.78 树实跑摘录
  (机检级真实验证),非示意稿。
- 依赖命令:bash、grep、sed、awk;缺任一则启动报错退出(1)。

## 三、输出结构

每站固定四层:`## 站 N:站名` → `### 定位`(grep -n 实测输出)→ `### 代码段`
(实际执行的 `sed -n 'A,Bp'` 命令 + 切出的代码,每段 ≤25 行)→ `职责:` 一行中文注。

行号定位用**两步法**:脚本先 `grep -n` 定位锚点行,再按「锚点行 + 校准偏移」推出
sed 区间。偏移按 v6.6.78 校准并写在脚本注释里;树小幅漂移时区间随锚点自动平移,
脚本头部展示的 sed 命令始终是实际执行的那一条。

末尾固定汇总一行:`TREE_VERSION=v6.6.78 STATIONS=6`(TREE_VERSION 是书稿锁定
版本常量,非自动探测;树版本不符时脚本会在 stderr 打 WARN)。

## 四、预期输出示例(已在本机 6.6.78 树实跑)

实跑命令:`bash trace-load-path.sh`,全文 307 行,以下为首尾各 10 行的原文摘录。

开头 10 行:

```
# trace-load-path.sh —— BPF_PROG_LOAD 加载路径六站切片(第 5 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(syscall.c/core.c/verifier.c),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:系统调用入口与分发
### 定位(grep -n 实测;v6.6.78 校准)
5567:SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
5426:static int __sys_bpf(int cmd, bpfptr_t uattr, unsigned int size)
5569:	return __sys_bpf(cmd, USER_BPFPTR(uattr), size);
```

结尾 10 行:

```
	struct bpf_prog *prog = filp->private_data;

	bpf_prog_put(prog);
	return 0;
}
职责:引用计数归零后的分级释放——put → deferred → noref → RCU;fd 关闭即触发 put。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=6
```

## 五、失败模式(均已实测)

1. **树不在**:`WALK_TREE` 指错目录或源码未解压 → stderr 报
   `走读树目录不存在:...`(或`树内关键文件缺失:...`),退出码 1,不产出任何站;
   处理:把 WALK_TREE 指向正确的 6.6.78 树重跑。
2. **锚点漂移**:树在但 grep 定位为空(内核改名/移动符号)→ 对应站打印
   `STATION n: NOT FOUND`,其余站继续输出,退出码 1;
   处理:回树里 grep 新位置,改脚本该站的锚点模式与偏移(同步更新校准注释)再重跑。
   实测:同一脚本指向 4.19 树时,站 1/6 报 NOT FOUND、站 2–5 仍切出,退出码 1。
3. **版本不符**:Makefile 显示非 6.6.78 → stderr 打
   `WARN: 树版本 x.y.z 与书稿锁定版本 v6.6.78 不符,行号仅供参考` 后继续;
   处理:正文引用一律以 v6.6.78 树实测为准,换树行号漂移属预期。
4. **依赖缺失 / 参数过多**:缺 grep/sed/awk,或多于一个位置参数 → stderr 报错,
   退出码 1。

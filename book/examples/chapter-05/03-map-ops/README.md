# 03-map-ops —— 映射生命周期切片器(trace-map-ops.sh)

## 一、用途

沿映射(map)从创建到释放的完整生命周期,在 eBPF 走读源码树上按「站」切出六段关键
代码,每段 ≤25 行,并给出 grep -n 定位行与一行中文职责注。它是第 5 章正文三节
(创建与类型查找、命令与通用层、freeze/pin 与释放)的配图式产物:**正文逐站引用
本脚本输出的行号与代码段**。行号基于 v6.6.78 树实测校准。

六站一览(锚点为 v6.6.78 实测值,均已在本机树复核):

| 站 | 站名 | 锚点(实测行号) |
|---|---|---|
| 1 | 入口分发与 bpf_map_types[] 注册表 | `bpf_map_types[]` syscall.c:63;`__sys_bpf` :5426;`case BPF_MAP_CREATE` :5446(第二处 :5587 在 `bpf_sys_bpf` 白名单) |
| 2 | map_create 与类型查找 | `map_create(` 定义 syscall.c:1127;map 侧 `array_index_nospec` :1165(另一处 :2056 在 prog 侧);`sysctl_unprivileged_bpf_disabled` 判定 :1186(声明 :60) |
| 3 | 通用层与 array_map_ops 对照 | `bpf_map_update_value` :153(通用 `ops->map_update_elem` 调用 :201);`bpf_map_copy_value` 通用 `ops->map_lookup_elem` 调用 :247;`array_map_ops` arraymap.c:750 |
| 4 | 命令包装 | `__bpf_map_get` :1322;`map_lookup_elem` :1434(通用层调用 :1487,第一处 :1483 在 bloom 分支);`map_update_elem` :1509(通用层调用 :1552);`map_delete_elem` :1565(rcu 下 ops 调用 :1605) |
| 5 | freeze 与 pin | `map_freeze` :1998(`bpf_map_write_active` 判定 :2023);`bpf_map_write_active` :124;`bpf_obj_pin` :2797 |
| 6 | 释放链 | `bpf_map_release` :773;`bpf_map_put_with_uref` :767;`bpf_map_put_uref` :717;`bpf_map_put` :750;`bpf_map_free_rcu_gp` :734;`bpf_map_free_deferred` :691 |

## 二、调用方法

```bash
bash trace-map-ops.sh [WALK_TREE]      # 位置参数可选,默认 /work/linux/linux-6.6.78
WALK_TREE=/path/to/tree bash trace-map-ops.sh   # 也可用环境变量(位置参数优先)
```

- **只读源码树**:仅以 grep/sed 读取 `kernel/bpf/syscall.c`、`arraymap.c`;
  **不构建、不加载**,对源码树零写入;结果全部输出到 stdout。
- 本脚本不涉及任何 bpf() 系统调用、程序加载或特权查询,因此没有需要区分
  「运行级待验证」的输出;下文「预期输出示例」为本机 6.6.78 树实跑摘录
  (机检级真实验证),非示意稿。
- 依赖命令:bash、grep、sed、awk;缺任一则启动报错退出(1)。

## 三、输出结构

每站固定四层:`## 站 N:站名` → `### 定位`(grep -n 实测输出)→ `### 代码段`
(实际执行的 `sed -n 'A,Bp'` 命令 + 切出的代码,每段 ≤25 行)→ `职责:` 一行中文注。
全片 25 段代码合计 330 行(实测;各段 4–25 行不等,最长一段为站 6 的
`bpf_map_free_deferred` 全体)。

行号定位用**两步法**:脚本先 `grep -n` 定位锚点行,再按「锚点行 + 校准偏移」推出
sed 区间。偏移按 v6.6.78 校准并写在脚本注释里;树小幅漂移时区间随锚点自动平移,
脚本头部展示的 sed 命令始终是实际执行的那一条。

末尾固定汇总一行:`TREE_VERSION=v6.6.78 STATIONS=6`(TREE_VERSION 是书稿锁定
版本常量,非自动探测;树版本不符时脚本会在 stderr 打 WARN)。

## 四、三层对照表:命令 → 通用层 → ops 回调

正文三节引用的核心骨架:**命令包装**(站 4,创建在站 2)是每个 `cmd` 的入口,
**通用层**(站 3)按 map 类型分流,**ops 回调**才是每个具体映射的算法本体。
freeze 与 pin 是两张「空白行」——它们不经过任何 ops 回调,恰是理解内核如何用
纯标志位/引用计数收口的好例子。

| 命令(cmd) | 命令包装 | 通用层 | ops 回调(以 BPF_MAP_TYPE_ARRAY 为例) |
|---|---|---|---|
| `BPF_MAP_CREATE` | `map_create` syscall.c:1127(站 2) | —(无独立通用层,直呼 ops) | `map_alloc_check` / `map_alloc` = `array_map_alloc_check` arraymap.c:52 / `array_map_alloc` arraymap.c:83 |
| `BPF_MAP_LOOKUP_ELEM` | `map_lookup_elem` syscall.c:1434(站 4) | `bpf_map_copy_value` :210(站 3) | `map_lookup_elem` = `array_map_lookup_elem` arraymap.c:166 |
| `BPF_MAP_UPDATE_ELEM` | `map_update_elem` syscall.c:1509(站 4) | `bpf_map_update_value` :153(站 3) | `map_update_elem` = `array_map_update_elem` arraymap.c:313 |
| `BPF_MAP_DELETE_ELEM` | `map_delete_elem` syscall.c:1565(站 4) | —(rcu_read_lock 内直呼 ops) | `map_delete_elem` = `array_map_delete_elem` arraymap.c:392 |
| `BPF_MAP_FREEZE` | `map_freeze` syscall.c:1998(站 5) | —(**自包含、无 ops 回调**,只置 `map->frozen`) | — |
| `BPF_OBJ_PIN` | `bpf_obj_pin` syscall.c:2797(站 5) | — | —(fd 对象绑 bpffs 路径,与 ops 无关) |

站 6 的释放链不属于单个命令:任何引用(fd、pin、程序内引用)归零后都汇入
`bpf_map_put` → RCU → `bpf_map_free_deferred` → `ops->map_free`。

## 五、预期输出示例(已在本机 6.6.78 树实跑)

实跑命令:`bash trace-map-ops.sh`,全文 427 行,以下为首尾各 10 行的原文摘录。

开头 10 行:

```
# trace-map-ops.sh —— 映射生命周期六站切片(第 5 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(syscall.c/arraymap.c),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:入口分发与 bpf_map_types[] 注册表
### 定位(grep -n 实测;v6.6.78 校准)
63:static const struct bpf_map_ops * const bpf_map_types[] = {
5426:static int __sys_bpf(int cmd, bpfptr_t uattr, unsigned int size)
5569:	return __sys_bpf(cmd, USER_BPFPTR(uattr), size);
```

结尾 10 行:

```
	/* Delay freeing of btf for maps, as map_free callback may need
	 * struct_meta info which will be freed with btf_put().
	 */
	btf_put(btf);
}
职责:引用计数两级归零——usercnt(uref)与 refcnt;refcnt 归零先注销 id,再经 RCU 宽限期转工作队列,最终在 bpf_map_free_deferred 里 ops->map_free 释放实现私有数据。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=6
```

## 六、失败模式(均已实测)

1. **树不在**:`WALK_TREE` 指错目录或源码未解压 → stderr 报
   `走读树目录不存在:...`(或`树内关键文件缺失:...`),退出码 1,不产出任何站;
   处理:把 WALK_TREE 指向正确的 6.6.78 树重跑。
2. **锚点漂移**:树在但 grep 定位为空(内核改名/移动符号)→ 对应站打印
   `STATION n: NOT FOUND`,其余站继续输出,退出码 1;
   处理:回树里 grep 新位置,改脚本该站的锚点模式与偏移(同步更新校准注释)再重跑。
   实测:同一脚本指向 4.19.325 树时,六站全部 NOT FOUND(4.19 尚无 `__sys_bpf`、
   `bpf_map_update_value`/`bpf_map_copy_value`、`bpf_map_write_active` 等,通用层
   大量符号是 5.x 才引入的),stderr 打版本 WARN 后退出码 1。
3. **版本不符**:Makefile 显示非 6.6.78 → stderr 打
   `WARN: 树版本 x.y.z 与书稿锁定版本 v6.6.78 不符,行号仅供参考` 后继续;
   处理:正文引用一律以 v6.6.78 树实测为准,换树行号漂移属预期。
4. **依赖缺失 / 参数过多**:缺 grep/sed/awk,或多于一个位置参数 → stderr 报错,
   退出码 1。

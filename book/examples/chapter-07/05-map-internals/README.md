# 05-map-internals:映射内部六站切片器

沿「类型注册表 → ops 接口表 → per-CPU 桶锁 → 更新路径 → 删除与 RCU 回收 → memcg 内存记账」的路径,把走读树里 hash 映射内部的六段关键代码按「站」切出来。正文《二、hash 映射内部:并发与内存》与《三、per-CPU、LRU 与内存记账》逐站引用本输出的行号与代码段——**本输出的行号即引用事实**。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。**只读源码树,不构建、不加载**——脚本只做 `grep -n` 定位与 `sed -n 'A,Bp'` 区间提取,对源码树零写入。
- 站点地图:站 1 `bpf_map_types[]` 注册表(第 5 章双展开的映射侧);站 2 `struct bpf_map_ops` 接口表;站 3 `htab_lock_bucket` 两级锁(per-CPU 判重入 + 桶级 raw_spin_lock);站 4 `htab_map_update_elem` 三语义起点;站 5 删除与 `htab_elem_free` 的 RCU 时代回收;站 6 `bpf_map_save_memcg` 记账挂靠。

## 用法

```bash
bash trace-map-internals.sh                              # 默认走读树
bash trace-map-internals.sh /path/to/linux-6.6.78        # 指定走读树(位置参数优先)
```

## 预期输出示例(本机实跑摘录,首尾各 10 行)

```
# trace-map-internals.sh —— 映射内部六站切片(第 7 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(syscall.c/bpf.h/hashtab.c),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:映射类型注册表 bpf_map_types[]
### 定位(grep -n 实测;v6.6.78 校准)
63:static const struct bpf_map_ops * const bpf_map_types[] = {
```

```
}
职责:映射内存记账的 6.x 形态——全部映射内存记在创建者 memcg 名下(配额随 cgroup 走,不再用 memlock rlimit);JIT 侧另有 bpf_jit_charge/uncharge_modmem 受 bpf_jit_limit 约束(第 6 章一节的 limit)。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=6
```

完整实跑为六站、exit 0、无 `STATION n: NOT FOUND`。输出结构与失败模式同 [`../04-helper-dispatch/`](../04-helper-dispatch/)(同款接口,五站),不再重复;树不在/锚点漂移/版本不符三类处置见该 README 的失败模式表。

## 与示例的对读

- 示例 [`../01-hash-lookup-update/`](../01-hash-lookup-update/) 的 `bpf_map_update_elem(BPF_NOEXIST)` 与 lookup→update 并发窗口,落在本切片器站 4 的源码现场;
- 示例 [`../02-percpu-counter/`](../02-percpu-counter/) 的 per-CPU 语义,其 ops 在站 2 接口表的程序侧实现(percpu_array_map_ops,arraymap.c:775,正文三节引用)。

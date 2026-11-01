# answer.md:map_update_elem 调用链参考表(v6.6.78 实测行号)

用户态 `bpf_map_update_elem(map_fd, key, value, BPF_ANY)`(libbpf 包装)到 hash 实现 `htab_map_update_elem` 的完整链,共 9 跳:

| 跳 | 文件:行号 | 符号/语句 | 职责 |
|---|---|---|---|
| 1 | kernel/bpf/syscall.c:5452 | `case BPF_MAP_UPDATE_ELEM:` | bpf(2) 命令分发(`__sys_bpf` 的 switch;第二处 :5589 属 bpf_sys_bpf 白名单) |
| 2 | kernel/bpf/syscall.c:1509 | `static int map_update_elem(union bpf_attr *attr, bpfptr_t uattr)` | 业务入口:fd 找映射、`___bpf_copy_key` 拷键、拷值 |
| 3 | kernel/bpf/syscall.c:1552 | `err = bpf_map_update_value(map, f.file, key, value, attr->flags);` | 进入**按映射类型分派**的包装函数 |
| 4 | kernel/bpf/syscall.c:201 | `err = map->ops->map_update_elem(map, key, value, flags);` | **ops 直调**(RCU 读侧临界区内;队列/栈走 push_elem 分支,hash 走本跳) |
| — | include/linux/bpf.h:75 | `struct bpf_map_ops {` | 第 4 跳穿过**接口表**;第 1 类表(注册表 `bpf_map_types[]`,syscall.c:63)在建表时已定型,更新路径上不再查它 |
| 5 | kernel/bpf/hashtab.c:1098 | `static long htab_map_update_elem(struct bpf_map *map, void *key, void *value,` | hash 实现入口(未知 flags 拒绝、RCU 断言、算哈希) |
| 6 | kernel/bpf/hashtab.c:1146 | `ret = htab_lock_bucket(htab, b, hash, &flags);` | **先拿锁**(per-CPU 判重入 + 桶级自旋) |
| 7 | kernel/bpf/hashtab.c:1151 | `ret = check_flags(htab, l_old, map_flags);` | **后判语义**(锁内裁决 NOEXIST/EXIST/ANY,保证原子) |
| 8 | kernel/bpf/hashtab.c:1181 | `hlist_nulls_add_head_rcu(&l_new->hash_node, head);` | 新元素 RCU 发布;旧元素同步摘除 |
| 9 | kernel/bpf/hashtab.c:1424 | `static long htab_map_delete_elem(...)`(延伸) | 非替换式更新(EXIST)的兄弟路径,同锁序 |

**两条备注**:①第 6/7 跳的先后关系是本题关键——**先拿锁、后判语义**;②**思考题**:BPF 程序内的 `bpf_map_update_elem`(helpers.c:53 的 `BPF_CALL_4`)在第 4 跳汇合(helpers.c:58 的 `return map->ops->map_update_elem(...)`)——两条路共享 ops 之后的全部实现,分叉只在「进 ops 之前」:用户态走 syscall 分派,程序侧走白名单 + 五寄存器调用约定(一节 1.1)。

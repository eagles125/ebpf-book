# 练习① 映射操作链查证:从 bpf(2) 到 htab_map_update_elem

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己完成再看 [`answer.md`](answer.md) 与 `check-chain.sh` 的断言口径。

## 任务

用户态调 `bpf_map_update_elem(map_fd, key, value, BPF_ANY)`(libbpf 的 syscall 包装),最终落到 hash 映射的实现 `htab_map_update_elem`。请产出这条**调用链表**:每一跳给出「文件:行号 + 一句话职责」,不少于 6 跳。

要求:

1. 链条必须穿过**两张表**——类型注册表(`bpf_map_types[]`)与接口表(`struct bpf_map_ops`),说明各自在链上的作用(注册表在建表时已定型,更新路径上真正起作用的是 ops 成员);
2. 标出 RCU 读侧临界区从哪一跳开始(`rcu_read_lock`);
3. 标出三语义裁决(`check_flags`)与桶锁(`htab_lock_bucket`)的先后关系。

## 提示

- 从 syscall.c 的命令分发入手:`grep -n "case BPF_MAP_UPDATE_ELEM:" kernel/bpf/syscall.c`;
- syscall 侧 `map_update_elem`(:1509 附近)不是直接调 ops——中间还有一个**按映射类型分派**的包装函数,找到它;
- BPF 程序内的 `bpf_map_update_elem`(helpers.c 的 BPF_CALL_4)与用户态路径在**哪一跳汇合**?这是本题的思考题,答案写进链表备注。

## 验收

```bash
bash check-chain.sh
# 预期:TOTAL=6 PASS=6 FAIL=0,exit 0(6 个锚点逐个 grep -F 断言)
```

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己画完链条</summary>

- 分发:`case BPF_MAP_UPDATE_ELEM:`(syscall.c:5452)→ `map_update_elem`(syscall.c:1509,拷键/拷值);
- 类型分派的包装是 `bpf_map_update_value`(syscall.c:1552 调用):队列/栈走 push_elem、reuseport 走专用路径、其余走 `map->ops->map_update_elem`(syscall.c:201,**RCU 读侧临界区内**);
- ops 到实现:`struct bpf_map_ops`(bpf.h:75)的 `map_update_elem` 成员 → hash 侧 `htab_map_update_elem`(hashtab.c:1098);
- 锁内:`check_flags`(:1083)在 `htab_lock_bucket`(:149)**之后**裁决——先拿锁再判语义,保证裁决原子;
- 发布:`hlist_nulls_add_head_rcu`(hashtab.c:1181)——新元素 RCU 挂链、旧元素摘除,读者无感切换;
- 思考题答案:BPF 侧 helper(`bpf_map_update_elem`,helpers.c:53 的 BPF_CALL_4)在 **ops 成员这一跳**汇合——两条路共享从 ops 往下的全部实现,分叉只在「进 ops 之前」(用户态走 syscall 分派,程序侧走白名单+寄存器调用约定)。

</details>

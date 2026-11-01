# 01-hash-lookup-update:hash 映射「查找 + 更新」闭环(编译级已验证)

一个最小的 hash 映射闭环示例:XDP 在 lo 上按以太类型计数——BPF 侧首包 `bpf_map_update_elem(BPF_NOEXIST)` 建项、后续包 `bpf_map_lookup_elem` 拿槽位原子自增;用户态每秒用 `bpf_map_get_next_key` + `bpf_map_lookup_elem` 遍历打印。

- **验证状态:编译级已验证**——`make` 全链(vmlinux.h → .bpf.o → skeleton → gcc 链接)在本机零告警通过;**运行级待验证**(需 root):加载/attach/读数均为预期输出示例。
- **安全声明:构建期零内核交互(只编译);运行时 XDP 只允许挂 lo 等受控接口,严禁挂物理网卡**(风险见第 17 章安全警告)。

## 构建与运行

```bash
make                 # 编译级:四步全链,产物 hash_count
sudo ./hash_count lo # 运行级待验证:挂 lo,Ctrl-C 退出并清理
```

**预期输出示例(运行级待验证,root 档)**:

```text
已挂 XDP 到 lo,每秒读一次 proto_count(Ctrl-C 退出)…
--- hash 映射遍历(顺序无保证) ---
  IPv4       (0x0800): 42 包
  IPv6       (0x86dd): 7 包
  ARP        (0x0806): 3 包
```

(产生流量:另一终端 `ping 127.0.0.1`;ARP 项在 lo 上通常不出现,属正常。)

## 正文对应

- `bpf_map_lookup_elem` / `bpf_map_update_elem` 的内核实现:正文二节(hash 查找 `__htab_map_lookup_elem` 无锁 RCU 读、更新三语义与 per-CPU 桶锁);切片器 [`../05-map-internals/`](../05-map-internals/)。
- 用户态遍历用的 `bpf_map_get_next_key` 走 `map_get_next_key` 命令路径(kernel/bpf/syscall.c),与第 5 章三节的映射生命周期衔接。

## 教学看点

1. **NOEXIST 与 EXIST 的分工**:首包建项(BPF_NOEXIST,表满返回 -E2BIG)、后续包先 lookup 再 `__sync_fetch_and_add`——lookup 与 update 之间存在并发窗口(两 CPU 同时首包,一个建项成功、另一个 NOEXIST 失败),这是正文二节「更新三语义」的活例;追求无窗口可改用 `BPF_ANY` + 原子加。
2. **遍历顺序无保证**:hash 映射没有顺序语义,`get_next_key` 的顺序随内核哈希布局变化——与 array 映射的「下标即顺序」成对照(正文二/三节)。
3. **用户态只读**:本示例 BPF 写、用户态读,无需任何锁——映射是内核与用户态的共享数据结构,一致性语义(这里是最弱的单键原子性)由映射类型决定。

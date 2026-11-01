# 02-percpu-counter:per-CPU 计数器与用户态聚合(编译级已验证)

per-CPU 数组映射的最小示例:XDP 收包只递增**本 CPU** 的值槽(`BPF_MAP_TYPE_PERCPU_ARRAY`,零锁零争用);用户态每秒一次性读出全部 CPU 槽、逐 CPU 打印并聚合合计。

- **验证状态:编译级已验证**——`make` 全链零告警通过;**运行级待验证**(需 root):加载/attach/读数均为预期输出示例。
- **安全声明:构建期零内核交互;运行时 XDP 只允许挂 lo,严禁挂物理网卡。**

## 构建与运行

```bash
make                     # 编译级:产物 pcpu_count
sudo ./pcpu_count lo     # 运行级待验证:挂 lo,另一终端 ping 127.0.0.1 产生流量
```

**预期输出示例(运行级待验证,root 档)**:

```text
已挂 XDP 到 lo,每秒聚合一次 pkt_count(8 个 CPU 槽,Ctrl-C 退出)…
--- per-CPU 计数(读出快照) ---
  cpu0 : 37
  cpu3 : 12
  合计: 49 包
```

(未收流量的 CPU 槽为 0 不打印;合计是「读出时刻的近似快照」。)

## 正文对应

- per-CPU 语义与聚合:正文三节;内核侧 `percpu_array_map_ops`(`kernel/bpf/arraymap.c:775`)见切片器 [`../05-map-internals/`](../05-map-internals/) 站 2 的 ops 接口表。
- 与示例 01 的对照:hash 是「共享计数、锁护写」,per-CPU 是「分身计数、无锁」——代价是把聚合转嫁给用户态(正文三节教师视角)。

## 教学看点

1. **零锁的代价模型**:BPF 侧不再有桶锁(对比示例 01),高频路径只剩一次本 CPU 内存访问;代价是 `ncpu × value_size` 的内存与用户态聚合步骤。
2. **读取是快照不是闸门**:`bpf_map_lookup_elem` 对 per-CPU 映射一次拷回全部 CPU 槽,但逐槽拷贝之间计数仍在变——合计数在多核下是近似值,这正是「per-CPU 交换一致性换性能」的语义边界。
3. **key 仍是数组下标**:per-CPU 改变的只是 value 的布局(每 CPU 一份),寻址方式与普通 array 相同(下标即顺序)。

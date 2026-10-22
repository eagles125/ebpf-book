# 01-map-null：未检查的映射查找（map_value_or_null 直接解引用）

失败画廊 card ①——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.1 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示最常见的一类拒绝：映射（map）查找辅助函数（helper）`bpf_map_lookup_elem`
的返回值可能是 NULL，不判空就直接 `*v += 1`——加载期预期被拒；
补一个 `if (!v)` 判空分支即修复。

## bad 与 fixed 差异

仅一处语义差异：fixed 在解引用前插入了 NULL 分支，其余（stats 映射定义、SEC、
license、返回动作）完全一致；函数名按画廊约定固定为 `probe_bad` / `probe_fixed`。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：只差一个 if (!v)
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `__u64 *v = bpf_map_lookup_elem(&stats, &key);` 之后直接 `*v += 1` | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | 解引用前插入 `if (!v) return XDP_PASS;` | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
invalid mem access 'map_value_or_null'
```

关键行指认「对处于 map_value_or_null 状态的寄存器做了不允许的内存访问」；
日志前文会列出对应的指令与寄存器状态（格式随版本略有差异）。

## 为什么拒绝

`bpf_map_lookup_elem` 的返回值天然可能为 NULL：key 不在映射里（hash 映射）、
key 越界（array 映射，本例 `max_entries=1` 之外）、映射内存紧张等都查不到。
因此它的返回值寄存器类型是 `map_value_or_null`——「映射值指针 或 NULL」两态合一。

验证器对每条内存访问指令都要证明安全：解引用 `*v` 时，若寄存器仍处于两态合一状态，
就存在「NULL 解引用」的路径——内核态空指针访问会直接崩溃，违反「已验证的程序不可能
让内核崩溃」的安全承诺，所以必须在加载期拒绝，而不是等运行时出错。这正是第一章
「安全模型」的落点：验证器按所有可能路径的最坏情况证明，不按「key 大概率存在」的
经验放行。

对照：通过路径锚点 [`../../01-pkt-parse/`](../../01-pkt-parse/) 中
`bpf_map_lookup_elem` 之后的 `if (count)` 防御式判空，与本对 fixed 是同一个模式。

## 修复原则

- **lookup 之后、解引用之前，先做 NULL 分支**：`if (!v) return ...;` 或用
  `if (v) { ... }` 包住使用点。判空后验证器把后续路径上的寄存器类型从
  `map_value_or_null` 定型为 `map_value`，解引用即可证明安全。
- NULL 分支的返回动作按业务语义选择（放行 / 丢弃 / 计入错误计数均可）——验证器只要求
  「NULL 不被解引用」，不规定 NULL 时程序做什么。
- 同型错误：`bpf_ringbuf_reserve`、`bpf_map_lookup_percpu_elem` 等返回值也可能为
  NULL，遵循同一判空原则。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（verifier 拒绝信息与
  `map_value_or_null` 相关措辞）。
- `bpf_helpers.h`（libbpf）：`bpf_map_lookup_elem` 声明与映射定义宏。

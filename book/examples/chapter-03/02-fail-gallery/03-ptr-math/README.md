# 03-ptr-math：无效指针运算（对报文尾界 pkt_end 做算术）

失败画廊 card ③——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.3 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示指针类型层面的拒绝：对报文尾界指针 `data_end`（pkt_end）做算术
`data_end += 4`——加载期预期被拒；把算术移到 packet 指针上并立刻用界检查收口即修复。

## bad 与 fixed 差异

仅一处语义差异：bad 把 `+4` 加在尾界 `data_end` 上且无任何收口；fixed 把同一偏移
运算放在 packet 指针 `data` 上、并立刻与 `data_end` 比较收口。SEC、license、返回
动作完全一致；函数名按画廊约定固定为 `probe_bad` / `probe_fixed`。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：算术对象与收口方式这一处差异
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `data_end += 4;`——对 pkt_end 指针做算术 | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | `if (data + 4 > data_end)`——算术在 packet 指针上，立即界检查 | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
R2 pointer arithmetic on pkt_end PROHIBITED
```

关键行指认「对 pkt_end（报文尾界）指针做算术被禁止」——这是少数直接在寄存器类型
层面（而非内存访问层面）给出的拒绝，日志会带上肇事指令 `data_end += 4` 的位置。

## 为什么拒绝

`ctx->data_end` 是报文尾界：验证器（verifier）对报文内存安全的全部证明——
「读偏移落在 `[data, data_end)` 内」——都建立在尾界**可信**的前提上。尾界寄存器
的类型是 `PTR_TO_PACKET_END`，验证器把它登记为不可参与算术：一旦允许 `data_end += 4`，
程序就能挪动界标，把越界访问伪造成「界内」，整个报文安全证明的地基即被抽掉。
所以这类错误在寄存器类型层面就被拦下，属于「结构性不可证明」——不是补一个检查
能豁免的，内核直接禁止。

对照：packet 指针（`ctx->data` 一侧，类型 `PTR_TO_PACKET`）**允许**算术，但算术
结果的后续访问必须仍受界检查约束——这正是 bad 与 fixed 的分界：错不在「想做偏移
运算」，而在「对界标本身做运算且不收口」。

## 修复原则

- **算术放在 packet 指针上，不要碰尾界**：需要「越过若干字节」时写
  `if (data + N > data_end)`——一条语句同时完成「做算术」与「证安全」，`data_end`
  全程只读、只作比较。
- **算术之后立即收口**：packet 指针算术合法的前提是后续访问仍被界检查约束；
  推进指针而不检查，就会落回画廊 02-pkt-bounds 的拒绝。
- 尾界与头指针的角色分工是固定模式：`data` 可推进（但每次推进都要重新界检查），
  `data_end` 只作比较基准——记住这一分工即可避免整类错误。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（寄存器类型与
  pointer arithmetic 相关措辞）。
- `include/uapi/linux/bpf.h`：`xdp_md` 的 `data` / `data_end` 字段语义。

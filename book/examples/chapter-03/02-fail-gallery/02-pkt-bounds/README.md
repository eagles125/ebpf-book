# 02-pkt-bounds：报文边界检查缺失（无 data_end 检查就读协议头）

失败画廊 card ②——《第 3 章 验证器与安全保障》「三、失败画廊：七类典型拒绝」3.2 节的
走读对象。本目录为 compile-only：`make` 只产出 `bad.bpf.o` / `fixed.bpf.o`，不生成
skeleton、不加载——bad 与 fixed 在 clang 层面都编译通过，差异只在加载期被
验证器（verifier）看到。

## 任务一句话

展示报文解析类程序最经典的拒绝：推进 eth→ipv4 两层指针后没做任何
`data_end` 边界检查就读字段——首个越界读在 `eth->h_proto`，加载期预期被拒；
每层补一个 `(void *)(指针 + 1) > data_end` 检查即修复。

## bad 与 fixed 差异

仅一处语义差异：fixed 插入了两层界检查（以太网头一层、IPv4 头一层），IPv4 头指针
声明随之移到第一层检查之后，其余（判定条件、`XDP_DROP` 动作、SEC、license）完全
一致；函数名按画廊约定固定为 `probe_bad` / `probe_fixed`。

```bash
diff bad.bpf.c fixed.bpf.c   # 亲眼确认：只差两层 (void *)(指针 + 1) > data_end
```

| 文件 | 关键形态 | 结果 |
|---|---|---|
| `bad.bpf.c` | `struct ethhdr *eth = data;` 推进后无任何检查就读 `eth->h_proto` | 编译通过；加载期预期被拒 |
| `fixed.bpf.c` | 每层解引用前先 `(void *)(指针 + 1) > data_end` 检查 | 编译通过；加载期预期放行 |

## 预期 verifier 日志关键行

（预期输出示例 / 运行级待验证；日志行随内核版本略有差异，以 5.10+ 稳定措辞为基准）

```text
R2 offset is outside of the packet
```

关键行指认「寄存器 R2 的偏移已超出报文范围」（或等价措辞
`invalid packet access`）——指认点就在第一个无检查的字段读 `eth->h_proto` 上。

## 为什么拒绝

packet 指针与普通内存指针不同：报文长度运行时才确定，且随时可能被硬件或上游截断
——最小以太网帧都可能不完整。因此验证器（verifier）要求**每个**字段的读偏移都必须
先与 `data_end` 界标比较、证明落在 `[data, data_end)` 区间内，才能放行解引用；
检查一旦通过，指针的「报文内」状态会沿着代码路径传播，后续读才被允许。

bad 程序从 `data` 推进到 `eth`、再到 `ip`，全程没有一次与 `data_end` 的比较：
第一个字段读 `eth->h_proto`（偏移 12..13）就处于不可证明状态——若报文实际短于
14 字节，这次读会越过报文缓冲区，读到缓冲区外的内核内存（信息泄露）。验证器按
最坏情况证明，所以在加载期直接拒绝，而不是赌运行时报文够长。

对照：通过路径锚点 [`../../01-pkt-parse/`](../../01-pkt-parse/) 就是本对 bad 的
修复完成态——同一解析链，每层解引用前先界检查即放行（正文 3.2 card 的双向对照）。

## 修复原则

- **每层协议头解引用前，先做整层界检查**：`(void *)(指针 + 1) > data_end` 用
  「下一层头尾」证明整个当前层落在报文内；越界时返回 `XDP_PASS`（截断报文交协议栈）
  或 `XDP_DROP`，按业务语义选择。
- **推进模式：上一层尾 = 这一层头**——IPv4 头指针取 `data + sizeof(*eth)`，
  之后仍需自己的界检查；上一层检查通过不代表下一层安全。
- 检查形式固定为「指针推进后的立即数比较」，不要把检查放到使用点之后或分支外
  ——验证器沿路径传播状态，检查必须先于解引用、且在通往解引用的每条路径上都成立。

## 验证状态

- 编译级：bad 与 fixed 均已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过——编译级已通过（预期如此——拒绝发生在加载期）。
- 加载级：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  加载级『预期被拒 / 预期放行』待特权环境验证，上方日志关键行为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/verifier.rst`（packet 指针与
  `data_end` 边界检查的 verifier 措辞）。
- `bpf_endian.h`（libbpf）：`bpf_htons` 字节序转换宏。

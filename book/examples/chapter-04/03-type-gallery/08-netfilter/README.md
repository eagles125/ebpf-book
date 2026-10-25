# 08-netfilter：6.6+ 的新程序类型——只计数不拦截（类型画廊⑧）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.14 节
（lsm+netfilter 合并 card）的 netfilter 半。netfilter 是内核 6.6+ 的新程序类型
（编写环境内核 7.0 满足），把 BPF 插进 netfilter 钩子点——防火墙规则从此可以用
BPF 写。完整可运行示例（Go 控制面 + 规则表）见第 9 章 `09-netfilter-go/`，
本目录是最小类型实物。

## 任务一句话

写一个 `SEC("netfilter")` 程序：对每个过钩子点的报文计数 +1，恒返回 `NF_ACCEPT`
（放行）——只计数不拦截的教学示例。

## 代码走读

- **返回值语义**：`NF_ACCEPT` / `NF_DROP`——uapi `netfilter.h` 的 #define，
  **不在 BTF**（编写环境冒烟结论④），所以程序自带 `#define NF_ACCEPT 1`：
  vmlinux.h 拿不到 uapi 宏的实证教学点（BTF 只含类型信息，宏不进场）。
- **ctx 形状**：`struct bpf_nf_ctx`——netfilter 钩子点的报文与状态；本机（内核
  7.0）BTF 实证成员为 `state`（钩子状态）与 `skb`（当前报文），同第 9 章
  `09-netfilter-go/` 口径（其 README 即按 `ctx->skb`/`ctx->state` 讲解）。本程序
  头注沿计划口径写作 `{ state, in, out }`，与编写环境 BTF 不符——以
  `bpftool btf dump` 复核为准（章级 README「常见失败点」同款口径）。
- **map 选型**：单 key ARRAY（`nf_seen`）——报文计数；`__sync_fetch_and_add`
  原子递增。
- **失败分支必检**：`if (!v) return NF_ACCEPT`——lookup 检查不可省（第 3 章
  画廊 card ①）；此处失败仍放行，与「只计数不拦截」职责一致。
- **深入示例交叉引用**：真实防火墙要读报文（`bpf_skb_load_bytes_relative`）、
  查规则表（map）——完整可运行示例（Go 控制面 + 规则表）见第 9 章
  `09-netfilter-go/`。

## 挂载方式与前提

- **预期加载前提**：root/特权档；内核 6.6+（本机 7.0 满足）。无 cgroup/内核配置
  额外前提（对照画廊⑦的 LSM 三前提，netfilter 前提最轻）。
- **挂载形态**：经 `bpf_link` 显式挂到具体钩子点——libbpf
  `bpf_program__attach_netfilter(prog, opts)`（opts 指定协议族 `pf`、hook 号
  `hooknum`、优先级 `priority`；编写环境 libbpf 头文件实证该 API 在场），pf/
  hooknum 取值同传统 netfilter（`include/uapi/linux/netfilter.h`）。与画廊⑦
  「load 即生效」不同，netfilter 必须显式选钩子点。
- **失败模式回指**：map 异常仍 `NF_ACCEPT`（fail-open，见代码走读）——拦截类
  改造（返回 `NF_DROP`）需自行权衡失败语义（对照画廊⑥ fail-closed）。

## 预期输出示例

（预期输出示例 / 运行级待验证）挂到某钩子点后，制造过点流量：

```text
$ sudo bpftool map dump name nf_seen
key: 00 00 00 00  value: 88 00 00 00 00 00 00 00   # 过钩子点的报文数
Found 1 element
```

计数随「该钩子点可见的报文」增长；本程序恒放行，报文照常走协议栈。具体数值随
负载，以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `netfilter.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/`（netfilter 程序类型与 attach
  语义）。
- 内核源码 `include/uapi/linux/netfilter.h`：`NF_ACCEPT`/`NF_DROP` 的 #define
  （不在 BTF——冒烟结论④的出处）。
- 内核源码 `net/netfilter/nf_bpf_link.c`：netfilter 程序类型的执行与 attach 路径。
- 第 9 章 `09-netfilter-go/`：完整可运行示例（Go 控制面 + 规则表）。
- 第 3 章失败画廊 card ①：`book/examples/chapter-03/02-fail-gallery/01-map-null/`
  （本程序 `if (!v)` 分支的出处）。

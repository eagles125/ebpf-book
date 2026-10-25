# 04-socket-filter：最古老的 BPF 用途——返回值是「拷几字节」而不是 verdict（类型画廊④）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.11 节
（socket filter card）的配套最小程序。socket filter 是 tcpdump 的载体：把程序挂到某个
socket 上，内核把过路报文先喂给它。ctx 与 cgroup/skb 同为 `struct __sk_buff`，
返回值语义却完全不同——**返回值 = 接受拷贝给该 socket 的字节数**（0 = 这个包对该
socket 不可见）。画廊③④是一对「同 ctx、不同返回语义」的对照组。

## 任务一句话

写一个 `SEC("socket")` 程序：每见一个报文计数 +1，然后恒返回 0（不拷贝给观测 socket）——
「命中观测」与「投递给观察者」是两件事的教学示例。

## 代码走读

- **返回值语义**：不是 verdict！返回值是接受拷贝的字节数（0=该 socket 不见此包）；
  报文本身照常投递协议栈——只读观测、不干预协议栈，与 cgroup/skb 的「丢弃」语义
  本质不同。本示例恒 0：命中计数进 map，但报文内容不拷给观测 socket。
- **ctx 形状**：`int sf(struct __sk_buff *skb)`——与 TC/cgroup-skb 同形；只读字段
  （`len`、`protocol` 等）可用，想做「只拷某类报文」的过滤改造从读 ctx 字段入手。
- **map 选型**：单 key ARRAY（`sf_seen`）——命中计数；`__sync_fetch_and_add` 原子递增。
- **失败分支必检**：`if (!v) return 0`——lookup 检查不可省（第 3 章画廊 card ①）；
  此处失败语义恰好等价「不拷贝」，无副作用。
- **能力边界回指**：socket filter 不能丢包、不能改包——「想在 socket 维度干预」要走
  sockops/sk_msg（画廊⑤⑥），对照讲解见正文 3.12/3.13。

## 挂载方式与前提

- **预期加载前提**：root + `SO_ATTACH_BPF` 到某个 socket（attach 对象是已打开的
  socket fd——tcpdump/libpcap 走 raw socket 路径；加载级需特权）。
- **挂载形态**：经典路径 `setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd,
  sizeof(prog_fd))`，卸载用 `SO_DETACH_BPF`；这是 socket filter 特有的 setsockopt
  挂载（无 bpf_link 对应物）。挂上后该 socket 收到的每个报文都先过一遍本程序。
- **观测方式**：本程序恒返回 0，观测 socket 实际收不到报文——教学目的是对照
  「返回值=拷贝字节数」语义；想看到报文，把返回值改成 `skb->len` 即可。

## 预期输出示例

（预期输出示例 / 运行级待验证）挂到某 socket 后，制造任意过路流量：

```text
$ sudo bpftool map dump name sf_seen
key: 00 00 00 00  value: 40 00 00 00 00 00 00 00   # 程序见过的报文数（64）
Found 1 element
```

计数随「该 socket 可见的报文」增长，与实际投递无关（恒 0=不投递）；具体数值随负载，
以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `socket_filter.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/networking/filter.rst`（classic/extended
  BPF 与 socket filter 沿革）。
- 内核源码 `include/uapi/linux/bpf.h`（`SO_ATTACH_BPF`/`SO_DETACH_BPF` 注释）；
  `net/core/filter.c`（socket filter 的执行路径与返回值语义）。
- tcpdump/libpcap 项目（`SO_ATTACH_BPF` 前身 `SO_ATTACH_FILTER` 的经典用户）。

# 06-sk-msg：sockmap 上的报文重定向判决——`__SK_PASS` / `__SK_DROP`（类型画廊⑥）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.13 节
（sk_msg card）的配套最小程序。sk_msg 是 sockmap 体系的「判决端」：sockops（画廊⑤）
负责把 socket 收进 sockmap，报文经 sockmap 转发时由 sk_msg 程序给出 verdict——
本目录只搭判决端这一块，sockmap 完整体系（map 搭建、socket 收纳、真实重定向）
深入见第 22/23 章交叉引用。

## 任务一句话

写一个 `SEC("sk_msg")` 程序：对每个到达判决点的报文计数 +1，恒返回 `__SK_PASS`
（放行）——真实重定向走 `bpf_msg_redirect_hash`，本示例只演示返回值语义。

## 代码走读

- **返回值语义**：`__SK_PASS` / `__SK_DROP`——uapi `enum sk_action` 的成员，
  **名字带下划线前缀**（在 BTF——编写环境冒烟实测「常量在场」）；正常路径恒
  `__SK_PASS`，map 查找失败回 `__SK_DROP`。
- **判决类失败即丢弃**：`if (!v) return __SK_DROP`——lookup 检查本身是第 3 章
  画廊 card ① 的正面写法；与观测类示例（画廊③④失败时不干预）对照：失败分支
  返回什么跟着程序职责走——判决类在自身异常时宁可丢弃也不误放行。
- **ctx 形状**：`struct sk_msg_md`——报文数据所在内存区间与两端 socket 元数据；
  `bpf_msg_redirect_hash` 等重定向 helper 以它为操作对象，本示例从简未用。
- **map 选型**：单 key ARRAY（`sm_verdict`）——判决计数；`__sync_fetch_and_add`
  原子递增。
- **与 sockops 配对才生效**：sockmap 未挂对应 sockops、socket 未被收进 map 前，
  本程序不会被触发——不是程序错误，而是「报文没到判决点」的体系性前提。

## 挂载方式与前提

- **预期加载前提**：root/特权档 + sockmap 与 sockops 配对搭建——先建
  `BPF_MAP_TYPE_SOCKMAP`/`SOCKHASH` map、由 sockops 程序把 socket 收进 map，
  sk_msg 判决端才有报文可看；深入搭建见第 22/23 章交叉引用。
- **挂载形态**：attach 到 sockmap/sockhash map——`bpf_prog_attach(prog_fd,
  map_fd, BPF_SK_MSG_VERDICT, flags)`，或 `bpftool prog attach <prog>
  msg_verdict <map>`；与 cgroup 类不同，sk_msg 的 attach 对象是 map 而非 cgroup。
- **失败模式排查顺序**：程序静默不触发时先查 sockmap 里有没有 socket（配对前提），
  再查本程序；map 异常时本程序回 `__SK_DROP`（fail-closed，见代码走读）。

## 预期输出示例

（预期输出示例 / 运行级待验证）按第 22/23 章口径搭好 sockmap 体系并有过流量后：

```text
$ sudo bpftool map dump name sm_verdict
key: 00 00 00 00  value: 2a 00 00 00 00 00 00 00   # 到达判决点的报文数
Found 1 element
```

计数随「经 sockmap 判决点的报文」增长；本程序恒放行，报文照常送达。具体数值随
负载，以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `sk_msg.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- 内核源码 `include/uapi/linux/bpf.h`：`enum sk_action`（`__SK_PASS`/`__SK_DROP`，
  成员名带下划线前缀）与 `struct sk_msg_md` 定义。
- 内核源码 `net/core/sock_map.c`：sockmap/sk_msg 的 attach 与判决执行路径。
- 第 22/23 章交叉引用：sockmap 完整体系（sockops 收纳 + sk_msg 判决 + 真实重定向）。
- 第 3 章失败画廊 card ①：`book/examples/chapter-03/02-fail-gallery/01-map-null/`
  （本程序 `if (!v)` 分支的出处）。

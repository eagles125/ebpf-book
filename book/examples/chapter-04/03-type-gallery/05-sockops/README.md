# 05-sockops：TCP 连接生命周期回调——两类「连接建立」事件计数（类型画廊⑤）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.12 节
（sockops card）的配套最小程序。XDP/TC 站在报文级看网络，sockops 站在 socket 级看
连接：程序挂在 cgroup 上，内核 TCP 状态机每走一步（连接建立、状态迁移、拥塞窗口
更新……）都以 `struct bpf_sock_ops` 回调一次——socket 级状态（两端地址端口、
TCP 状态、`snd_cwnd` 等）直接可读，这是报文级类型（XDP/TC）没有的维度。

## 任务一句话

写一个 `SEC("sockops")` 程序：按事件码 `ops->op` 识别两类连接建立事件——被动建立
（服务端三次握手完成）与主动建立（客户端 connect 完成），分 key 计数进一个 ARRAY
map；其余事件一律放行不干预。

## 代码走读

- **事件码判别**：`ops->op` 是事件码——`BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB`/
  `BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB` 两个枚举常量在 BTF（编写环境冒烟实测
  「常量在场」），直接 `switch` 判别即可。
- **未知事件放行**：sockops 回调种类多（状态迁移、拥塞控制更新……），`switch` 的
  `default` 一律 `return 0`——教学只关心两种建立，其余全交还内核默认路径。
- **返回值语义**：**1 = 本 callback 已处理该事件，0 = 交给默认路径**——与网络
  verdict 类的「1 放行 / 0 丢弃」（画廊③）不同：同样是 1/0，含义由程序类型说了算，
  「类型决定返回值语义」的又一实证。
- **ctx 形状**：`struct bpf_sock_ops`——本地/对端地址端口、TCP 状态与 `snd_cwnd`
  等 socket 级字段直接可读；本程序只读 `op`，其余字段留作改造入口。
- **map 选型**：ARRAY、`max_entries=2`——两类建立事件固定计数（0=被动建立
  1=主动建立）；计数用 `__sync_fetch_and_add` 原子递增。
- **失败分支必检**：`bpf_map_lookup_elem` 返回 NULL 即放行——第 3 章画廊 card ①
  （未检查的映射查找）的正面写法，此处仅回指不重复展开。

## 挂载方式与前提

- **预期加载前提**：root/特权档 + cgroup 挂载（cgroup v2 已挂载，如
  `/sys/fs/cgroup`）；事件由内核 TCP 状态机驱动，无需额外用户态准备。
- **挂载形态**：attach 到某个 cgroup——libbpf
  `bpf_program__attach_cgroup(prog, cgroup_fd)`，或
  `bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCKOPS, flags)`，或
  `bpftool cgroup attach <cgroup> sockops <prog>`；挂上后该 cgroup 内所有 TCP
  连接的状态机事件都回调本程序。
- **与 sockmap 的关系**：sockops 常作 sockmap 的「收 socket 进 map」一端（在建立
  事件里调 sockmap 更新 helper）——本示例只计数不收 socket；sockmap 完整体系深入
  见第 22/23 章交叉引用（同总览表口径）。

## 预期输出示例

（预期输出示例 / 运行级待验证）挂到某个 cgroup 后，在该 cgroup 下跑一组 TCP
客户端/服务端（如 `nc -l` 与 `nc`，或 `curl`），读 map：

```text
$ sudo bpftool map dump name tcp_estab
key: 00 00 00 00  value: 03 00 00 00 00 00 00 00   # 被动建立（服务端）计数
key: 01 00 00 00  value: 05 00 00 00 00 00 00 00   # 主动建立（客户端）计数
Found 2 elements
```

每次握手完成对应一次计数增长；其余 TCP 事件（状态迁移等）被 `default` 放行、
不计数。具体数值随负载，以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `sockops.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- 内核源码 `include/uapi/linux/bpf.h`：`BPF_SOCK_OPS_*` 事件码枚举与 sockops 回调
  语义注释（「常量在场」的 BTF 出处即此处枚举）。
- 内核源码 `include/net/tcp.h`（`tcp_call_bpf`——sockops 回调的触发封装）与
  `net/ipv4/tcp_input.c`（连接建立等状态机事件的调用点）。
- libbpf 文档：`bpf_program__attach_cgroup()`。
- 第 22/23 章交叉引用：sockmap 体系中 sockops 的「收 socket」角色。
- 第 3 章失败画廊 card ①：`book/examples/chapter-03/02-fail-gallery/01-map-null/`
  （本程序 `if (!v)` 分支的出处）。

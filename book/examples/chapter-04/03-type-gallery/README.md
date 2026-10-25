# 03-type-gallery：类型画廊——8 类「怎么写对」的最小实物总览

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.8–3.14 节
（目录级 7 card）的配套示例总览：8 个目录，每目录一个最小可编译程序 + Makefile +
六段 README，回答「每一类怎么写对」；失败形态（「为什么被拒」）回指第 3 章失败画廊，
本画廊只做正面示范，不重复展开。

## 8 类总览表

目录/prog_type/SEC/ctx 类型/返回值语义五列与正文 3.8–3.14 card 共用口径逐字一致；
「关键教学点」展开为一句话速览，完整口径见各目录 README。

| 目录 | prog_type | SEC | ctx 类型 | 返回值语义 | 关键教学点 |
|---|---|---|---|---|---|
| `01-kretprobe/` | KPROBE | `SEC("kretprobe/do_sys_openat2")` | `BPF_KRETPROBE(krp, int ret)`（宏内部为 pt_regs） | 0 正常 | kretprobe 在目标函数**返回处**触发，返回值经 `BPF_KRETPROBE` 宏读取（架构相关展开，x86 由 `PT_REGS_RC`——以所用架构为准）；`ret>=0` 为 fd、负值为错误码 |
| `02-uprobe/` | KPROBE（uprobe 变体） | `SEC("uprobe")` | `int up(struct pt_regs *ctx)` | 0 | 用户态探针：SEC 命名规则 `uprobe/<二进制路径>:<偏移>`，路径与偏移由 loader/bpftool 提供、编译期 SEC 串仅为标记；用户内存读必须走 probe 系 helper（回指第 3 章画廊 card ⑦） |
| `03-cgroup-skb/` | CGROUP_SKB | `SEC("cgroup/skb")` | `int cg(struct __sk_buff *skb)` | **1 放行 / 0 丢弃** | cgroup 维度的网络 verdict；`bpf_get_current_cgroup_id()` 演示 cgroup 上下文获取；与 XDP/TC 的选择边界（挂哪、能否看 socket 状态） |
| `04-socket-filter/` | SOCKET_FILTER | `SEC("socket")` | `int sf(struct __sk_buff *skb)` | **返回值=接受拷贝的字节数**（0=该 socket 不见此包） | 经典 tcpdump/pcap 载体；只读观测、不干预协议栈；与 cgroup-skb 同 ctx 不同返回语义的对照 |
| `05-sockops/` | SOCK_OPS | `SEC("sockops")` | `int so(struct bpf_sock_ops *ops)` | 1=已处理 / 0 透传 | TCP 连接生命周期回调（`ops->op` 判 `BPF_SOCK_OPS_PASSIVE/ACTIVE_ESTABLISHED_CB`）；socket 级状态可见——XDP/TC 没有的视角 |
| `06-sk-msg/` | SK_MSG | `SEC("sk_msg")` | `int sm(struct sk_msg_md *msg)` | `__SK_PASS` / `__SK_DROP` | sockmap 重定向判决；与 sockops 配对构建 sockmap 体系（深入留第 22/23 章交叉引用） |
| `07-lsm/` | LSM | `SEC("lsm/file_open")` | `int BPF_PROG(lf, struct file *file)` | **0 允许 / 非 0 阻断**（裁决语义以所用内核源码 `include/linux/lsm_hook_defs.h` 为准） | 安全决策点：内核对象（`struct file *`）直接可读；前提 `CONFIG_BPF_LSM=y` + `lsm=bpf` 启动参数（本机 =y 实证，运行级待验证） |
| `08-netfilter/` | NETFILTER | `SEC("netfilter")` | `int nf(struct bpf_nf_ctx *ctx)` | `NF_ACCEPT` / `NF_DROP` | 内核 6.6+ 新程序类型；ctx 为 `bpf_nf_ctx{state, in, out}`；完整可运行示例交叉引用第 9 章 `09-netfilter-go/` |

## 阅读顺序建议

- **目录序即正文序**：`01→08` 对应正文 3.8–3.14（3.14 是 lsm+netfilter 合并 card，
  对应 `07/`、`08/` 两个目录）。
- 建议分三组读，每组钉住一个三元组维度：
  1. **追踪类延伸（01/02）**：kprobe 家族——kretprobe 读返回值、uprobe 下探用户态；
     ctx 都是 pt_regs，差异在「站在哪、SEC 给谁看」。
  2. **网络 verdict 类（03/04）**：同为 `__sk_buff` ctx，返回值语义却完全不同——
     1/0 二值判决 vs 「拷几字节」的观测语义；类型决定返回值语义的最短对照。
  3. **socket 与安全类（05–08）**：sockops/sk_msg 的 socket 级视角，LSM/netfilter 的
     安全决策点——从观测走进干预。
- 每目录先读 README「挂载方式与前提」，再看 `.bpf.c`；正文 card 与目录 README 的
  代码走读共用口径，可对照读。

## compile-only 口径与安全警告

- 画廊 8 目录全部 **compile-only**：Makefile 只产出 `.bpf.o`，不生成 skeleton、
  不生成 loader——这些程序是「类型怎么写对」的教学实物，验收口径就是 clang 编译通过；
  各目录 Makefile 头部注明「本脚本只编译不加载」。
- **安全警告**：本画廊所有构建脚本只编译不加载，无挂载风险；加载级实验须按各目录
  README 的「挂载方式与前提」在特权环境进行；若把画廊程序改造成加载实验，网络类
  只应挂在 veth 或 lo 上（章级 README 安全警告同口径）。

## 验证状态汇总

| 目录 | 验证状态 |
|---|---|
| `01-kretprobe/`–`08-netfilter/`（8 目录统一口径） | 编译级已通过；加载级/运行级待特权环境验证 |

- **编译验证**：编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）；
  各目录实际执行的 `make` 结果记录在其 README「验证状态」段。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；
  各目录 README 中「预期输出」均为「预期输出示例 / 运行级待验证」。

## 参考来源

- 正文 3.8–3.14 各 card（与本画廊目录一一对应）；第 3 章失败画廊 card ①–⑦
  （`book/examples/chapter-03/02-fail-gallery/`，本画廊失败模式回指的出处）。
- Linux kernel documentation：`Documentation/bpf/`（程序类型、attach 类型与各 ctx 定义）。
- 各目录 README「参考来源」段（逐类文献与内核源码路径）。

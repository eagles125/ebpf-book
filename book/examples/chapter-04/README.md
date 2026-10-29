# 第 4 章独立示例资产

本目录是《第 4 章 eBPF 程序类型与钩子点深入》的配套示例代码，对应正文「三、类型画廊：十四类逐类走读」与「四、实战：为任务选对类型并挂上钩子」两节；
全部资产围绕本章教学主轴——「同一事件多副眼镜」组织：2 个集成锚点 + 8 个类型画廊目录 + 2 项课后扩展任务，把「类型决定什么」从一句话变成可 diff 的实物。

## 演示目标

程序类型（program type）决定三件事：上下文（ctx）布局、辅助函数（helper）可用面、返回值语义。本章用两个集成锚点把这三件事钉到可 diff 的实物上：

- `01-multi-observe/`——**同一事件五副眼镜**：同一个 `do_sys_openat2` 事件（openat2 系统调用的公共实现）同时经 tracepoint、raw tracepoint（raw_tp）、kprobe、fentry、fexit 五类追踪程序观察，一个 loader 挂 5 个 link、共享一个分镜映射（map）按镜计数并存各镜最近读到的文件名。五副眼镜的差异就是类型化程度的谱系：typed 上下文（tracepoint）→ 无类型 args 强转（raw_tp）→ pt_regs 手工取参（kprobe）→ BTF 直接类型化（fentry）→ 返回值语义（fexit 才有返回值）。
- `02-xdp-tc/`——**同一报文两种 ctx**：XDP 与 TC 程序同挂 lo，同一报文先经 `xdp_md`（驱动层视角）再经 `__sk_buff`（协议栈视角），对照网络类程序的 ctx 布局与挂载点（attachment point）差异。

8 个类型画廊目录（`03-type-gallery/`）是「正面画廊」：每类一个最小可编译程序，回答「每类怎么写对」；失败形态（「为什么被拒」）回指第 3 章失败画廊，不在此重复。

## 示例总览

| 目录 | 类别 | 程序类型 / SEC | 教学主题 | 构建口径 |
|---|---|---|---|---|
| `01-multi-observe/` | 集成锚点①（追踪类） | tracepoint / raw_tp / kprobe / fentry / fexit，五程序一个对象 | 同一事件五副眼镜：ctx 布局、类型化程度、返回值语义 | 全链（skeleton + loader） |
| `02-xdp-tc/` | 集成锚点②（网络类） | XDP + TC（clsact） | 同一报文两种 ctx：`xdp_md` vs `__sk_buff` | 全链（skeleton + loader） |
| `03-type-gallery/01-kretprobe/` | 画廊 1 | KPROBE（kretprobe），`kretprobe/do_sys_openat2` | `BPF_KRETPROBE` 读返回值（架构相关展开，以所用架构为准） | compile-only |
| `03-type-gallery/02-uprobe/` | 画廊 2 | KPROBE（uprobe 变体），`uprobe/<二进制>:<偏移>` | 用户态探针与 SEC 命名规则（用户内存必须走 probe 系辅助函数） | compile-only |
| `03-type-gallery/03-cgroup-skb/` | 画廊 3 | CGROUP_SKB，`cgroup/skb` | cgroup 维度网络判决：1 放行 / 0 丢弃 | compile-only |
| `03-type-gallery/04-socket-filter/` | 画廊 4 | SOCKET_FILTER，`socket` | 返回值 = 接受拷贝的字节数（经典 tcpdump 载体，只读观测） | compile-only |
| `03-type-gallery/05-sockops/` | 画廊 5 | SOCK_OPS，`sockops` | TCP 连接生命周期回调（socket 级状态可见） | compile-only |
| `03-type-gallery/06-sk-msg/` | 画廊 6 | SK_MSG，`sk_msg` | sockmap 重定向判决（`__SK_PASS`/`__SK_DROP`） | compile-only |
| `03-type-gallery/07-lsm/` | 画廊 7 | LSM，`lsm/file_open` | 安全决策点：0 允许 / 非 0 阻断（前提 `CONFIG_BPF_LSM=y` + `lsm=bpf` 启动参数） | compile-only |
| `03-type-gallery/08-netfilter/` | 画廊 8 | NETFILTER，`netfilter` | `NF_ACCEPT`/`NF_DROP`（内核 6.6+；完整可运行示例交叉引用第 9 章） | compile-only |
| `exercises/01-type-selection/` | 练习 1 | 按场景选型 | 选型决策：三个场景各选程序类型与挂载点并写骨架论证 | compile-only（参考实现） |
| `exercises/02-type-mismatch/` | 练习 2 | 错位 → 归位 | 类型错位修复挑战：编译通过但加载预期被拒 | compile-only（broken/fixed 对） |

另有两个支撑目录，不占总览行：`00-env/`（环境自检，含本章特有的 tracefs 与 `CONFIG_BPF_LSM`/`lsm=bpf` 检查项）与 `scripts/`（`verify-build.sh` 单章编译级汇总验证，模式同第 2/3 章）。

## 环境前提

- **编译级（无特权可完成）**：clang、gcc、bpftool、make、libbpf 头文件与内核 BTF（`/sys/kernel/btf/vmlinux`）；`bash 00-env/check-env.sh` 逐行自检。
- **加载级/运行级（需特权）**：root 或 CAP_BPF；XDP/TC 挂载另需 CAP_NET_ADMIN；`unprivileged_bpf_disabled=2` 的主机上非特权用户不可加载。
- **本章特有前提**：
  - tracefs（`/sys/kernel/tracing`）：tracepoint/raw_tp 事件的运行时载体，需 root 读取，加载级验证时使用；
  - LSM 画廊条目：内核需 `CONFIG_BPF_LSM=y` 且启动参数含 `lsm=bpf`（缺任一只剩编译级可玩）；
  - netfilter 画廊条目：内核 6.6+。

## 学习路径

1. `bash 00-env/check-env.sh` 自检：无 root 也可运行，按「编译级/加载级/运行级」三档给出结论，并附本章特有的 tracefs/LSM 检查。
2. 进入 `01-multi-observe/`：先 `make` 编译，再按其 README 的五镜对照表逐段读代码（加载与运行需要 root；编译级验证无特权即可完成）。
3. 进入 `02-xdp-tc/`：对照 `xdp_md` 与 `__sk_buff` 两种 ctx 读代码；运行需 root 且只挂 veth 或 lo（见下方安全警告）。
4. 进入 `03-type-gallery/`：按 01→08 顺序逐目录读「六段 README + 最小程序」，每类先看挂载方式与前提，再读代码。
5. 进入 `exercises/`：完成 2 项课后扩展任务（先自行实现，再看防剧透 README 后部的参考实现思路）。

## 安全警告

- XDP 程序直接作用于网卡收包路径——本章示例只应挂在 veth 或 lo 上实验；切勿在不了解影响前挂到生产物理网卡。（锚点②及一切 XDP 实验适用）
- TC 程序挂于 qdisc（clsact），同样只应在实验接口（veth/lo）上操作。
- 画廊 8 目录与 exercises 练习只编译不加载；`00-env/check-env.sh` 为只读检查脚本；`scripts/verify-build.sh` 只编译不加载——均无上述风险。

## 统一构建模式

两类构建口径，均在目录内直接 `make`：

- **锚点全链**（`01-multi-observe/`、`02-xdp-tc/`）：libbpf-bootstrap 三步——

  vmlinux.h（bpftool 由 /sys/kernel/btf/vmlinux 生成） -> *.bpf.o（clang -target bpf）
      -> *.skel.h（bpftool gen skeleton） -> 可执行 loader（gcc -lbpf -lelf -lz）

- **compile-only**（画廊 8 目录与 exercises 两目录）：只产出 `.bpf.o`，不生成 skeleton 与 loader——这些程序是「类型怎么写对」的教学实物，验收口径就是编译成功；画廊与编译脚本头部注明「本脚本只编译不加载」。

`00-env/` 为纯脚本目录（无 Makefile）；`scripts/verify-build.sh` 汇总本目录全部 `make`（模式同第 2/3 章）。

## 预期输出说明

编写环境无特权（非 root、`unprivileged_bpf_disabled=2`），加载与运行不可实测：各目录 README 中的「预期输出」一律为**预期输出示例 / 运行级待验证**，由特权环境或读者复验；编译级结果（`.bpf.o`、skeleton、loader 是否生成）则是本机可验证的事实。

## 代码讲解要点

- 全部 `.bpf.c` 中文注释覆盖五类要点：字段语义、验证器边界检查原因、辅助函数选型原因、映射选型原因、失败模式；loader 注释覆盖 attach 选型与失败模式。
- 目录 README 结构：两个锚点为十段结构（含五镜对照表 / 两种 ctx 对照表）；画廊各目录为六段（任务一句话 / 代码走读 / 挂载方式与前提 / 预期输出示例 / 验证状态 / 参考来源）；练习为防剧透七段（参考实现思路独立成节放后部）。

## 常见失败点

| 现象 | 原因 | 处理 |
|---|---|---|
| `open`/`load` 返回 EPERM | 非 root 且 `unprivileged_bpf_disabled=2`，不可加载 | 用 sudo 完成加载级实验；无特权只做编译级验证 |
| SEC 名写错导致加载报错 / attach 失败 | SEC 前缀决定程序类型与挂载点解析 | 对照总览表与各目录 README 的 SEC 名逐字核对 |
| LSM 程序加载被拒 | 内核缺 `CONFIG_BPF_LSM=y` 或启动参数未含 `lsm=bpf` | 看 `00-env/check-env.sh` 本章特有检查项的 WARN/INFO 提示 |
| fentry/fexit/kprobe 加载报签名不匹配 | 目标函数签名随内核版本演进（漂移本身进教学） | 以本机 BTF 解析为准（`bpftool btf dump` 后检索目标函数名），加载期由 BTF 校验 |
| tracefs 事件目录读不到 | tracefs 需 root | 加载级验证时使用 sudo（check-env.sh 的 WARN 档说明） |

## 验证状态说明

遵循 `book/STYLE.md` 约定：

- **编译验证**：编写环境工具链齐备（x86_64、内核 7.0.0-38、clang 18.1.3、bpftool 7.7.0（libbpf v1.7）、libbpf-dev 1.3.0，BTF 可用），`bash 00-env/check-env.sh` 可逐行复核。全量编译结果由 `scripts/verify-build.sh` 生成：**PASS=13、FAIL=0、退出码 0**（2026-10-30 本机实测，工具版本即上列实测值）。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；各 README 中「预期输出」均为「预期输出示例 / 运行级待验证」。
- **本章补充（C6 口径）**：全链锚点（01/02）的验证状态口径为「编译级（含 gcc 链接）已通过；加载级/运行级待特权环境验证」；画廊与练习程序为「编译级已通过；加载级/运行级待特权环境验证」。
- **画廊 compile-only 口径**：画廊 8 类与练习程序的验收标准是 clang 编译通过——它们不是语法错误，而是语义教学实物；加载级行为（挂载、事件触发、ctx 访问拒绝）本机不可实测，预期日志一律标注「预期输出示例 / 运行级待验证」。

## 参考来源

- Linux kernel documentation：BPF 程序类型与挂载点（`Documentation/bpf/`）、tracefs（`Documentation/trace/`）。
- libbpf 与 libbpf-bootstrap 项目文档（SEC 命名与自动 attach、skeleton 机制）。
- `bpftool` 手册页（BTF dump 与 skeleton 生成）。

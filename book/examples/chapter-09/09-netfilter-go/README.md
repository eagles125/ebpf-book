# 09-netfilter-go：Netfilter 数据面 + Go 控制面

对应 ebpf.top BPF 入门系列实践教程九（在 Netfilter 中使用 eBPF 以及 Go 经验总结，系列第 12 篇），以及本书第 9 章第 12 节。

## 演示目标

- 第一次接触“控制面/数据面分离”的工程形态：Go 负责加载、规则下发、统计读取；
  BPF 数据面在内核网络路径上高频执行，只做解析报文、查 map、更新计数、返回动作。
- 理解 map pinning：map 固定到 bpffs（`/sys/fs/bpf/nf_guard_rules` 等），
  控制面进程退出后规则保留，多个进程可共享操作。
- 认识 Netfilter 钩子：与 XDP（驱动收包早期）、TC（qdisc）位置不同，
  Netfilter 位于传统防火墙路径，适合把 eBPF 与 hook/优先级/返回动作等经典概念对接。

## 架构

```text
           +----------------------------- Go 控制面 (nfctl) -----------------------------+
           |  run: 加载 .o、attach NF_INET_LOCAL_OUT、定时读统计                          |
           |  block/unblock: 写 /sys/fs/bpf/nf_guard_rules                                |
           |  stats: 读 /sys/fs/bpf/nf_guard_stats                                        |
           +------|---------------------------|--------------------------|----------------+
                  | bpf_link                  | map update               | map lookup
                  v                           v                          v
   NF_INET_LOCAL_OUT 钩子            nf_guard_rules (HASH)      nf_guard_stats (PERCPU)
                  |                           ^                          ^
                  v                           |                          |
           nf_guard.bpf.o 解析 IPv4/TCP/UDP --+--> 查黑名单 --命中--> drop +1
           （数据面，无用户态参与）                                     --未命中--> pass +1
```

## 文件说明

| 文件 | 说明 |
|---|---|
| `bpf/nf_guard.bpf.c` | 数据面：`SEC("netfilter")` 程序，按目标端口黑名单 drop |
| `cmd/nfctl/main.go` | Go 控制面：cilium/ebpf 加载、attach、规则与统计 |
| `go.mod` | Go 模块定义（依赖 `cilium/ebpf`） |
| `scripts/netns-lab.sh` | 隔离实验拓扑（netns + veth，`up`/`down`） |

## 环境前提

- **内核 6.6+**（`BPF_PROG_TYPE_NETFILTER` 引入版本）。
- Go 1.22+；`go.sum` 已随示例入库，`go mod tidy` 仅在增删依赖或更换版本时需要（见下文编译节）。
- bpffs 已挂载（`mount | grep bpf`，通常为 `/sys/fs/bpf`）。
- root 或 `CAP_BPF` + `CAP_NET_ADMIN`。
- **安全警告**：网络路径示例必须在隔离拓扑中验证。错误的 drop 规则可能断开
  当前 SSH 连接。请始终在 `scripts/netns-lab.sh` 创建的 netns 内实验。

## 编译

```bash
cd book/examples/chapter-09/09-netfilter-go
go mod tidy     # 首次或更换依赖版本时执行（go.sum 已随示例入库）
make            # vmlinux.h -> bpf/nf_guard.bpf.o -> nfctl
```

## 运行（完整实验流程）

```bash
# 1. 创建隔离拓扑
sudo bash scripts/netns-lab.sh up

# 2. 进入命名空间，启动控制面
sudo ip netns exec nfguard bash
./nfctl run
#   输出示例（预期输出为教学说明）：
#   nf_guard 已挂到 NF_INET_LOCAL_OUT（优先级 0）
#   map 已 pin：/sys/fs/bpf/nf_guard_rules / /sys/fs/bpf/nf_guard_stats
#   pass=24           drop=0

# 3. 另一终端（同样进入 netns），拉黑 DNS（UDP 53）并发起解析
sudo ip netns exec nfguard bash
./nfctl block 53
dig +short example.com @1.1.1.1     # 或 nslookup
#   预期：请求超时——报文已在 LOCAL_OUT 被 drop

# 4. 观察统计变化
./nfctl stats
#   预期输出为教学说明：pass=31           drop=2

# 5. 解除并清理
./nfctl unblock 53
dig +short example.com @1.1.1.1     # 恢复正常
exit    # 退出两个 netns shell（nfctl run 收到 Ctrl-C 后卸载程序）
sudo bash scripts/netns-lab.sh down
sudo rm /sys/fs/bpf/nf_guard_rules /sys/fs/bpf/nf_guard_stats   # 彻底清理 map
```

## 代码讲解要点

- **`SEC("netfilter")` + `struct bpf_nf_ctx`**：程序类型与上下文由 section 名确定；
  `ctx->skb` 是当前报文，`ctx->state` 提供钩子信息。返回 `NF_DROP`(0) 丢弃报文、
  `NF_ACCEPT`(1) 放行——这就是 Netfilter 程序的“决策语义”，与 tracepoint
  的“观测语义”形成对照。
- **`bpf_skb_load_bytes_relative(..., BPF_HDR_START_NET)`**：从网络层头开始
  拷贝报文内容到栈缓冲，屏蔽链路层差异。示例简化处理：跳过带 IP options
  （`ihl != 5`）的包。
- **`__uint(pinning, LIBBPF_PIN_BY_NAME)`**：map 自动 pin 到
  `/sys/fs/bpf/<map 名>`。不 pin 时 map 生命周期与进程 fd 绑定，进程退出即释放；
  pin 后跨进程、跨重启保留——这也是 daemon 重启、多进程协作的基础。
- **cilium/ebpf 侧**：`LoadCollectionSpec` + `NewCollectionWithOptions(PinByName)`
  复用或创建 pinned map；`link.AttachNetfilter` 创建 bpf link 挂钩子；
  `LoadPinnedMap` 让独立子命令直接操作 map。per-CPU map 的 `Lookup`
  返回每 CPU 计数切片，用户态求和。
- **hook 选择**：`NF_INET_LOCAL_OUT` 命中本机出方向流量，适合在 netns 内
  验证“发起的连接被丢弃”；`run` 在哪个 netns 内执行，link 就挂在哪个
  命名空间的钩子上。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 加载报 unknown program type / invalid section | 内核低于 6.6，不支持 NETFILTER 程序类型 |
| `mount bpffs` 相关报错 | bpffs 未挂载：`sudo mount -t bpf bpf /sys/fs/bpf` |
| `block` 报打开 map 失败 | 没有先运行 `run`（map 尚未 pin） |
| drop 统计不增长 | 报文没经过所选钩子：确认在正确的 netns、正确的方向发起流量 |
| verifier 拒绝访问 `ctx->skb` 相关 helper | NETFILTER 程序的 helper 集较新，跨内核行为有差异；用 `bpftool feature probe` 核对，并参考内核源码 `net/netfilter/nf_bpf_link.c` 与 `tools/testing/selftests/bpf/progs/netfilter.c` |
| 宿主机网络异常 | 违反了隔离原则——立即 `down` 清理拓扑，确认没有把程序挂到宿主机命名空间 |

## 与第 17 章的关系

第 17 章的高性能防火墙将在 XDP/TC 上实现完整 ACL、conntrack、NAT；
本示例用 Netfilter + 最简黑名单展示“控制面/数据面分离”的工程骨架，
两者的 map 设计、pinning、Go 控制面模式一脉相承。

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3、cilium/ebpf v0.16.0）
  `make` 全链（vmlinux.h -> `bpf/nf_guard.bpf.o` -> `nfctl`）编译通过，
  `go build ./...` 与 `go vet ./...` 实测零报错（`go.sum` 随示例入库）。
- 待运行验证：编写环境为非 root，未执行加载、挂载与隔离拓扑实验；
  README 中所有“预期”输出均为教学说明。Netfilter BPF 的 verifier 行为
  与 `cilium/ebpf` link API 细节建议在 6.6+ 特权环境按“常见失败点”逐项核对。

## 参考来源

- ebpf.top: BPF 入门系列实践教程九（在 Netfilter 中使用 eBPF 以及 Go 经验总结）。
- 内核 6.6 Netfilter BPF 合入系列（netfilter: bpf: netfilter bpf_link 支持）。
- cilium/ebpf 上游文档：`link.AttachNetfilter`、map pinning。
- 内核 selftests：`tools/testing/selftests/bpf/progs/netfilter.c`。

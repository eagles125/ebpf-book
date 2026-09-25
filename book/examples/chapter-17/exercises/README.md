# 第 17 章课后扩展任务总览

本章四项课后扩展任务的完整参考实现，覆盖**数据面**与**控制面**两个扩展维度，按难度由浅入深编号（任务编号即目录编号）。各练习以对应阶段的示例为基线，复用本章主示例的构建体系（Makefile 同构），可独立编译。

## 任务总览

| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 数据面 | IPv6 ACL 支持：解析分支 + map key 扩展 | `01-ipv6-acl/` |
| 2 | 控制面 | 规则持久化与重启恢复：保存/加载/原子替换 | `02-rule-persistence/` |
| 3 | 控制面 | Prometheus 指标导出：per-CPU 聚合 + /metrics | `03-prometheus-export/` |
| 4 | 数据面 | 按端口限速：令牌桶 map + 超额动作 | `04-port-rate-limit/` |

## 防剧透阅读法

每项练习的 README 都按「**任务在前、参考实现思路独立成节放在后部**」组织，以一条 `---` 分界线隔开：

1. **先只读分界线之前**：任务（含验收标准）、提示（只给「会用到什么」）、环境前提与构建、预期输出示例——据此自行设计并实现；
2. **做完再对照分界线之后**：参考实现思路逐条解释本目录内参考代码的取舍与理由，用作自查与进阶讨论的起点。

直接阅读参考实现等于放弃练习价值——四项任务的验收标准都写在对应 README 的「任务」一节，可先按标准自测。

## 构建命令

各练习在其目录内 `make` 即可（数据面练习为 C/libbpf loader，控制面练习为 BPF 对象 + Go 控制面联合构建）：

| 练习 | 基线 | 语言栈 | 构建 |
|---|---|---|---|
| `01-ipv6-acl/` | 阶段 2（02-parse-acl） | C / libbpf skeleton | `cd 01-ipv6-acl && make` |
| `02-rule-persistence/` | 阶段 5（05-firewall） | BPF 对象 + Go（cilium/ebpf） | `cd 02-rule-persistence && make` |
| `03-prometheus-export/` | 阶段 5（05-firewall） | BPF 对象 + Go（cilium/ebpf） | `cd 03-prometheus-export && make` |
| `04-port-rate-limit/` | 阶段 2（02-parse-acl） | C / libbpf skeleton | `cd 04-port-rate-limit && make` |

一键全量编译级验证：`../scripts/verify-build.sh`（逐阶段 + 逐练习 `make`，Go 依赖拉取失败按 WARN 记录不算失败）。运行任何练习都需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN` 与实验拓扑（`../scripts/setup-veth.sh`）；涉及丢包/NAT 的练习**只应挂载到拓扑创建的 veth 接口**，勿挂物理网卡。

## 验证状态汇总

| 练习 | 编译级 | 运行级 |
|---|---|---|
| 01-ipv6-acl | 已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过 | 待验证 |
| 02-rule-persistence | 已在编写环境（同上 / Go 1.27.1）`make` 通过（含 `go vet`） | 待验证 |
| 03-prometheus-export | 已在编写环境（同上 / Go 1.27.1）`make` 通过（含 `go vet`） | 待验证 |
| 04-port-rate-limit | 已在编写环境（同上，无 Go）`make` 通过 | 待验证 |

统一口径：编写环境为非 root（`unprivileged_bpf_disabled=2`），四项练习均未执行加载、挂载与运行，各 README 的「预期输出示例」均为教学示例；练习 2/3 的 Go 依赖已提交 `go.sum`，正常联网环境可直接编译，离线环境按「依赖待拉取，编译级待验证」处理。

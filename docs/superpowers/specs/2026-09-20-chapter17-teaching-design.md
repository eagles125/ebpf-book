---
comet_change: generate-ebpf-book
role: technical-design
canonical_spec: openspec
---

# 第 17 章完整教学章节技术设计（eBPF 防火墙，渐进搭建式）

- 日期：2026-09-20
- 上游事实源：`openspec/changes/generate-ebpf-book/`（proposal / design / specs / tasks）
- 设计范围：tasks.md §5.1 全部任务——第 17 章从深度骨架升级为完整教学章节，落地全部渐进式示例资产、课后扩展任务与参考实现，并把教学规范写入 `book/STYLE.md`。

## 1. 背景与目标

用户反馈：第 17 章内容过于空泛，`book/examples/chapter-17/` 目录为空；要求按「教师-学生双视角 + 五层环境阐述 + 核心代码原理注释 + 课后扩展任务与参考实现」重新组织，确保读者跟着书稿能获取完整知识。

本设计将第 17 章从 8.3k 字符的深度骨架升级为 40–60k 字符的完整教学章节，并将可复用的教学模板沉淀进 `book/STYLE.md`，约束后续所有实战章节。

### 已确认的决策（用户逐项确认）

| 决策 | 内容 |
|------|------|
| 验证策略 | 编译级真实验证（本机 clang 18.1.3 / bpftool / Go 1.27.1 / BTF 齐备）+ 运行效果写「预期输出示例」并标注「运行级待验证」（本机无 root，`unprivileged_bpf_disabled=2`） |
| 课后参考实现 | 完整可编译：每练习独立目录、完整源码、复用主示例构建体系、附思路讲解与验证状态 |
| 示例组织 | 渐进式阶段目录（00-env → 01-xdp-drop → 02-parse-acl → 03-conntrack → 04-nat-tc → 05-firewall），与第 9 章分目录模式一致 |
| 章节叙事 | 方案 A 渐进搭建式：章内叙事跟随阶段推进，每阶段四段式教学节奏 |

## 2. 示例资产设计

### 2.1 目录结构

```text
book/examples/chapter-17/
├── README.md                # 章级导读：阶段目录表、学习路径、安全警告、验证状态约定
├── 00-env/
│   └── check-env.sh         # 环境自检：内核/BTF/工具链/权限分级诊断（无特权也能跑）
├── 01-xdp-drop/             # 阶段1：最小 XDP 丢弃
│   ├── xdp_drop.bpf.c       #   ~50 行：SEC("xdp")、XDP_DROP/XDP_PASS 返回
│   ├── xdp_drop.c           #   C loader（libbpf-bootstrap 风格：open_and_load + attach）
│   ├── Makefile             #   vmlinux.h -> bpf.o -> skel.h -> loader
│   └── README.md            #   演示目标/文件说明/环境前提/编译与运行/预期输出
├── 02-parse-acl/            # 阶段2：报文解析 + ACL 五元组精确匹配
│   ├── fw_common.h          #   flow_key、规则结构、统计结构（阶段2 版本）
│   ├── acl.bpf.c            #   Ethernet/IPv4/TCP/UDP 解析 + hash 精确规则 + LPM CIDR 查找
│   ├── acl.c                #   loader + 命令行规则下发
│   ├── Makefile
│   └── README.md
├── 03-conntrack/            # 阶段3：连接跟踪 + LRU
│   ├── fw_common.h          #   扩展 ct_key/ct_value/ct_state
│   ├── conntrack.bpf.c      #   正反向 key、状态迁移、LRU 限额
│   ├── conntrack.c          #   loader + conntrack dump
│   ├── Makefile
│   └── README.md
├── 04-nat-tc/               # 阶段4：TC 路径 SNAT/DNAT + 增量校验和
│   ├── fw_common.h          #   扩展 NAT 映射结构
│   ├── nat.bpf.c            #   TC clsact、地址/端口改写、checksum 增量更新
│   ├── nat.c                #   loader + NAT 规则下发
│   ├── Makefile
│   └── README.md
├── 05-firewall/             # 阶段5：完整系统 + Go 控制面（最终形态）
│   ├── bpf/
│   │   ├── fw_common.h      #   完整共享头（阶段2–4 的并集 + 完善注释）
│   │   ├── fw_xdp.bpf.c     #   完整 XDP 数据面（解析 + ACL + conntrack 快路径 + stats）
│   │   └── fw_tc.bpf.c      #   完整 TC NAT 路径（SNAT/DNAT + 校验和 + stats）
│   ├── user/                #   Go 控制面（cilium/ebpf）
│   │   ├── go.mod           #   固定 cilium/ebpf 版本
│   │   ├── main.go          #   CLI：attach/detach/rule/nat/conntrack/stats
│   │   ├── rules.go         #   规则解析、冲突检查、原子更新
│   │   ├── maps.go          #   map pin 管理、加载
│   │   └── stats.go         #   per-CPU stats 读取与展示
│   ├── Makefile             #   BPF + Go 联合构建
│   └── README.md
├── scripts/
│   ├── setup-veth.sh        # ns-client/ns-fw/ns-server veth 拓扑搭建/销毁（含安全警告）
│   ├── bench-iperf.sh       # iperf3 吞吐压测
│   ├── bench-pktgen.sh      # pktgen 丢包/pps 压测
│   └── verify-build.sh      # 顶层编译级验证：逐阶段编译汇总（本机真实执行）
└── exercises/               # 课后扩展任务 + 完整可编译参考实现
    ├── README.md            #   任务总览 + 防剧透结构说明
    ├── 01-ipv6-acl/         #   练习1：IPv6 ACL 支持（解析扩展 + map key 扩展）
    ├── 02-rule-persistence/ #   练习2：规则持久化与重启恢复（控制面扩展）
    ├── 03-prometheus-export/#   练习3：Prometheus 指标导出（stats map → 指标）
    └── 04-port-rate-limit/  #   练习4：按端口限速（数据面扩展）
```

### 2.2 阶段设计原则

- **每阶段独立可编译**：各自 Makefile + README，读者可在任意阶段进入；阶段间构建文件存在刻意重复（教程仓库标准做法，换取独立性）。
- **loader 演进**：阶段 1–4 用 C loader（libbpf-bootstrap 风格，与第 9 章一致，读者聚焦数据面）；阶段 5 引入 Go/cilium-ebpf 控制面（生产级形态）。
- **代码注释规范**：所有 `.bpf.c`/`.h`/Go 源文件中文注释，覆盖：数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。
- **fw_common.h 渐进演化**：阶段 2/3/4 各自持有当阶段所需子集，阶段 5 为完整版；章内正文讲解演化过程。

### 2.3 各阶段技术要点

| 阶段 | 数据面核心 | 读者学到 | verifier/工程陷阱 |
|------|-----------|---------|------------------|
| 01-xdp-drop | `XDP_DROP`/`XDP_PASS` 返回 | XDP 挂载位置、最小闭环 | 无；第一个可运行版本 |
| 02-parse-acl | `data`/`data_end` 解析 + hash 精确规则 + LPM trie CIDR 黑白名单 | 五元组规范化、hash/LPM 两类 map 查找 | 每次访问前的边界检查；对齐访问 |
| 03-conntrack | 正反向 key + 状态机 + LRU hash | 有状态防火墙原理 | map 容量上限、写竞争、时间戳 |
| 04-nat-tc | TC clsact + NAT 改写 + 增量校验和 | XDP/TC 分工、NAT 映射 | checksum 增量算法顺序、skb vs xdp_md |
| 05-firewall | 组装完整数据面 + Go 控制面 | 生产级分离架构 | pin、原子更新、per-CPU stats 读取 |

## 3. 章节正文设计

### 3.1 章节结构

```text
学习目标 → 本章导读（教师怎么教/学生怎么学的双视角路线图）
→ 一、环境准备（1.1 依赖 / 1.2 编译 / 1.3 部署 / 1.4 运行 / 1.5 运行效果与验证状态）
→ 二、总体架构（数据面/控制面分工，改陈述式）
→ 三、阶段1：最小 XDP 丢弃
→ 四、阶段2：报文解析与 ACL
→ 五、阶段3：连接跟踪
→ 六、阶段4：TC 与 NAT、校验和
→ 七、阶段5：完整系统与 Go 控制面
→ 八、压测与结果解读
→ 可视化：数据路径决策树 → 常见问题与排错
→ 课后扩展任务（4 项，指向 exercises/）
→ 总结 → 本章参考来源
```

### 3.2 四段式教学节奏（每阶段统一，沉淀为 STYLE.md 模板）

1. **教师视角引入**：为什么需要这一步（承接上一阶段的痛点）、类比、常见误区与「读者容易卡住的地方」。
2. **核心代码原理讲解**：逐段讲设计原因、数据结构选型、helper 调用、verifier 约束、失败模式；正文代码块与 `book/examples/chapter-17/` 源文件一一对应。
3. **学生动手**：构建命令、挂载/测试命令、预期输出示例（真实运行标注 vs 预期标注区分）。
4. **检查点**：2–4 个自测问题 + 答案提示（折叠或独立小节）。

### 3.3 五层环境阐述内容口径

- **1.1 依赖环境**：内核版本与 BTF、root/`CAP_BPF`/`CAP_NET_ADMIN` 权限矩阵、veth/netns 前置、工具清单 → 对应 `00-env/check-env.sh`。
- **1.2 编译环境**：clang/LLVM、libbpf 头文件、bpftool、Go 工具链、cilium/ebpf 库的版本要求与安装验证命令。
- **1.3 部署环境**：ns-client/ns-fw/ns-server veth 拓扑设计图、`setup-veth.sh` 搭建/销毁、安全警告（勿在物理网卡误挂 XDP）。
- **1.4 运行环境**：XDP/TC 挂载与卸载、fwctl 命令总览、规则/NAT 下发流程。
- **1.5 运行效果**：各阶段预期输出总览、tcpdump 抓包对比、stats/conntrack 查询结果示例、验证状态约定说明。

### 3.4 篇幅预算

目标 40–60k 字符：环境篇 5–7k、架构 3–4k、阶段 1–4 各 6–9k、阶段 5 与控制面 8–10k、压测 4–6k、课后任务与排错 5–7k。

## 4. 课后练习设计

| 练习 | 扩展维度 | 参考实现要点 |
|------|---------|-------------|
| 01-ipv6-acl | 数据面 | IPv6 解析分支、`flow_key` 扩展、LPM/IPv6 map |
| 02-rule-persistence | 控制面 | 规则文件格式、加载顺序、重启恢复、原子替换 |
| 03-prometheus-export | 控制面 | per-CPU stats 聚合、/metrics 端点、label 设计 |
| 04-port-rate-limit | 数据面 | 令牌桶/配额 map、时间窗、超额动作 |

- 每个练习：完整可编译源码 + README（**任务与提示在前，参考实现思路独立成节**——防剧透教学设计）。
- 验证状态：编译级「已验证/待验证」+ 运行级一律「待验证」。

## 5. STYLE.md 教学规范回写（§5.1.0）

新增「实战章节教学模板」章节，包含：
1. 五层环境阐述模板（每层的内容口径，见 §3.3）；
2. 四段式阶段教学节奏（见 §3.2）；
3. 课后扩展任务与参考实现目录约定（`book/examples/<chapter>/exercises/`、防剧透 README 结构、验证状态标注）；
4. 教师-学生双视角写作要点（误区提醒、类比、自测题格式）。

## 6. 验证策略（编译级）

`scripts/verify-build.sh` 逐阶段执行并汇总：

1. `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`（无特权可执行）；
2. 各阶段 `make`（clang `-target bpf` 编译 `.bpf.c`、`bpftool gen skeleton`、C loader 编译）；
3. `05-firewall` Go 构建（`go build`，需网络拉取 cilium/ebpf；不可用时降级为「依赖说明 + 待验证」并在汇总中标注）；
4. exercises 各目录同口径编译验证。

任何编译失败即设计缺陷，修复后重跑；验证结果写入各 README 的验证状态区。

## 7. 技术风险与边界

| 风险 | 缓解 |
|------|------|
| cilium/ebpf 依赖需网络 | go.mod 固定版本；网络不可用时明确标注并给出手动拉取说明 |
| 内核版本差异（读者环境 5.x vs 本机 7.0） | helper/特性使用保守选择（5.10+ 兼容优先），版本相关处正文用保守措辞并注明 |
| pktgen 脚本无法本地验证 | 脚本 + 预期输出 + 待验证标注 |
| 章节篇幅膨胀 | 四段式模板 + §3.4 预算控制，超出预算时优先压缩重复性叙述而非删原理 |
| 阶段间代码演化漂移 | 阶段 5 的 `fw_common.h` 为阶段 2–4 的严格并集，正文明确标注新增字段来源 |
| XDP 挂载安全 | 1.3 层 + 各 README 显式安全警告；所有脚本只操作 veth/netns |

## 8. tasks.md 回写清单

- §5.1.2 由「单一 firewall/ 项目」重组为 §2.1 渐进阶段结构；
- §5.1.3 首任务对齐「五阶段叙事 + 五层环境开篇」；
- §5.1.4 练习任务固定为 4 项（IPv6/持久化/Prometheus/限速）；
- §5.1.5 交叉链接路径更新为渐进阶段目录；
- delta spec 无需变更（无 Spec Patch）。

## 9. 测试策略汇总

- **编译级（本机执行）**：§6 verify-build.sh 全绿为 build 完成条件之一。
- **结构级**：章节审计清单——五层环境、五阶段四段式、可视化、排错、总结、参考、课后任务全部存在；代码块与示例文件一一对应。
- **运行级**：预期输出 + 待验证标注；由后续批次/读者环境验证。

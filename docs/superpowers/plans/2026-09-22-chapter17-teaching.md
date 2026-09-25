---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-09-20-chapter17-teaching-design.md
base-ref: 681b67ac1629c53f69dbcff613e69263bb5fe74f
---

# 第 17 章完整教学章节（build 阶段）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 17 章从 8.3k 字符的深度骨架升级为 40–60k 字符的完整教学章节，并落地 `book/examples/chapter-17/` 全部渐进阶段示例资产（00-env → 05-firewall）、scripts 编译级验证与 4 项课后练习参考实现，同时把教学模板沉淀进 `book/STYLE.md`。

**架构:** 渐进搭建式教学：6 个独立可编译的阶段目录（阶段 1–4 用 C loader，libbpf-bootstrap 模式；阶段 5 用 Go 控制面 cilium/ebpf），`fw_common.h` 随阶段演化（阶段 2 子集 → 阶段 5 完整并集）；`scripts/verify-build.sh` 做顶层编译级汇总验证；章节正文按「五层环境开篇 + 五阶段四段式叙事」重写，所有代码块与示例文件一一对应。

**技术栈:** C（vmlinux.h + clang -target bpf + bpftool gen skeleton + libbpf loader）、Go 1.22+（cilium/ebpf v0.16.0 固定版本）、bash（环境自检/拓扑/压测/验证脚本）、Markdown（中文正文）。

## 范围与边界

- 本计划只覆盖 `openspec/changes/generate-ebpf-book/tasks.md` §5.1（5.1.0–5.1.5）与「Chapter 17: High-Performance eBPF Firewall」矩阵节。
- **第 9 章改造任务（§9 Chapter 9 的教学改造条目）不在本计划范围**，不要触碰 `book/chapters/09-*.md` 与 `book/examples/chapter-09/`。
- delta spec 无需变更（无 Spec Patch），不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 只修改：`book/STYLE.md`、`book/chapters/17-high-performance-ebpf-firewall.md`、`book/examples/chapter-17/**`、`openspec/changes/generate-ebpf-book/tasks.md`（勾选）、`.gitignore`（追加产物条目）。

## 全局约束

以下约束适用于所有任务（复制自设计文档与 STYLE.md，逐字为准）：

1. **语言**：全书与全部代码注释使用中文（`Language: zh-CN`）；术语遵循 `book/STYLE.md`（map=映射、helper=辅助函数、program type=程序类型、attachment point=挂载点）。
2. **验证策略（硬性验收）**：编译级真实验证在本机执行——clang 18.1.3、bpftool 7.7.0（libbpf 1.3 系统头）、Go 1.27.1、BTF（`/sys/kernel/btf/vmlinux` 存在）齐备；**无 root，`kernel.unprivileged_bpf_disabled=2`**。凡涉及加载/运行的效果一律写「预期输出示例」并标注「运行级待验证」，不得写成「已验证通过」。
3. **运行级验证**：只允许出现「预期输出示例 / 运行级待验证」标注；如未来在有特权环境验证，才可改为实测记录。
4. **篇幅**：章节正文目标 40–60k 字符（`wc -m` 口径；当前 8268）；预算按设计 §3.4：环境篇 5–7k、架构 3–4k、阶段 1–4 各 6–9k、阶段 5 与控制面 8–10k、压测 4–6k、课后任务与排错 5–7k。
5. **叙事**：每个阶段小节必须四段式——教师视角引入 → 核心代码原理讲解 → 学生动手 → 检查点自测（2–4 问 + 答案提示）。
6. **代码注释**：所有 `.bpf.c`/`.h`/Go 源文件中文注释，覆盖：数据结构字段语义、verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。
7. **内核兼容**：helper/特性保守选择，5.10+ 兼容优先；版本相关细节用保守措辞并注明（本机内核 7.0.0-31）。
8. **安全**：所有脚本只操作 veth/netns；每份 README 与脚本头部显式安全警告「勿在物理网卡误挂 XDP/TC」。
9. **构建产物不入库**：`.gitignore` 已排除 `book/examples/**/vmlinux.h`、`*.skel.h`、`*.o`；新增二进制产物需按任务追加条目。
10. **任务对应**：每个任务必须引用 tasks.md 的唯一任务文本片段；完成所有任务后由 Task 15 统一勾选 tasks.md。
11. **提交纪律**：每个任务以一次 git 提交结束；提交信息格式 `feat(ch17): ...` / `docs(ch17): ...`；从仓库根 `/work/ai/ebpf-book` 执行命令。
12. **工作区**：开始前确认 `git rev-parse HEAD == 681b67ac1629c53f69dbcff613e69263bb5fe74f`（或基于该提交的工作树）、`git status` 干净；如在隔离 worktree 执行，需经 `superpowers:using-git-worktrees` 创建。

## 现状基线（编写计划时已核实）

| 事实 | 值 |
|---|---|
| HEAD | `681b67ac1629c53f69dbcff613e69263bb5fe74f`（master，工作区干净） |
| 章节现状 | `book/chapters/17-high-performance-ebpf-firewall.md` 8268 字符，深度骨架 |
| 示例目录 | `book/examples/chapter-17/` 存在且为空 |
| 本机工具链 | clang 18.1.3、bpftool 7.7.0、gcc、libbpf-dev 1.3.0（`/usr/include/bpf/bpf_helpers.h` 存在）、Go 1.27.1、内核 7.0.0-31 |
| 权限 | 非 root；`/proc/sys/kernel/unprivileged_bpf_disabled` = 2（非特权不可加载） |
| BTF | `/sys/kernel/btf/vmlinux` 存在（`bpftool btf dump` 无特权可用） |
| 可复用模式 | `book/examples/chapter-09/`（README/Makefile/loader/check-env.sh 模式）、`09-netfilter-go/`（go.mod 固定 cilium/ebpf v0.16.0、Go+BPF 联合 Makefile） |

## 共享接口契约（跨任务定义，类型一致性以此为准）

各任务实现者只看自己的任务，以下契约必须逐字一致：

### C1. 统计计数下标 `enum stat_idx`（阶段 2 起定义，随阶段追加，顺序不得变）

```c
/* 阶段 2 版本 */                /* 阶段 3 追加 */        /* 阶段 4 追加 */
enum stat_idx {
    STAT_TOTAL = 0,              /* 不变 */               /* 不变 */
    STAT_PASS,                   /* 不变 */               /* 不变 */
    STAT_DROP_ACL,               /* 不变 */               /* 不变 */
    STAT_DROP_PARSE,             /* 不变 */               /* 不变 */
                                 STAT_CT_NEW,            /* 不变 */
                                 STAT_CT_ESTABLISHED,    /* 不变 */
                                 STAT_CT_FULL,           /* 不变 */
                                 STAT_NAT,
    STAT_MAX,
};
```

阶段 2：`STAT_MAX = 4`；阶段 3：`= 7`；阶段 4/5：`= 8`。Go 侧展示名固定为
`["total","pass","drop_acl","drop_parse","ct_new","ct_established","ct_full","nat"]`。

### C2. 数据结构（`fw_common.h` 演化；字段/字节序/pad 不得改动）

- `struct flow_key`：`__be32 src_ip; __be32 dst_ip; __be16 src_port; __be16 dst_port; __u8 proto; __u8 pad[3];`（16 字节，网络字节序）。
- `struct rule_value`：`__u8 action; __u8 pad[7];`（action 取 `enum fw_action { FW_ALLOW=0, FW_DROP=1 }`）。
- `struct lpm_key`：`__u32 prefixlen; __be32 addr;`（LPM trie 专用，key 侧定义）。
- `struct lpm_value`：`__u8 action; __u8 pad[7];`。
- 阶段 3 追加：`enum ct_state { CT_NEW=1, CT_ESTABLISHED=2, CT_FIN_WAIT=3, CT_CLOSED=4 }`；`struct ct_key`（与 flow_key 同构）；`struct ct_value { __u64 last_seen_ns; __u64 packets; __u64 bytes; __u8 state; __u8 pad[7]; }`。
- 阶段 4 追加：`struct nat_key`（与 flow_key 同构）；`struct nat_value { __be32 new_addr; __be16 new_port; __u8 is_snat; __u8 pad; }`（is_snat：1=改源，0=改目的）。
- Go 侧镜像结构（阶段 5，逐字节对应，网络字节序手工填充）：`FlowKey/RuleValue/LpmKey/CtKey/CtValue/NatKey/NatValue`，见 Task 7。

### C3. map 名（SEC(".maps")，同名必为同一实例）

| map | 类型 | max_entries | 出现阶段 |
|---|---|---|---|
| `stats` | `BPF_MAP_TYPE_PERCPU_ARRAY`，key `__u32`，value `__u64[STAT_MAX]` | `STAT_MAX` | 2–5 |
| `acl_rules` | `BPF_MAP_TYPE_HASH`，key `flow_key`，value `rule_value` | `MAX_RULES=1024` | 2、5 |
| `acl_lpm` | `BPF_MAP_TYPE_LPM_TRIE`，`map_flags BPF_F_NO_PREALLOC`，key `lpm_key`，value `lpm_value` | `MAX_LPM_KEYS=4096` | 2、5 |
| `conntrack` | `BPF_MAP_TYPE_LRU_HASH`，key `ct_key`，value `ct_value` | `CT_MAX_ENTRIES=65536` | 3、5 |
| `nat_map` | `BPF_MAP_TYPE_HASH`，key `nat_key`，value `nat_value` | `NAT_MAX_ENTRIES=4096` | 4、5 |

### C4. 程序与 SEC 名（决定 skeleton 符号前缀）

| 文件 | SEC / 函数 | skeleton 符号前缀 |
|---|---|---|
| `01-xdp-drop/xdp_drop.bpf.c` | `SEC("xdp") int xdp_drop(struct xdp_md *ctx)` | `xdp_drop_bpf` |
| `02-parse-acl/acl.bpf.c` | `SEC("xdp") int acl_prog(struct xdp_md *ctx)` | `acl_bpf` |
| `03-conntrack/conntrack.bpf.c` | `SEC("xdp") int conntrack_prog(struct xdp_md *ctx)` | `conntrack_bpf` |
| `04-nat-tc/nat.bpf.c` | `SEC("tc") int nat_ingress(struct __sk_buff *skb)` / `SEC("tc") int nat_egress(...)` | `nat_bpf` |
| `05-firewall/bpf/fw_xdp.bpf.c` | `SEC("xdp") int fw_xdp(struct xdp_md *ctx)` | —（cilium/ebpf 按函数名引用） |
| `05-firewall/bpf/fw_tc.bpf.c` | `SEC("tc") int fw_tc_ingress(...)` / `SEC("tc") int fw_tc_egress(...)` | — |

注意：**TC 程序不能靠 skeleton `__attach()` 自动挂载**，C 侧用 libbpf `bpf_tc_hook_create/bpf_tc_attach`（Task 6），Go 侧用 `tc filter replace ... pinned`（Task 7，兼容 5.10+，TCX 需 6.6+ 不采用）。

### C5. 实验拓扑与地址（`scripts/setup-veth.sh`，Task 8 建立后各任务引用）

```text
ns-client: veth-c    10.0.0.2/24  默认路由 -> 10.0.0.1
ns-fw:     veth-fw-c 10.0.0.1/24   veth-fw-s 10.0.2.1/24  ip_forward=1
ns-server: veth-s    10.0.2.2/24  默认路由 -> 10.0.2.1
测试流：ns-client -> ns-fw（XDP 挂 veth-fw-c；TC ingress DNAT 挂 veth-fw-c，
        TC egress SNAT 挂 veth-fw-s）-> ns-server
```

### C6. 验证状态约定（所有 README 与章节正文统一）

- **编译验证**：写明「已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3 / Go 1.27.1）`make` 通过」。
- **待运行验证**：写明「编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；『预期输出』为教学示例」。
- Go 依赖若网络不可用：标注「依赖待拉取，编译级待验证（需网络 `go mod tidy`）」。

### C7. Makefile 模板（阶段 1–4 与练习同构，仅替换目标名）

```makefile
# 第 17 章阶段 N 构建脚本（libbpf-bootstrap 模式，与第 9 章示例一致）。
# 用法：make（自动生成 vmlinux.h -> 编译 BPF -> 生成 skeleton -> 链接 loader）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool

INCLUDES := -I$(shell $(CLANG) -print-file-name=include)
CFLAGS := -g -Wall

.PHONY: all clean

all: <可执行目标名>

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

<名>.bpf.o: <名>.bpf.c vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) -c $< -o $@

<名>.skel.h: <名>.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

<可执行目标名>: <名>.c <名>.skel.h
	$(CC) $(CFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f *.o *.skel.h vmlinux.h <可执行目标名>
```

替换表：阶段 1 → `<名>=xdp_drop`，目标 `xdp_drop`；阶段 2 → `acl`；阶段 3 → `conntrack`；阶段 4 → `nat`；练习 01 → `acl`（目录内）；练习 04 → `ratelimit`。阶段 2–4 含本地头文件时，`.bpf.o` 规则追加依赖 `fw_common.h`。

### C8. 各阶段 README.md 统一结构（参照 `book/examples/chapter-09/01-hello-world/README.md`）

1. 标题 + 一句话定位（对应章节小节）　2. 演示目标　3. 文件说明（表）　4. 环境前提　5. 编译与运行（命令）　6. 预期输出（标注「预期输出示例 / 运行级待验证」）　7. 代码讲解要点（中文注释呼应）　8. 常见失败点（表：现象/原因与处理）　9. 验证状态（按 C6）　10. 参考来源。

---

### Task 1: STYLE.md 新增「实战章节教学模板」

- [x] Task 1: STYLE.md 新增「实战章节教学模板」

**tasks.md 条目**：§5.1.0「Update `book/STYLE.md` with the teaching template: five-layer environment narrative (dependency, build, deploy, run, effects), core-code principle-annotation rules, teacher-student dual-perspective sections, and the post-chapter exercise convention (`book/examples/<chapter>/exercises/` with reference implementations and verification status)」

**Files:**
- Modify: `book/STYLE.md`（在「验证标注」章节之前插入新章节）

**Interfaces:**
- Consumes: 设计文档 §5（四部分内容口径）。
- Produces: 后续所有任务的 README/正文写作模板依据；无代码接口。

- [x] **Step 1: 在 `## 验证标注` 之前插入以下完整章节**

```markdown
## 实战章节教学模板

实战章节（含独立示例资产的章节）在「完整章节模板」基础上，必须再满足以下四条教学规范。

### 1. 五层环境阐述

实战章节开篇必须按五层组织环境说明，每层都要落到具体命令、脚本或表格：

1. **依赖环境**：内核版本与 BTF 要求、root/`CAP_BPF`/`CAP_NET_ADMIN`/`CAP_PERFMON` 权限矩阵、
   veth 与网络命名空间前置条件、依赖工具清单与检查方法（对应示例 `00-env/check-env.sh`）。
2. **编译环境**：clang/LLVM、libbpf 头文件、bpftool、Go 工具链与依赖库的版本要求、安装与验证命令。
3. **部署环境**：实验拓扑设计（含图）、脚本化搭建与销毁步骤、安全警告（避免在物理网卡误挂 XDP/TC）。
4. **运行环境**：挂载与卸载方式、控制面命令总览、规则/配置下发流程。
5. **运行效果**：各阶段预期输出、抓包对比、状态查询结果示例、验证状态约定说明。

### 2. 核心代码原理注释规则

- 示例源码（`.bpf.c`、共享头、控制面源码）必须使用中文注释，覆盖：数据结构字段语义、
  verifier 边界检查原因、helper 选型原因、map 类型选型原因、失败模式。
- 章节正文中的代码块必须与 `book/examples/<chapter>/` 下源文件一一对应，并在代码块前后
  说明关键行；禁止出现正文里「计划写、建议实现」式表述，只允许对已落地实现的陈述式讲解。

### 3. 四段式阶段教学节奏

以渐进阶段推进的章节，每个阶段小节统一按四段展开：

1. **教师视角引入**：为什么需要这一步（承接上一阶段的痛点）、类比、常见误区与「读者容易卡住的地方」。
2. **核心代码原理讲解**：逐段讲设计原因、数据结构选型、helper 调用、verifier 约束、失败模式。
3. **学生动手**：构建命令、挂载/测试命令、预期输出示例（真实运行标注与预期标注必须区分）。
4. **检查点**：2–4 个自测问题 + 答案提示。

### 4. 课后扩展任务与参考实现约定

- 章末课后扩展任务存放在 `book/examples/<chapter>/exercises/<nn>-<name>/`，每项练习目录包含
  完整可编译源码、独立 `Makefile`（复用主示例构建体系）与 README。
- 练习 README 采用防剧透结构：任务描述与提示在前，参考实现思路独立成节放在后部。
- 每项练习必须标注验证状态：编译级「已验证/待验证」+ 运行级一律「待验证」。
```

- [x] **Step 2: 核对**

Run: `grep -c "^### " book/STYLE.md && grep -n "实战章节教学模板" book/STYLE.md`
Expected: 新章节 4 个三级标题；文件中只出现一处「实战章节教学模板」章节头。

- [x] **Step 3: 提交**

```bash
git add book/STYLE.md
git commit -m "docs(ch17): STYLE.md 新增实战章节教学模板（五层环境/四段式/练习约定）"
```

---

### Task 2: 章级 README 与 00-env 环境自检

- [x] Task 2: 章级 README 与 00-env 环境自检

**tasks.md 条目**：§5.1.2「Generate chapter-level `book/examples/chapter-17/README.md` with the stage table (stage, datapath focus, learning outcome, attach point, output mechanism), learning path, safety warnings, and verification-status conventions.」＋「Generate `book/examples/chapter-17/00-env/check-env.sh` with graded environment diagnostics (kernel/BTF, toolchain, privileges) that runs and reports meaningfully without root.」

**Files:**
- Create: `book/examples/chapter-17/README.md`
- Create: `book/examples/chapter-17/00-env/check-env.sh`（可执行，`chmod +x`）

**Interfaces:**
- Consumes: 共享契约 C5（拓扑/地址）、C6（验证状态约定）。
- Produces: `check-env.sh` 输出三档结论（编译级/加载级/实验级），供所有阶段 README「环境前提」引用；`00-env/` 无 Makefile（纯脚本）。

- [x] **Step 1: 创建 `book/examples/chapter-17/README.md`**

结构对齐 `book/examples/chapter-09/README.md`，内容如下（表格逐字固定）：

```markdown
# 第 17 章独立示例资产

本目录是《第 17 章 实战：构建高性能 eBPF 防火墙》的配套示例代码，
按渐进搭建式教学组织为 6 个阶段（00–05），每个阶段独立可编译、独立成篇。

## 目录结构

| 目录 | 阶段主题 | 数据面重点 | 学到什么 | 挂载点 | 输出机制 |
|---|---|---|---|---|---|
| `00-env/` | 环境自检 | - | 权限分级与工具链诊断 | - | - |
| `01-xdp-drop/` | 最小 XDP 丢弃 | `XDP_DROP`/`XDP_PASS` | XDP 挂载位置、最小闭环 | XDP（veth-fw-c） | per-CPU stats map |
| `02-parse-acl/` | 报文解析 + ACL | 边界检查解析 + hash 精确 + LPM CIDR | 五元组、两类 map 查找 | XDP | stats map |
| `03-conntrack/` | 连接跟踪 | 正反 key 状态机 + LRU | 有状态防火墙 | XDP | stats + conntrack dump |
| `04-nat-tc/` | TC NAT | SNAT/DNAT + 增量校验和 | XDP/TC 分工、改写顺序 | TC clsact ingress/egress | stats map |
| `05-firewall/` | 完整系统 | XDP 防火墙 + TC NAT | 生产级数据面/控制面分离 | XDP + TC | pin map + Go CLI |
| `scripts/` | 拓扑与验证 | - | 实验方法学 | - | - |
| `exercises/` | 课后扩展任务 | 4 项完整参考实现 | 见章末「课后扩展任务」 | - | - |

## 学习路径

1. `bash 00-env/check-env.sh` 自检（无 root 也能给出三档结论）。
2. 按 01 → 05 顺序推进；每个阶段目录内 `make` 后阅读其 README 的「编译与运行」。
3. 运行/压测需要 root + `scripts/setup-veth.sh` 拓扑；只有编译验证可在无特权机器完成。

## 安全警告

- XDP/TC 程序会直接丢包或改包：**只挂载到 `setup-veth.sh` 创建的 veth 接口，
  绝不要挂到物理网卡或 SSH 使用的接口**，否则可能立即失联。
- NAT 实验会改写报文内容；实验完毕执行 `sudo ./fwctl detach` 与 `setup-veth.sh teardown` 清理。

## 统一构建模式

阶段 01–04 与练习的 C 侧构建采用 libbpf-bootstrap 三步（各目录内直接 `make`）：

vmlinux.h（bpftool 由 /sys/kernel/btf/vmlinux 生成） -> *.bpf.o（clang -target bpf）
    -> *.skel.h（bpftool gen skeleton） -> 可执行 loader（gcc -lbpf -lelf -lz）

阶段 05 为 BPF + Go 联合构建（`make` 一次完成，见其 Makefile）。

## 验证状态说明

遵循 `book/STYLE.md` 约定：

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3、Go 1.27.1）
  通过 `scripts/verify-build.sh` 全量编译（结果见各 README 验证状态区）。
- **待运行验证**：加载/挂载/NAT/压测需要特权内核环境；编写环境为非 root
  （`unprivileged_bpf_disabled=2`），各 README 中「预期输出」均为教学示例。
```

- [x] **Step 2: 创建 `book/examples/chapter-17/00-env/check-env.sh`**

分级诊断脚本（无 root 可运行且输出有意义），完整内容：

```bash
#!/usr/bin/env bash
# 第 17 章示例环境自检（分级诊断）。
# 用法：bash check-env.sh（无 root 也可运行，按「编译级/加载级/实验级」三档给出结论）

set -u
fail=0

check() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        printf 'OK    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        fail=1
    fi
}

echo "== 内核与 BTF =="
printf 'kernel: %s\n' "$(uname -r)"
if [ -r /sys/kernel/btf/vmlinux ]; then
    echo 'OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用，CO-RE 可用）'
else
    echo 'FAIL  内核 BTF 缺失：全部示例无法编译，参见附录 C'
    fail=1
fi

echo "== 工具链（编译级） =="
check 'clang（编译 BPF C）'              clang --version
check 'gcc（编译用户态 loader）'          gcc --version
check 'bpftool（BTF dump / skeleton）'   bpftool version
check 'make'                             make --version
check 'libbpf 头文件（libbpf-dev）'       test -f /usr/include/bpf/bpf_helpers.h
check 'Go（仅 05-firewall 与控制面练习需要）' go version
check 'ip/tc（拓扑与 TC 挂载，运行级）'    bash -c 'command -v ip && command -v tc'

echo "== 权限分级（加载级/实验级） =="
if [ "$(id -u)" -eq 0 ]; then
    echo 'OK    当前为 root：可加载、可挂载、可做 veth 实验（仍勿用物理网卡）'
elif capsh --print 2>/dev/null | grep -q 'cap_bpf'; then
    echo 'OK    具备 CAP_BPF：可加载程序，实验还需 CAP_NET_ADMIN/CAP_SYS_ADMIN'
else
    ubd="$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || echo 未知)"
    echo "WARN  非 root 且无 CAP_BPF（unprivileged_bpf_disabled=${ubd}）："
    echo '      - 可以完成：编译级验证（make / verify-build.sh）'
    echo '      - 不能完成：BPF 加载、XDP/TC 挂载、veth 拓扑（需 sudo 或 CAP_BPF+CAP_NET_ADMIN）'
fi

echo
if [ "$fail" -eq 0 ]; then
    echo '结论：编译级依赖齐备；加载级/实验级能力见上方权限分级输出。'
else
    echo '结论：存在 FAIL 项，先补齐依赖（Ubuntu: sudo apt install clang llvm gcc bpftool libbpf-dev make golang）。'
fi
exit "$fail"
```

- [x] **Step 3: 本机验证（无特权可执行）**

Run: `chmod +x book/examples/chapter-17/00-env/check-env.sh && bash book/examples/chapter-17/00-env/check-env.sh`
Expected: 退出码 0；工具链各行 OK；权限分级输出 WARN 段（本机非 root）并明示「可以完成编译级验证」。

- [x] **Step 4: 提交**

```bash
git add book/examples/chapter-17/README.md book/examples/chapter-17/00-env/
git commit -m "feat(ch17): 章级 README 与 00-env 分级环境自检"
```

---

### Task 3: 阶段 1 —— 01-xdp-drop 最小 XDP 丢弃

- [x] Task 3: 阶段 1 —— 01-xdp-drop 最小 XDP 丢弃

**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/01-xdp-drop/` with `xdp_drop.bpf.c`, C loader, Makefile, and README, with Chinese comments explaining XDP attach point, action return values, and the minimal load-verify-JIT-attach loop.」

**Files:**
- Create: `book/examples/chapter-17/01-xdp-drop/xdp_drop.bpf.c`
- Create: `book/examples/chapter-17/01-xdp-drop/xdp_drop.c`
- Create: `book/examples/chapter-17/01-xdp-drop/Makefile`
- Create: `book/examples/chapter-17/01-xdp-drop/README.md`

**Interfaces:**
- Consumes: C7 Makefile 模板（`<名>=xdp_drop`）；C4 SEC 名。
- Produces: 阶段 1 loader 骨架（`handle_sig`/轮询循环/`sum_percpu`），阶段 2–4 loader 复用；skeleton 符号前缀 `xdp_drop_bpf`。

- [x] **Step 1: 创建 `xdp_drop.bpf.c`**（丢弃全部 IPv4、放行其余，per-CPU 统计可观测）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 1：最小 XDP 丢弃示例。
 *
 * 闭环：XDP 挂载点（网卡驱动收包最早处）-> 动作返回码 -> loader 轮询 map 观察效果。
 * 行为：丢弃所有 IPv4 报文（XDP_DROP），放行 ARP 等（XDP_PASS），per-CPU 数组计数
 *       （pass 只计实际放行的报文，pass 与 drop 互斥）。
 *
 * 返回值语义（include/uapi/linux/bpf.h 中 enum xdp_action）：
 *   XDP_DROP(1)  在驱动层直接丢弃，不生成 skb、不进协议栈；
 *   XDP_PASS(2)  交给内核协议栈继续处理；
 *   XDP_TX(4)/XDP_REDIRECT(6) 本章暂不使用。
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

enum { STAT_PASS = 0, STAT_DROP };

/* 选 per-CPU 数组：每 CPU 独立副本，数据面更新无锁、零竞争；loader 侧再求和 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

/* 小工具：计数 +1；lookup 失败静默跳过，不影响主路径 */
static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

SEC("xdp")
int xdp_drop(struct xdp_md *ctx)
{
    /* xdp_md 的 data/data_end 描述报文起止； verifier 只认显式边界检查，
       「人看起来安全」的代码一律拒绝加载 */
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    /* Ethernet 头固定 14 字节；必须在解引用前确认 eth+1 不越过 data_end */
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        bump(STAT_PASS);          /* 残包交协议栈：计入实际放行 */
        return XDP_PASS;
    }

    if (eth->h_proto == bpf_htons(ETH_P_IP)) {
        bump(STAT_DROP);
        return XDP_DROP;          /* 教学演示：IPv4 全部丢弃 */
    }
    bump(STAT_PASS);              /* 非 IPv4（ARP 等）实际放行：计入 pass */
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";   /* GPL 兼容声明，必需 */
```

- [x] **Step 2: 创建 `xdp_drop.c`**（阶段 1–4 loader 骨架，后续任务复用其 `handle_sig`/`sum_percpu`）

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 1 用户态 loader（libbpf-bootstrap 风格）。
 * 用法: sudo ./xdp_drop -i <接口名>
 * 职责: open_and_load（加载+verifier 校验+JIT）-> attach XDP -> 每秒轮询 stats。
 * 注意: 非 root（unprivileged_bpf_disabled=2）时 open_and_load 返回 NULL，属预期。
 */
#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "xdp_drop.skel.h"

static volatile sig_atomic_t exiting = 0;
static void handle_sig(int sig) { exiting = 1; }   /* 阶段 1–4 loader 共用 */

/* per-CPU map 单键求和：bpf_map_lookup_elem 一次返回 nr_cpus 份 value */
static __u64 sum_percpu(int fd, __u32 key)
{
    __u64 buf[64] = {};                 /* 教学示例假设 CPU <= 64 */
    int n = libbpf_num_possible_cpus();
    if (n > 64 || bpf_map_lookup_elem(fd, &key, buf))
        return 0;
    __u64 sum = 0;
    for (int i = 0; i < n; i++)
        sum += buf[i];
    return sum;
}

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "-i")) {
        fprintf(stderr, "用法: %s -i <接口名>\n", argv[0]);
        return 1;
    }
    int ifindex = if_nametoindex(argv[2]);
    if (!ifindex) {
        fprintf(stderr, "接口 %s 不存在: %s\n", argv[2], strerror(errno));
        return 1;
    }

    /* 加载 = 打开对象 -> verifier 校验 -> JIT；失败常见原因：权限不足、verifier 拒绝 */
    struct xdp_drop_bpf *skel = xdp_drop_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "open_and_load 失败（需 root/CAP_BPF，或 verifier 拒绝）\n");
        return 1;
    }

    /* XDP 挂载点无法从 SEC 名推断到具体接口，必须显式 attach；
       libbpf 1.x 失败返回 ERR_PTR（非 NULL），必须用 libbpf_get_error 判错 */
    struct bpf_link *link = bpf_program__attach_xdp(skel->progs.xdp_drop, ifindex);
    if (libbpf_get_error(link)) {
        fprintf(stderr, "attach_xdp 失败（接口是否为 veth？勿挂物理网卡！）\n");
        xdp_drop_bpf__destroy(skel);
        return 1;
    }
    printf("XDP 程序已挂载到 %s（ifindex=%d），Ctrl-C 退出。\n", argv[2], ifindex);

    int stats_fd = bpf_map__fd(skel->maps.stats);
    while (!exiting) {
        printf("pass=%llu drop=%llu\n",
               (unsigned long long)sum_percpu(stats_fd, STAT_PASS),
               (unsigned long long)sum_percpu(stats_fd, STAT_DROP));
        sleep(1);
    }

    xdp_drop_bpf__destroy(skel);   /* close(fd) + 解除引用，程序随进程退出卸载 */
    return 0;
}
```

- [x] **Step 3: 创建 `Makefile`** —— 使用共享契约 C7 模板，替换 `<名>=xdp_drop`、目标 `xdp_drop`。

- [x] **Step 4: 编译验证（本机真实执行，硬性）**

Run: `make -C book/examples/chapter-17/01-xdp-drop`
Expected: 退出码 0，生成 `xdp_drop`；`clang -target bpf` 与 `bpftool gen skeleton` 无告警。
失败即设计缺陷：修代码后重跑，直至通过。

- [x] **Step 5: 创建 `README.md`** —— 按 C8 十段结构。关键口径：
  - 演示目标：第一个「加载→verifier→JIT→挂载→触发」闭环。
  - 编译与运行：`make`；`sudo ./xdp_drop -i veth-fw-c`（需先建拓扑，指向 `../scripts/setup-veth.sh`）；触发：`sudo ip netns exec ns-client ping 10.0.2.2`。
  - 预期输出示例（标注运行级待验证）：`pass=0 drop=12`（ping 时 drop 递增）。
  - 常见失败点：非 root 加载失败（预期，指向 check-env.sh）；挂物理网卡风险警告；`vmlinux.h` 缺失。
  - 验证状态：按 C6。

- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-17/01-xdp-drop/
git commit -m "feat(ch17): 阶段1 最小 XDP 丢弃（BPF+loader+Makefile+README）"
```

---

### Task 4: 阶段 2 —— 02-parse-acl 报文解析与 ACL

- [x] Task 4: 阶段 2 —— 02-parse-acl 报文解析与 ACL

**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/02-parse-acl/` with staged `fw_common.h`, `acl.bpf.c` (Ethernet/IPv4/TCP/UDP parsing + hash-map exact rules + LPM-trie CIDR blacklist/whitelist), rule-provisioning loader, Makefile, and README, with Chinese comments explaining parser bounds checks, five-tuple normalization, and map design.」

**Files:**
- Create: `book/examples/chapter-17/02-parse-acl/fw_common.h`
- Create: `book/examples/chapter-17/02-parse-acl/acl.bpf.c`
- Create: `book/examples/chapter-17/02-parse-acl/acl.c`
- Create: `book/examples/chapter-17/02-parse-acl/Makefile`
- Create: `book/examples/chapter-17/02-parse-acl/README.md`

**Interfaces:**
- Consumes: C1/C2/C3 契约（阶段 2 版本）；Task 3 的 loader 骨架（`handle_sig`/`sum_percpu` 逐字复制）。
- Produces: `parse_flow(ctx, key)`（阶段 3 扩展为带 `__u8 *tcp_flags` 出参）；`bump(idx)`；`fw_common.h` 阶段 2 版本；skeleton 前缀 `acl_bpf`。

- [x] **Step 1: 创建 `fw_common.h`（阶段 2 版本）**

```c
/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * 阶段 2 共享头：报文解析与 ACL 所需结构。
 * 阶段 3/4 按需扩展（conntrack/NAT），阶段 5 汇总为完整并集；
 * 字段语义/字节序/pad 一经定义不再改动（正文与 Go 控制面按此契约镜像）。
 */
#ifndef FW_COMMON_H
#define FW_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define MAX_RULES    1024   /* hash 精确规则上限：防 map 膨胀，超限下发直接失败 */
#define MAX_LPM_KEYS 4096   /* LPM trie 条目上限 */
#define STAT_MAX     4      /* 阶段 2 统计计数个数（阶段 3/4 追加） */

/* per-CPU 统计下标（顺序即契约，Go 侧按顺序展示） */
enum stat_idx {
    STAT_TOTAL = 0,     /* 成功解析出五元组的报文数 */
    STAT_PASS,          /* 放行 */
    STAT_DROP_ACL,      /* ACL 命中拒绝 */
    STAT_DROP_PARSE,    /* 保留（阶段 2 暂不使用，残包按 PASS 处理） */
    STAT_MAX,
};

/* 五元组 key：IP/端口一律网络字节序存储（原样存原样查即可匹配，无需主机序转换）；
 * pad[3] 使结构体 4 字节对齐，哈希与字节级比较行为确定 */
struct flow_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

enum fw_action {
    FW_ALLOW = 0,
    FW_DROP  = 1,
};

/* hash 精确规则 value */
struct rule_value {
    __u8 action;        /* enum fw_action */
    __u8 pad[7];
};

/* LPM trie key：前缀长度（bit）+ 地址；LPM 要求 key 以 prefixlen 开头 */
struct lpm_key {
    __u32 prefixlen;
    __be32 addr;
};

/* LPM value：最长前缀命中的动作（黑/白名单由 action 区分，网段越具体优先级越高） */
struct lpm_value {
    __u8 action;
    __u8 pad[7];
};

#endif /* FW_COMMON_H */
```

- [x] **Step 2: 创建 `acl.bpf.c`**

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 2 数据面：Ethernet/IPv4/TCP/UDP 解析 + ACL 判定。
 * 查找优先级（正文与 README 必须同口径）：
 *   1) acl_rules  精确五元组（最具体） -> 命中即返回其 action；
 *   2) acl_lpm    源地址 CIDR 最长前缀匹配（黑/白名单由 action 区分）；
 *   3) 默认 FW_ALLOW（教学基线；生产默认策略应可配置）。
 */
#include "fw_common.h"

/* hash：key 全等匹配，O(1)；适合精确封禁，不适合网段/端口范围 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, MAX_RULES);
    __type(key, struct flow_key);
    __type(value, struct rule_value);
} acl_rules SEC(".maps");

/* LPM trie：按最长前缀匹配网段；BPF_F_NO_PREALLOC 必需（节省内存、允许大 max_entries） */
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __uint(max_entries, MAX_LPM_KEYS);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, struct lpm_key);
    __type(value, struct lpm_value);
} acl_lpm SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

/*
 * 逐层解析：每访问一层头之前都做一次边界检查。
 * verifier 把 (eth+1) > data_end 这类比较当作「指针定界」证据；少写一次就加载失败。
 * 成功填充 *key 并返回 1；非 IPv4/TCP/UDP 或残包返回 0。
 */
static __always_inline int parse_flow(struct xdp_md *ctx, struct flow_key *key)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return 0;
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return 0;                       /* ARP/IPv6 等不在本章策略范围 */

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return 0;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return 0;

    /* IPv4 头长以 4 字节为单位（ihl 最小 5=20B）；跳过可选字段前必须重查边界 */
    __u32 ihl_len = ip->ihl * 4;
    if (ihl_len < sizeof(*ip))
        return 0;
    void *l4 = (void *)ip + ihl_len;

    key->src_ip  = ip->saddr;           /* 网络字节序原样保存 */
    key->dst_ip  = ip->daddr;
    key->proto   = ip->protocol;
    key->pad[0] = key->pad[1] = key->pad[2] = 0;

    if (ip->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end)
            return 0;
        key->src_port = tcp->source;
        key->dst_port = tcp->dest;
    } else {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end)
            return 0;
        key->src_port = udp->source;
        key->dst_port = udp->dest;
    }
    return 1;
}

static __always_inline __u8 acl_verdict(const struct flow_key *flow)
{
    struct rule_value *exact = bpf_map_lookup_elem(&acl_rules, flow);
    if (exact)
        return exact->action;

    /* LPM 查找 key 的 prefixlen 固定 32：按主机地址查，由 trie 返回最长前缀规则 */
    struct lpm_key lk = { .prefixlen = 32, .addr = flow->src_ip };
    struct lpm_value *lv = bpf_map_lookup_elem(&acl_lpm, &lk);
    if (lv)
        return lv->action;

    return FW_ALLOW;
}

SEC("xdp")
int acl_prog(struct xdp_md *ctx)
{
    struct flow_key flow;
    if (!parse_flow(ctx, &flow)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    if (acl_verdict(&flow) == FW_DROP) {
        bump(STAT_DROP_ACL);
        return XDP_DROP;
    }
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 3: 创建 `acl.c`**（loader + 命令行规则下发；复制 Task 3 的 `handle_sig`/`sum_percpu`，新增解析下发）

用法固定（README 与正文同口径）：

```text
sudo ./acl -i veth-fw-c --rule drop,tcp,10.0.0.2,5201,10.0.2.2,5201 --cidr drop,10.0.0.0/24
可重复 --rule/--cidr；随后 attach XDP 并每 2 秒打印统计。
```

核心新增代码（其余与 Task 3 loader 相同）：

```c
/* 解析 --rule <action>,<proto>,<src>,<sport>,<dst>,<dport>（精确五元组，不支持通配） */
static int parse_rule_arg(const char *s, struct flow_key *k, struct rule_value *v)
{
    char action[8], proto[8], src[64], dst[64];
    unsigned sport, dport;
    memset(k, 0, sizeof(*k));
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%7[^,],%7[^,],%63[^,],%u,%63[^,],%u",
               action, proto, src, &sport, dst, &dport) != 6)
        return -1;
    if (sport > 65535 || dport > 65535)
        return -1;
    if (inet_pton(AF_INET, src, &k->src_ip) != 1 ||
        inet_pton(AF_INET, dst, &k->dst_ip) != 1)
        return -1;                       /* inet_pton 得到的已是网络字节序 */
    k->src_port = htons(sport);
    k->dst_port = htons(dport);
    k->proto = strcmp(proto, "tcp") ? IPPROTO_UDP : IPPROTO_TCP;
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}

/* 解析 --cidr <action>,<cidr>（源地址网段，写 LPM map） */
static int parse_cidr_arg(const char *s, struct lpm_key *k, struct lpm_value *v)
{
    char action[8], cidr[64], *slash;
    memset(k, 0, sizeof(*k));
    memset(v, 0, sizeof(*v));
    if (sscanf(s, "%7[^,],%63s", action, cidr) != 2)
        return -1;
    if ((slash = strchr(cidr, '/')) == NULL)
        return -1;
    *slash = '\0';
    k->prefixlen = (unsigned char)atoi(slash + 1);
    if (k->prefixlen > 32 || inet_pton(AF_INET, cidr, &k->addr) != 1)
        return -1;
    v->action = strcmp(action, "drop") ? FW_ALLOW : FW_DROP;
    return 0;
}
```

`main()` 流程：解析 `-i` 与全部 `--rule/--cidr` → `acl_bpf__open_and_load()` → 遍历参数把规则写入 `skel->maps.acl_rules` / `skel->maps.acl_lpm`（`bpf_map_update_elem`，失败报「规则数超上限或格式错误」）→ `bpf_program__attach_xdp` → 每 2 秒打印 4 个统计项（`STAT_TOTAL/PASS/DROP_ACL/DROP_PARSE`）。

- [x] **Step 4: 创建 `Makefile`** —— C7 模板替换 `<名>=acl`，目标 `acl`；`.bpf.o` 规则依赖追加 `fw_common.h`。

- [x] **Step 5: 编译验证（硬性）**

Run: `make -C book/examples/chapter-17/02-parse-acl`
Expected: 退出码 0。

- [x] **Step 6: 创建 `README.md`**（C8 结构）。要点：
  - 预期输出示例（运行级待验证）：下发 `--cidr drop,10.0.0.0/24` 后 `total=512 drop_acl=512 pass=0`。
  - 动手顺序：建拓扑 → 服务端 `iperf3 -s -p 5201` → 客户端 `iperf3 -c 10.0.2.2 -p 5201` → 用固定端口五元组下发 `--rule` 观察丢包。
  - 讲解要点：字节序「存什么查什么」、hash vs LPM 选型、`BPF_F_NO_PREALLOC` 原因、`ihl` 处理。
  - 验证状态按 C6；参考来源：libbpf LPM 文档、内核 `map` 文档。

- [x] **Step 7: 提交**

```bash
git add book/examples/chapter-17/02-parse-acl/
git commit -m "feat(ch17): 阶段2 报文解析与 ACL（hash 精确 + LPM CIDR）"
```

---

### Task 5: 阶段 3 —— 03-conntrack 连接跟踪与 LRU

- [x] Task 5: 阶段 3 —— 03-conntrack 连接跟踪与 LRU

**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/03-conntrack/` with extended `fw_common.h`, `conntrack.bpf.c` (forward/reverse keys, state machine, LRU cap), dump-capable loader, Makefile, and README, with Chinese comments explaining state transitions and map capacity pitfalls.」

**Files:**
- Create: `book/examples/chapter-17/03-conntrack/fw_common.h`
- Create: `book/examples/chapter-17/03-conntrack/conntrack.bpf.c`
- Create: `book/examples/chapter-17/03-conntrack/conntrack.c`
- Create: `book/examples/chapter-17/03-conntrack/Makefile`
- Create: `book/examples/chapter-17/03-conntrack/README.md`

**Interfaces:**
- Consumes: 阶段 2 全部（`parse_flow`、map 模式）；C1 枚举追加 CT_*；C3 conntrack LRU。
- Produces: `parse_flow(ctx, key, tcp_flags)` 三参版本（阶段 5 沿用）；`ct_key` 构造函数 `fill_ct_key()`；skeleton 前缀 `conntrack_bpf`。

- [x] **Step 1: 创建 `fw_common.h`（阶段 3 版本 = 阶段 2 全文 + 以下追加）**

```c
/* ===== 阶段 3 追加：连接跟踪 ===== */
#define CT_MAX_ENTRIES 65536    /* LRU 上限：满后内核静默淘汰最久未用项，新连接登记仍成功 */

/* 最小可用 TCP 状态机（不复刻 netfilter conntrack 全量语义） */
enum ct_state {
    CT_NEW         = 1,   /* 首包登记（TCP SYN 或 UDP 首包） */
    CT_ESTABLISHED = 2,   /* 看到反向报文 */
    CT_FIN_WAIT    = 3,   /* 收到 FIN，等待对端收尾 */
    CT_CLOSED      = 4,   /* 收到 RST 或双向收尾；控制面超时后可清理 */
};

/* conntrack key：与 flow_key 同构；正反两个方向各构成一个 key */
struct ct_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

/* 时间戳用 bpf_ktime_get_ns()（单调时钟，非墙钟）；超时回收由控制面完成 */
struct ct_value {
    __u64 last_seen_ns;
    __u64 packets;
    __u64 bytes;
    __u8  state;          /* enum ct_state */
    __u8  pad[7];
};
```

并把 `enum stat_idx` 扩为 `STAT_TOTAL/PASS/DROP_ACL/DROP_PARSE/CT_NEW/CT_ESTABLISHED/CT_FULL/STAT_MAX`（`STAT_MAX=7`，注释各加「阶段 3 新增」）。

- [x] **Step 2: 创建 `conntrack.bpf.c`**（阶段 2 的 map/`bump`/`acl_verdict` 不需要——本阶段只观测，不判 ACL；`parse_flow` 升级为三参）

```c
/* 阶段 3：parse_flow 增加出参 tcp_flags（仅 TCP 有效；位含义见下方宏） */
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_ACK 0x08
/* TCP 分支内新增：
 *   *tcp_flags = (tcp->fin ? TCP_FLAG_FIN : 0) | (tcp->syn ? TCP_FLAG_SYN : 0)
 *              | (tcp->rst ? TCP_FLAG_RST : 0) | (tcp->ack ? TCP_FLAG_ACK : 0);
 * 非 TCP 分支 *tcp_flags = 0。位域访问在 x86_64/arm64 小端上安全，注释注明此前提。 */
```

map 与主程序：

```c
/* LRU hash：容量固定，满时自动淘汰最久未用项——避免表无限增长拖垮内存，
 * 代价是「淘汰是静默的」，只能靠统计与控制面观测 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, CT_MAX_ENTRIES);
    __type(key, struct ct_key);
    __type(value, struct ct_value);
} conntrack SEC(".maps");

static __always_inline void fill_ct_key(struct ct_key *k, const struct flow_key *f, __u8 reverse)
{
    k->src_ip   = reverse ? f->dst_ip   : f->src_ip;
    k->dst_ip   = reverse ? f->src_ip   : f->dst_ip;
    k->src_port = reverse ? f->dst_port : f->src_port;
    k->dst_port = reverse ? f->src_port : f->dst_port;
    k->proto    = f->proto;
    k->pad[0] = k->pad[1] = k->pad[2] = 0;
}

SEC("xdp")
int conntrack_prog(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __u64 pkt_len = data_end - data;

    struct flow_key flow;
    __u8 flags = 0;
    if (!parse_flow(ctx, &flow, &flags)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    bump(STAT_TOTAL);

    struct ct_key fwd, rev;
    fill_ct_key(&fwd, &flow, 0);
    fill_ct_key(&rev, &flow, 1);
    __u64 now = bpf_ktime_get_ns();

    /* 先查正向（报文自身方向），再查反向（回包方向） */
    struct ct_value *ct = bpf_map_lookup_elem(&conntrack, &fwd);
    __u8 from_reply = 0;
    if (!ct) {
        ct = bpf_map_lookup_elem(&conntrack, &rev);
        from_reply = 1;
    }

    if (ct) {
        /* 多 CPU 同时更新同一表项存在写竞争（后写覆盖）；教学实现接受该近似，
           正文与注释必须说明，生产可改 per-CPU 计量或 spin_lock */
        ct->last_seen_ns = now;
        ct->packets++;
        ct->bytes += pkt_len;
        if (ct->state == CT_NEW && from_reply) {
            ct->state = CT_ESTABLISHED;          /* 反向报文 = 连接建立 */
            bump(STAT_CT_ESTABLISHED);
        }
        if (flow.proto == IPPROTO_TCP) {
            if (flags & TCP_FLAG_RST)
                ct->state = CT_CLOSED;
            else if (flags & TCP_FLAG_FIN)
                ct->state = CT_FIN_WAIT;
        }
        bump(STAT_PASS);
        return XDP_PASS;                         /* 阶段 3 只观测，不做策略 */
    }

    /* 新连接登记：TCP 必须从 SYN 开始（非 SYN 且无记录的中途报文跳过登记） */
    if (flow.proto == IPPROTO_TCP && !(flags & TCP_FLAG_SYN)) {
        bump(STAT_PASS);
        return XDP_PASS;
    }
    struct ct_value nv = {
        .state = CT_NEW,
        .last_seen_ns = now,
        .packets = 1,
        .bytes = pkt_len,
    };
    if (bpf_map_update_elem(&conntrack, &fwd, &nv, BPF_NOEXIST) == 0)
        bump(STAT_CT_NEW);
    else
        bump(STAT_CT_FULL);   /* 非 0 = 同 key 并发登记竞争（-EEXIST）或分配失败：放行并计数。
                                 LRU 满表不走此分支——内核静默淘汰最久未用项后插入仍成功
                                 （LRU「不阻塞新流」的设计行为）；是否对失败 DROP 由正文讨论取舍 */
    bump(STAT_PASS);
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 3: 创建 `conntrack.c`**：Task 3 loader 骨架（头文件追加 `<arpa/inet.h>` 与 `<time.h>`）+ 每 2 秒 dump：

```c
/* bpf_ktime_get_ns 基于 CLOCK_MONOTONIC；用户态取同源单调时钟对齐时间基准 */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* 遍历是「无快照保证」的弱遍历：遍历期间表项可能被 LRU 淘汰，读到啥打印啥 */
static void dump_conntrack(int fd)
{
    struct ct_key cur = {}, next;
    struct ct_value v;
    while (bpf_map_get_next_key(fd, &cur, &next) == 0) {
        if (bpf_map_lookup_elem(fd, &next, &v) == 0) {
            struct in_addr s = { .s_addr = next.src_ip }, d = { .s_addr = next.dst_ip };
            /* 不能在一条 printf 里调用两次 inet_ntoa：它返回线程内静态缓冲区，
             * 第二次调用会覆盖第一次的结果（两个 IP 打成同一个），
             * 故改用可重入的 inet_ntop 各自写入独立缓冲区 */
            char sstr[INET_ADDRSTRLEN], dstr[INET_ADDRSTRLEN];
            printf("%-15s:%-5u -> %-15s:%-5u proto=%u state=%u idle=%.1fs pkts=%llu\n",
                   inet_ntop(AF_INET, &s, sstr, sizeof(sstr)), ntohs(next.src_port),
                   inet_ntop(AF_INET, &d, dstr, sizeof(dstr)), ntohs(next.dst_port),
                   next.proto, v.state,
                   now_s() - v.last_seen_ns / 1e9,
                   (unsigned long long)v.packets);
        }
        cur = next;
    }
}
```

- [x] **Step 4: `Makefile`**（C7，`<名>=conntrack`）。**Step 5: 编译验证** `make -C book/examples/chapter-17/03-conntrack` 退出码 0。
- [x] **Step 6: `README.md`**（C8）。预期输出示例（运行级待验证）：iperf3 建流后 dump 出一条 `state=1`，客户端 FIN 后同一条目推进为 `state=3 idle=...`；单一挂载点看不到反向报文、`state=2` 在本拓扑不可复现——「挂载点决定数据面可见性」写成本阶段教学点（控制器裁定 2026-09-25）。讲解要点：正反 key 互换规则、LRU 静默淘汰、写竞争近似、超时回收归控制面。验证状态 C6。
- [x] **Step 7: 提交** `git commit -m "feat(ch17): 阶段3 连接跟踪（正反 key + 状态机 + LRU + dump）"`

---

### Task 6: 阶段 4 —— 04-nat-tc TC 路径 SNAT/DNAT 与增量校验和

- [x] Task 6: 阶段 4 —— 04-nat-tc TC 路径 SNAT/DNAT 与增量校验和

**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/04-nat-tc/` with extended `fw_common.h`, `nat.bpf.c` (TC clsact, SNAT/DNAT rewrite, incremental checksum), NAT-provisioning loader, Makefile, and README, with Chinese comments explaining rewrite order and checksum algorithms.」

**Files:**
- Create: `book/examples/chapter-17/04-nat-tc/fw_common.h`
- Create: `book/examples/chapter-17/04-nat-tc/nat.bpf.c`
- Create: `book/examples/chapter-17/04-nat-tc/nat.c`
- Create: `book/examples/chapter-17/04-nat-tc/Makefile`
- Create: `book/examples/chapter-17/04-nat-tc/README.md`

**Interfaces:**
- Consumes: C1（追加 STAT_NAT，STAT_MAX=8）、C3（nat_map）；Task 3 loader 骨架。
- Produces: `do_nat(skb, dir)` 改写核心（阶段 5 fw_tc 复用）；TC attach 模式 `bpf_tc_hook_create + bpf_tc_attach`；skeleton 前缀 `nat_bpf`，程序 `nat_ingress`（DNAT）/`nat_egress`（SNAT）。

- [x] **Step 1: 创建 `fw_common.h`（阶段 4 版本 = 阶段 3 全文 + 以下追加；STAT_MAX=8，追加 `STAT_NAT`）**

```c
/* ===== 阶段 4 追加：NAT 映射 ===== */
#define NAT_MAX_ENTRIES 4096

struct nat_key {
    __be32 src_ip;
    __be32 dst_ip;
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;
    __u8   pad[3];
};

/* is_snat：1=改源（SNAT，egress）0=改目的（DNAT，ingress）；
 * 数据面用 dir 过滤：DNAT 程序只认 is_snat==0 的表项，反之亦然 */
struct nat_value {
    __be32 new_addr;
    __be16 new_port;
    __u8   is_snat;
    __u8   pad;
};
```

- [x] **Step 2: 创建 `nat.bpf.c`**

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * 阶段 4 数据面：TC clsact 上做 SNAT/DNAT + 增量校验和。
 * 挂载约定（与 C5 拓扑一致）：
 *   nat_ingress 挂 veth-fw-c ingress 做 DNAT（目的改写）；
 *   nat_egress  挂 veth-fw-s egress  做 SNAT（源改写）。
 */
#include "fw_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, NAT_MAX_ENTRIES);
    __type(key, struct nat_key);
    __type(value, struct nat_value);
} nat_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, STAT_MAX);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

static __always_inline void bump(__u32 idx)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &idx);
    if (c)
        (*c)++;
}

/* 用 bpf_skb_load_bytes 读端口（读 helper 不改动报文，不会使包指针失效） */
static __always_inline int load_ports(struct __sk_buff *skb, __u32 l4_off, __u8 proto,
                                      __be16 *sport, __be16 *dport)
{
    if (proto == IPPROTO_TCP) {
        struct tcphdr t;
        if (bpf_skb_load_bytes(skb, l4_off, &t, sizeof(t)))
            return 0;
        *sport = t.source; *dport = t.dest;
        return 1;
    }
    struct udphdr u;
    if (bpf_skb_load_bytes(skb, l4_off, &u, sizeof(u)))
        return 0;
    *sport = u.source; *dport = u.dest;
    return 1;
}

/*
 * 统一改写核心：dir=0 DNAT（改目的地址/端口），dir=1 SNAT（改源地址/端口）。
 *
 * 实现要点（正文逐条讲解）：
 *  1) 所有包指针必须在第一个改写 helper 调用之前用完——bpf_skb_store_bytes /
 *     csum_replace 可能使 verifier 判定包指针失效；
 *  2) 改写字段用 store_bytes（绝对偏移），旧值/新值都先读入局部变量；
 *  3) 校验和用增量算法：改动 4 字节地址等价于在原校验和上叠加 ~(old)+new 的进位折叠，
 *     bpf_l4_csum_replace 带 BPF_F_PSEUDO_HDR 处理 TCP/UDP 伪头中的地址项；
 *  4) UDP 校验和为 0 表示「不校验」，此时不得调用 helper（会被改坏语义）；
 *  5) 通配规则：精确 key 未命中时，把「非 NAT 侧」的地址/端口清零再查一次；
 *  6) new_port==0 表示「只改地址不改端口」（SNAT 场景），此时跳过端口改写与端口增量。
 */
static __always_inline int do_nat(struct __sk_buff *skb, __u8 dir)
{
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    __u32 l3_off = sizeof(struct ethhdr);

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return TC_ACT_OK;
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return TC_ACT_OK;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return TC_ACT_OK;
    if (ip->protocol != IPPROTO_TCP && ip->protocol != IPPROTO_UDP)
        return TC_ACT_OK;

    __u32 l4_off = l3_off + ip->ihl * 4;
    __be16 sport = 0, dport = 0;
    if (!load_ports(skb, l4_off, ip->protocol, &sport, &dport))
        return TC_ACT_OK;

    struct nat_key key = {
        .src_ip = ip->saddr, .dst_ip = ip->daddr,
        .src_port = sport, .dst_port = dport,
        .proto = ip->protocol,
    };
    struct nat_value *v = bpf_map_lookup_elem(&nat_map, &key);
    if (!v) {
        struct nat_key wild = key;
        if (dir == 0) { wild.src_ip = 0; wild.src_port = 0; }   /* DNAT：源侧通配 */
        else          { wild.dst_ip = 0; wild.dst_port = 0;     /* SNAT：目的侧通配 */
                        wild.src_port = 0; }   /* 源端口一并通配：--snat 只有 IP 入参，
                                                  出方向源端口是内核随机分配的临时值，
                                                  静态规则写不出，按 (源 IP, 协议) 命中 */
        v = bpf_map_lookup_elem(&nat_map, &wild);
    }
    if (!v || v->is_snat != dir)
        return TC_ACT_OK;

    __u8  is_tcp    = (ip->protocol == IPPROTO_TCP);
    __u32 old_addr  = (dir == 0) ? ip->daddr : ip->saddr;
    __u32 new_addr  = v->new_addr;
    __be16 old_port = (dir == 0) ? dport : sport;
    __be16 new_port = v->new_port;
    __u8  do_port   = (v->new_port != 0);   /* new_port==0：仅改地址（SNAT 场景） */
    __u32 addr_off  = l3_off + (dir == 0 ? offsetof(struct iphdr, daddr)
                                         : offsetof(struct iphdr, saddr));
    /* tcphdr/udphdr 的 source/dest 布局相同：SNAT 改 offset 0，DNAT 改 offset 2 */
    __u32 port_off  = l4_off + (dir == 0 ? 2 : 0);
    __u32 l4_ck_off = l4_off + (is_tcp ? offsetof(struct tcphdr, check)
                                       : offsetof(struct udphdr, check));

    /* 1) 改写端口与地址（先改字段，后修校验和：helper 参数自带 old/new，顺序无关正确性） */
    if (do_port)
        bpf_skb_store_bytes(skb, port_off, &new_port, sizeof(new_port), 0);
    bpf_skb_store_bytes(skb, addr_off, &new_addr, sizeof(new_addr), 0);

    /* 2) L4 校验和：地址增量按伪头计（BPF_F_PSEUDO_HDR），端口增量 2 字节 */
    if (is_tcp) {
        bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                            sizeof(__u32) | BPF_F_PSEUDO_HDR);
        if (do_port)
            bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
    } else {
        __u16 ck = 0;
        bpf_skb_load_bytes(skb, l4_ck_off, &ck, sizeof(ck));
        if (ck != 0) {                       /* UDP 校验和 0 = 不校验，跳过 */
            bpf_l4_csum_replace(skb, l4_ck_off, old_addr, new_addr,
                                sizeof(__u32) | BPF_F_PSEUDO_HDR);
            if (do_port)
                bpf_l4_csum_replace(skb, l4_ck_off, old_port, new_port, sizeof(__u16));
        }
    }

    /* 3) IP 头校验和最后更新（只含地址增量，与端口无关） */
    bpf_l3_csum_replace(skb, l3_off + offsetof(struct iphdr, check),
                        old_addr, new_addr, sizeof(__u32));

    bump(STAT_NAT);
    return TC_ACT_OK;
}

SEC("tc")
int nat_ingress(struct __sk_buff *skb)   /* DNAT */
{
    return do_nat(skb, 0);
}

SEC("tc")
int nat_egress(struct __sk_buff *skb)    /* SNAT */
{
    return do_nat(skb, 1);
}

char LICENSE[] SEC("license") = "GPL";
```

- [x] **Step 3: 创建 `nat.c`**（loader + NAT 下发 + TC attach）。TC attach 核心代码（骨架其余同 Task 3）：

```c
#include <bpf/libbpf.h>   /* LIBBPF_OPTS / bpf_tc_* */
/* ingress DNAT */
LIBBPF_OPTS(bpf_tc_hook, hook_in, .ifindex = ifindex, .attach_point = BPF_TC_INGRESS);
if (bpf_tc_hook_create(&hook_in) != 0 && errno != EEXIST) { /* 报错退出 */ }
LIBBPF_OPTS(bpf_tc_opts, opt_in, .handle = 1, .priority = 1,
            .prog_fd = bpf_program__fd(skel->progs.nat_ingress));
bpf_tc_attach(&hook_in, &opt_in);
/* egress SNAT 同型：attach_point=BPF_TC_EGRESS，handle=2 */
```

NAT 下发参数（固定口径）：

```text
sudo ./nat -i veth-fw-c -e veth-fw-s \
     --dnat tcp,10.0.1.1,8080,10.0.2.2,8080 \
     --snat 10.0.0.2,10.0.2.1
--dnat <proto>,<原目的IP>,<原目的端口>,<新IP>,<新端口>  写通配 key {src=0,sport=0}
--snat <原源IP>,<新IP>                                  写通配 key {dst=0,dport=0,同 proto}
```

- [x] **Step 4: `Makefile`**（C7，`<名>=nat`）。**Step 5: 编译验证** `make -C book/examples/chapter-17/04-nat-tc` 退出码 0。
- [x] **Step 6: `README.md`**（C8）。预期输出示例（运行级待验证）：tcpdump 在 ns-server 抓到目的 10.0.2.2:8080 的报文（原为 10.0.1.1:8080），回程源为 10.0.2.1；`stats` 中 `nat` 计数递增。讲解要点：改写顺序与指针失效、增量校验和数学直觉、UDP 0 校验和、DNAT/SNAT 挂载方向。验证状态 C6。
- [x] **Step 7: 提交** `git commit -m "feat(ch17): 阶段4 TC NAT（SNAT/DNAT + 增量校验和）"`

---

### Task 7: 阶段 5 —— 05-firewall 完整系统与 Go 控制面
- [x] Task 7: 阶段 5 —— 05-firewall 完整系统与 Go 控制面
**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/05-firewall/` complete system: `bpf/fw_common.h` (full union of stages 2-4), `bpf/fw_xdp.bpf.c`, `bpf/fw_tc.bpf.c`, `user/` Go control plane (go.mod with pinned cilium/ebpf, main/rules/maps/stats), combined Makefile, and README, with Chinese comments throughout.」

**Files:**
- Create: `book/examples/chapter-17/05-firewall/bpf/fw_common.h`
- Create: `book/examples/chapter-17/05-firewall/bpf/fw_xdp.bpf.c`
- Create: `book/examples/chapter-17/05-firewall/bpf/fw_tc.bpf.c`
- Create: `book/examples/chapter-17/05-firewall/user/go.mod`（+ `go mod tidy` 生成 go.sum 一并提交）
- Create: `book/examples/chapter-17/05-firewall/user/main.go`、`rules.go`、`maps.go`、`stats.go`
- Create: `book/examples/chapter-17/05-firewall/Makefile`
- Create: `book/examples/chapter-17/05-firewall/README.md`
- Modify: `.gitignore`（追加一行 `book/examples/chapter-17/05-firewall/fwctl`）

**Interfaces:**
- Consumes: Task 4–6 全部 BPF 逻辑；C2 Go 镜像结构；C5 拓扑。
- Produces: `fwctl` CLI（attach/detach/rule/nat/conntrack/stats）；pin 目录 `/sys/fs/bpf/fw17`；spec 合并共享 map 模式（练习 02/03 复用 user/ 四文件）。

- [x] **Step 1: 创建 `bpf/fw_common.h`** —— Task 4+5+6 三份 `fw_common.h` 的**逐字并集**：常量区（MAX_RULES/MAX_LPM_KEYS/STAT_MAX=8/CT_MAX_ENTRIES/NAT_MAX_ENTRIES）→ `enum stat_idx`（含全部 8 项，逐项注释「阶段 N 新增」）→ flow_key/fw_action/rule_value/lpm_key/lpm_value → ct_state/ct_key/ct_value → nat_key/nat_value。头注释写明「本文件为阶段 2–4 的严格并集，正文按新增字段来源讲解」。

- [x] **Step 2: 创建 `bpf/fw_xdp.bpf.c`** —— 阶段 2 的解析/ACL + 阶段 3 的 conntrack 合并，关键差异：**快路径先于 ACL**（已建立连接跳过 ACL 查找）：

```c
/* 主流程（SEC("xdp") int fw_xdp(struct xdp_md *ctx)）：
 *   parse_flow 失败 -> PASS
 *   bump(STAT_TOTAL)
 *   conntrack 查 fwd/rev：
 *     命中且 state==CT_ESTABLISHED -> 刷新表项、bump(STAT_CT_ESTABLISHED)、PASS（快路径）
 *     否则 -> ACL 判定（阶段 2 acl_verdict 原样）-> DROP 则 bump(STAT_DROP_ACL)+XDP_DROP
 *   未命中 -> 新连接登记（阶段 3 逻辑原样：SYN/UDP 首包、BPF_NOEXIST、STAT_CT_FULL）
 *   状态迁移（ESTABLISHED/FIN_WAIT/CLOSED）与阶段 3 完全一致 */
```

实现时把 Task 4 `acl.bpf.c` 与 Task 5 `conntrack.bpf.c` 的代码逐段合并进单一文件（两张 ACL map + conntrack map + stats map 四个 map 定义同文件），不要发明新逻辑。

- [x] **Step 3: 创建 `bpf/fw_tc.bpf.c`** —— Task 6 的 `do_nat`/map 定义原样迁入；SEC 名改为 `fw_tc_ingress`/`fw_tc_egress`（`SEC("tc") int fw_tc_ingress(struct __sk_buff *skb) { return do_nat(skb, 0); }` 等）；注释标注「与阶段 4 的差异仅程序名与共享 map pin」。

- [x] **Step 4: 创建 `user/go.mod`（固定版本，与第 9 章 09-netfilter-go 一致）**

```text
module github.com/ebpf-book/fwctl

go 1.22

require (
	github.com/cilium/ebpf v0.16.0
	golang.org/x/sys v0.25.0
)
```

随后执行 `cd book/examples/chapter-17/05-firewall/user && go mod tidy` 生成 `go.sum` 并提交（网络不可用时在 README 标注「依赖待拉取」并继续，见 C6）。

- [x] **Step 5: 创建 `user/maps.go`**（类型镜像 + 加载/pin/attach/detach）

```go
package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
)

const pinDir = "/sys/fs/bpf/fw17"

/* 与 bpf/fw_common.h 逐字节对应（网络字节序手工填充，见 rules.go） */
type FlowKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type RuleValue struct {
	Action uint8
	Pad    [7]byte
}

type LpmKey struct {
	PrefixLen uint32
	Addr      [4]byte
}

type LpmValue struct {
	Action uint8
	Pad    [7]byte
}

type CtKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type CtValue struct {
	LastSeenNS uint64
	Packets    uint64
	Bytes      uint64
	State      uint8
	Pad        [7]byte
}

type NatKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type NatValue struct {
	NewAddr [4]byte
	NewPort [2]byte
	IsSnat  uint8
	Pad     uint8
}

func ensurePinDir() error {
	return os.MkdirAll(pinDir, 0o700) // bpffs 子目录，detach 时清理
}

/* 两个 .o 的同名 map 必须是同一实例（XDP 与 TC 要看到同一份规则/统计）。
 * cilium/ebpf 不允许一个 Collection 里出现同名 map——把 TC spec 的程序与缺失的
 * map 并入 XDP spec 后一次性加载，同名 map 自然共享。 */
func mergeSpecs(xdp, tc *ebpf.CollectionSpec) *ebpf.CollectionSpec {
	for name, prog := range tc.Programs {
		xdp.Programs[name] = prog
	}
	for name, m := range tc.Maps {
		if _, ok := xdp.Maps[name]; !ok {
			xdp.Maps[name] = m
		}
	}
	return xdp
}

func loadAndAttach(xdpObj, tcObj, xdpIface, tcIngress, tcEgress string) error {
	if err := ensurePinDir(); err != nil {
		return err
	}
	specX, err := ebpf.LoadCollectionSpec(xdpObj)
	if err != nil {
		return fmt.Errorf("解析 %s: %w", xdpObj, err)
	}
	specT, err := ebpf.LoadCollectionSpec(tcObj)
	if err != nil {
		return fmt.Errorf("解析 %s: %w", tcObj, err)
	}
	coll, err := ebpf.NewCollectionWithOptions(mergeSpecs(specX, specT),
		ebpf.CollectionOptions{Maps: ebpf.MapOptions{PinPath: pinDir}})
	if err != nil {
		return fmt.Errorf("加载 BPF（需 root/CAP_BPF）: %w", err)
	}
	defer coll.Close()

	/* pin 两个数据面程序，供 tc filter 以 pinned 方式挂载 */
	for name, prog := range coll.Programs {
		p := filepath.Join(pinDir, name+".prog")
		if err := prog.Pin(p); err != nil {
			return err
		}
	}

	ifi, err := link.InterfaceByName(xdpIface) // 只允许 veth，勿挂物理网卡
	if err != nil {
		return err
	}
	xdpLink, err := link.AttachXDP(link.XDPOptions{
		Interface: ifi.Index, Program: coll.Programs["fw_xdp"],
	})
	if err != nil {
		return fmt.Errorf("attach XDP: %w", err)
	}
	lp := filepath.Join(pinDir, "xdp_link")
	if err := xdpLink.Pin(lp); err != nil { // pin link 供 detach 复用
		return err
	}
	if err := tcAttach(tcIngress, "ingress", filepath.Join(pinDir, "fw_tc_ingress.prog")); err != nil {
		return err
	}
	return tcAttach(tcEgress, "egress", filepath.Join(pinDir, "fw_tc_egress.prog"))
}

/* TC 挂载用 tc 命令 + pinned prog：兼容 5.10+（cilium/ebpf 的 TCX 路径需 6.6+） */
func tcAttach(iface, direction, progPin string) error {
	for _, args := range [][]string{
		{"tc", "qdisc", "replace", "dev", iface, "clsact"},
		{"tc", "filter", "replace", "dev", iface, direction, "bpf", "direct-action", "pinned", progPin},
	} {
		if out, err := exec.Command(args[0], args[1:]...).CombinedOutput(); err != nil {
			return fmt.Errorf("tc attach: %v: %s", err, out)
		}
	}
	return nil
}

func detach(xdpIface, tcIngress, tcEgress string) error {
	if l, err := link.LoadPinnedLink(filepath.Join(pinDir, "xdp_link"), nil); err == nil {
		_ = l.Close() // 关闭 pinned XDP link 即解除挂载
	}
	/* filter 必须按方向 flush：内核拒绝指定 kind 的 flush——
	 * `tc filter del dev <dev> bpf` 实测报
	 * 「Error: Cannot flush filters with protocol, handle or kind set.」（exit 2）且
	 * filter 原样残留，只有 `tc filter del dev <dev> ingress/egress` 才能清掉。
	 * 这里显式检查退出码、不静默吞错：filter 持有程序引用，残留会让防火墙在
	 * 「卸载成功」的假象下继续过滤/改写流量，比报错更危险 */
	for _, f := range []struct{ dev, dir string }{{tcIngress, "ingress"}, {tcEgress, "egress"}} {
		if out, err := exec.Command("tc", "filter", "del", "dev", f.dev, f.dir).CombinedOutput(); err != nil {
			return fmt.Errorf("tc filter del dev %s %s: %v: %s", f.dev, f.dir, err, out)
		}
	}
	/* qdisc 为尽力清理：无 clsact 时（如重复 detach）容忍失败——filter 已按方向清空，
	 * 空 clsact 不再持有程序引用 */
	_ = exec.Command("tc", "qdisc", "del", "dev", tcEgress, "clsact").Run()
	return os.RemoveAll(pinDir) // map/prog/link pin 一并清理，避免残留导致二次 attach 失败
}

func openMap(name string) (*ebpf.Map, error) {
	return ebpf.LoadPinnedMap(filepath.Join(pinDir, name), nil)
}
```

（加载失败时若 `os.IsNotExist(pinDir)` 给出「先执行 attach」提示。）

- [x] **Step 6: 创建 `user/rules.go`**

```go
package main

import (
	"encoding/binary"
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/cilium/ebpf"
)

/* inet.IPString -> [4]byte（网络字节序） */
func ipBytes(s string) ([4]byte, error) {
	ip := net.ParseIP(s)
	if ip == nil || ip.To4() == nil {
		return [4]byte{}, fmt.Errorf("非法 IPv4: %s", s)
	}
	var b [4]byte
	copy(b[:], ip.To4())
	return b, nil
}

func portBytes(p string) ([2]byte, error) {
	n, err := strconv.Atoi(p)
	if err != nil || n < 0 || n > 65535 {
		return [2]byte{}, fmt.Errorf("非法端口: %s", p)
	}
	var b [2]byte
	binary.BigEndian.PutUint16(b[:], uint16(n))
	return b, nil
}

/* fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop */
func ParseRule(proto, src, sport, dst, dport, action string) (FlowKey, RuleValue, error) {
	var k FlowKey
	var v RuleValue
	if proto != "tcp" && proto != "udp" {
		return k, v, fmt.Errorf("proto 仅支持 tcp/udp")
	}
	var err error
	if k.SrcIP, err = ipBytes(src); err != nil {
		return k, v, err
	}
	if k.DstIP, err = ipBytes(dst); err != nil {
		return k, v, err
	}
	if k.SrcPort, err = portBytes(sport); err != nil {
		return k, v, err
	}
	if k.DstPort, err = portBytes(dport); err != nil {
		return k, v, err
	}
	if proto == "tcp" {
		k.Proto = 6
	} else {
		k.Proto = 17
	}
	if action == "drop" {
		v.Action = 1
	} else if action != "allow" {
		return k, v, fmt.Errorf("action 仅支持 allow/drop")
	}
	return k, v, nil
}

/* 冲突检查：key 已存在即拒绝——hash map 的逐 key 更新本身就是原子的，
 * 「原子更新」指不做清空重建，避免瞬时全放行/全阻断 */
func AddRule(m *ebpf.Map, k FlowKey, v RuleValue) error {
	var existing RuleValue
	if err := m.Lookup(k, &existing); err == nil {
		return fmt.Errorf("规则已存在（冲突），如需覆盖请先 rule del")
	}
	return m.Put(k, v)
}

func DelRule(m *ebpf.Map, k FlowKey) error { return m.Delete(k) }

func ListRules(m *ebpf.Map) error {
	var k FlowKey
	var v RuleValue
	it := m.Iterate()
	for it.Next() {
		if err := it.Decode(&k); err != nil {
			return err
		}
		if err := it.Decode(&v); err != nil {
			return err
		}
		act := "allow"
		if v.Action == 1 {
			act = "drop"
		}
		fmt.Printf("%s:%d -> %s:%d proto=%d action=%s\n",
			int2ip(k.SrcIP), be16(k.SrcPort), int2ip(k.DstIP), be16(k.DstPort), k.Proto, act)
	}
	return it.Err()
}

/* fwctl cidr add --action drop --cidr 10.0.0.0/24：LPM key 由前缀长度+网络地址构成 */
func AddLpmRule(m *ebpf.Map, action, cidr string) error {
	_, ipnet, err := net.ParseCIDR(cidr)
	if err != nil {
		return err
	}
	ones, _ := ipnet.Mask.Size()
	var k LpmKey
	k.PrefixLen = uint32(ones)
	copy(k.Addr[:], ipnet.IP.To4())
	var v LpmValue
	if strings.EqualFold(action, "drop") {
		v.Action = 1
	}
	return m.Put(k, v)
}

/* NAT：fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
 *      fwctl nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1   （仅改地址）
 * 与 do_nat 通配 key 契约一致：DNAT 写 {src=0,sport=0}，SNAT 写 {dst=0,dport=0}；
 * snat 的 --to 为裸 IP 时 NewPort 保持 0，数据面按「仅改地址」处理。 */
func AddNat(m *ebpf.Map, typ, proto, dst, dport, to string) error {
	if typ != "snat" && typ != "dnat" {
		return fmt.Errorf("type 仅支持 snat/dnat")
	}
	if proto != "tcp" && proto != "udp" {
		return fmt.Errorf("proto 仅支持 tcp/udp")
	}
	var k NatKey
	var v NatValue
	var err error
	if typ == "dnat" {
		if k.DstIP, err = ipBytes(dst); err != nil {
			return err
		}
		if k.DstPort, err = portBytes(dport); err != nil {
			return err
		}
		host, portStr, err2 := net.SplitHostPort(to)
		if err2 != nil {
			return fmt.Errorf("--to 需为 IP:端口: %w", err2)
		}
		if v.NewAddr, err = ipBytes(host); err != nil {
			return err
		}
		if v.NewPort, err = portBytes(portStr); err != nil {
			return err
		}
	} else {
		if k.SrcIP, err = ipBytes(dst); err != nil {
			return err
		}
		if v.NewAddr, err = ipBytes(to); err != nil {
			return err
		}
	}
	if proto == "tcp" {
		k.Proto = 6
	} else {
		k.Proto = 17
	}
	if typ == "snat" {
		v.IsSnat = 1
	}
	return m.Put(k, v)
}

/* 共享小工具（rules.go 内定义，stats.go / 练习 02 persist.go 复用） */
func int2ip(b [4]byte) string { return net.IP(b[:]).String() }
func be16(b [2]byte) uint16   { return binary.BigEndian.Uint16(b[:]) }
func protoName(p uint8) string {
	if p == 6 {
		return "tcp"
	}
	return "udp"
}
```

实现注意：`ListRules` 使用的 `int2ip`/`be16` 即上方共享小工具；`AddNat` 的 snat 分支不带端口，数据面按 `new_port==0` 仅改地址（与 Task 6 `do_nat` 的 `do_port` 契约一致）。

- [x] **Step 7: 创建 `user/stats.go`**

```go
package main

import (
	"encoding/binary"
	"fmt"

	"github.com/cilium/ebpf"
)

var statNames = [...]string{
	"total", "pass", "drop_acl", "drop_parse",
	"ct_new", "ct_established", "ct_full", "nat",
} // 顺序即 C1 契约，不得改动

/* per-CPU 聚合：lookup 返回 nr_cpus 份 __u64，逐 CPU 求和（漏 CPU 即漏计数） */
func SumPerCPU(m *ebpf.Map, key uint32) (uint64, error) {
	var all []uint64
	if err := m.Lookup(key, &all); err != nil {
		return 0, err
	}
	var sum uint64
	for _, v := range all {
		sum += v
	}
	return sum, nil
}

func PrintStats(m *ebpf.Map) error {
	for i, name := range statNames {
		v, err := SumPerCPU(m, uint32(i))
		if err != nil {
			return err
		}
		fmt.Printf("%-16s %d\n", name, v)
	}
	return nil
}

func PrintConntrack(m *ebpf.Map) error {
	var k CtKey
	var v CtValue
	it := m.Iterate()
	for it.Next() {
		if err := it.Decode(&k); err != nil {
			return err
		}
		if err := it.Decode(&v); err != nil {
			return err
		}
		states := map[uint8]string{1: "NEW", 2: "ESTABLISHED", 3: "FIN_WAIT", 4: "CLOSED"}
		fmt.Printf("%s:%d -> %s:%d proto=%d state=%s\n",
			int2ip(k.SrcIP), binary.BigEndian.Uint16(k.SrcPort[:]),
			int2ip(k.DstIP), binary.BigEndian.Uint16(k.DstPort[:]),
			k.Proto, states[v.State])
	}
	return it.Err()
}
```

- [x] **Step 8: 创建 `user/main.go`** —— `flag.NewFlagSet` 子命令分发，CLI 固定口径：

```text
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
sudo ./fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
sudo ./fwctl rule list / rule del（同参） / cidr add --action drop --cidr 10.0.0.0/24
sudo ./fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
sudo ./fwctl conntrack list
sudo ./fwctl stats
sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
```

`main()` 结构：`switch os.Args[1]` → 各子命令开 flagset → 调 `maps.go/rules.go/stats.go` 函数；map 名常量 `acl_rules/acl_lpm/conntrack/nat_map/stats` 与 C3 一致；usage 文本中文。

- [x] **Step 9: 创建 `Makefile`** —— 参照 `book/examples/chapter-09/09-netfilter-go/Makefile`，目标与依赖：

```makefile
# 第 17 章阶段 5 构建脚本：BPF 数据面 + Go 控制面联合构建。
# 用法：make（首次构建前先执行 go mod tidy 拉取依赖）

ARCH := $(shell uname -m | sed 's/x86_64/x86/;s/aarch64/arm64/')

CLANG ?= clang
BPFTOOL ?= bpftool
GO ?= go

.PHONY: all clean

all: fwctl

bpf/vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

bpf/fw_xdp.bpf.o: bpf/fw_xdp.bpf.c bpf/fw_common.h bpf/vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Ibpf -c $< -o $@

bpf/fw_tc.bpf.o: bpf/fw_tc.bpf.c bpf/fw_common.h bpf/vmlinux.h
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Ibpf -c $< -o $@

fwctl: bpf/fw_xdp.bpf.o bpf/fw_tc.bpf.o user/go.mod user/main.go user/rules.go user/maps.go user/stats.go
	cd user && $(GO) build -o ../fwctl

clean:
	rm -f bpf/*.o bpf/vmlinux.h fwctl
```

- [x] **Step 10: 编译验证（硬性）**

Run: `make -C book/examples/chapter-17/05-firewall`
Expected: 退出码 0（BPF 编译 + `go build` 成功）。若 go 依赖因网络失败：执行 `go mod tidy` 重试；仍失败则按 C6 在 README 标注并记录，其余阶段继续。

- [x] **Step 11: `README.md`**（C8 + 增补「与阶段 1–4 的关系」与「pin 对象生命周期」小节）。预期输出示例（运行级待验证）：`fwctl stats` 表格输出 8 项计数、`fwctl conntrack list` 列出 ESTABLISHED 表项。
- [x] **Step 12: 追加 `.gitignore` 并提交**

```bash
echo 'book/examples/chapter-17/05-firewall/fwctl' >> .gitignore
git add book/examples/chapter-17/05-firewall/ .gitignore
git commit -m "feat(ch17): 阶段5 完整系统（XDP+TC 数据面 + Go fwctl 控制面）"
```

---

### Task 8: scripts/ 拓扑、压测与顶层编译级验证
- [x] Task 8: scripts/ 拓扑、压测与顶层编译级验证
**tasks.md 条目**：§5.1.2「Generate `book/examples/chapter-17/scripts/` with `setup-veth.sh` (topology setup/teardown + safety warnings), `bench-iperf.sh`, `bench-pktgen.sh`, and `verify-build.sh` (top-level compile-level verification across all stages and exercises).」

**Files:**
- Create: `book/examples/chapter-17/scripts/setup-veth.sh`、`bench-iperf.sh`、`bench-pktgen.sh`、`verify-build.sh`（均 `chmod +x`）

**Interfaces:**
- Consumes: C5 拓扑契约；C6 验证约定。
- Produces: `verify-build.sh` 退出码语义（0=全绿，1=存在 FAIL，WARN 仅限 Go 网络依赖）；Task 14 硬性验收依赖它。

- [x] **Step 1: 创建 `setup-veth.sh`**（完整脚本按 C5 契约实现，头部安全警告「仅操作 veth/netns，绝不触碰物理网卡」）

```bash
#!/usr/bin/env bash
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 第 17 章实验拓扑：ns-client -- ns-fw -- ns-server（三命名空间，veth 组网）。
# 安全警告：本脚本只创建 veth 与 netns；XDP/TC 只允许挂到 veth-fw-c / veth-fw-s。
# 用法：sudo bash setup-veth.sh [setup|teardown]
set -euo pipefail
NS_CLIENT=ns-client; NS_FW=ns-fw; NS_SERVER=ns-server

setup() {
    [ "$(id -u)" -eq 0 ] || { echo "需要 root（sudo）运行"; exit 1; }
    # 幂等防护：残留命名空间会让下方 ip link set 中途失败并在宿主 ns 遗留游离 veth 对
    for ns in "$NS_CLIENT" "$NS_FW" "$NS_SERVER"; do
        if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
            echo "检测到残留命名空间 $ns，请先执行 teardown（sudo bash $0 teardown）"
            exit 1
        fi
    done
    ip netns add $NS_CLIENT 2>/dev/null || true
    ip netns add $NS_FW     2>/dev/null || true
    ip netns add $NS_SERVER 2>/dev/null || true
    # 左链路
    ip link add veth-c type veth peer name veth-fw-c
    ip link set veth-c netns $NS_CLIENT
    ip link set veth-fw-c netns $NS_FW
    # 右链路
    ip link add veth-s type veth peer name veth-fw-s
    ip link set veth-s netns $NS_SERVER
    ip link set veth-fw-s netns $NS_FW
    # 地址与转发
    ip -n $NS_CLIENT addr add 10.0.0.2/24 dev veth-c; ip -n $NS_CLIENT link set veth-c up
    ip -n $NS_CLIENT route replace default via 10.0.0.1
    ip -n $NS_FW addr add 10.0.0.1/24 dev veth-fw-c;  ip -n $NS_FW link set veth-fw-c up
    ip -n $NS_FW addr add 10.0.2.1/24 dev veth-fw-s;  ip -n $NS_FW link set veth-fw-s up
    # ip 无 sysctl 子对象，进 ns 执行 sysctl 是标准用法
    ip netns exec $NS_FW sysctl -w net.ipv4.ip_forward=1
    ip -n $NS_SERVER addr add 10.0.2.2/24 dev veth-s; ip -n $NS_SERVER link set veth-s up
    ip -n $NS_SERVER route replace default via 10.0.2.1
    echo "拓扑就绪。连通性：sudo ip netns exec $NS_CLIENT ping 10.0.2.2"
}

teardown() {
    ip netns del $NS_CLIENT 2>/dev/null || true
    ip netns del $NS_FW     2>/dev/null || true   # veth 对随 netns 一并销毁
    ip netns del $NS_SERVER 2>/dev/null || true
    echo "拓扑已销毁"
}

case "${1:-setup}" in
    setup) setup ;;
    teardown) teardown ;;
    *) echo "用法: $0 [setup|teardown]"; exit 1 ;;
esac
```

- [x] **Step 2: 创建 `bench-iperf.sh`**

```bash
#!/usr/bin/env bash
# 吞吐压测：依赖 setup-veth.sh 拓扑与 iperf3。运行级待验证（编写环境无 root）。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 用法：sudo bash bench-iperf.sh [时长秒]
set -euo pipefail
DURATION=${1:-30}
# 时长校验：仅接受正整数
case "$DURATION" in
    ''|*[!0-9]*|0) echo "用法: $0 [时长秒]（需为正整数）"; exit 1 ;;
esac
ip netns exec ns-server iperf3 -s >/dev/null 2>&1 &   # 后台服务端（不用 -D，前台后台化以便记录 PID）
IPERF_PID=$!
trap 'kill "$IPERF_PID" 2>/dev/null || true' EXIT     # 退出时只清理本脚本启动的服务端
ip netns exec ns-client iperf3 -c 10.0.2.2 -P 8 -t "$DURATION"
# 指标口径：SUM 行 sender/receiver 吞吐（Gbits/sec）与重传数；对照 fwctl stats 的 pass/drop
```

- [x] **Step 3: 创建 `bench-pktgen.sh`**

```bash
#!/usr/bin/env bash
# pktgen 打流压测 pps/丢包率：必须 root；仅作用于 veth（默认 veth-fw-c）。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 运行级待验证：pktgen 依赖 /proc/net/pktgen，无法在编写环境验证，以脚本+预期输出交付。
# 用法：sudo bash bench-pktgen.sh [dev] [时长秒]
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "需要 root（sudo）运行"; exit 1; }   # 写 /proc/net/pktgen 需 root
DEV=${1:-veth-fw-c}; SECS=${2:-10}
PG=/proc/net/pktgen
# 依赖检查：count 的微秒换算需要 bc，缺失时报错并给出安装提示
command -v bc >/dev/null 2>&1 || { echo "缺少依赖：bc 未安装，请先安装（如 apt install bc 或 dnf install bc）后重试"; exit 1; }
modprobe pktgen 2>/dev/null || true
[ -d "$PG" ] || { echo "内核未启用 pktgen"; exit 1; }
echo "rem_device_all"                              > "$PG/kpktgend_0"
echo "add_device ${DEV}@0"                         > "$PG/kpktgend_0"
D="$PG/${DEV}@0"
echo "count $(echo "$SECS"*1000000 | bc)"          > "$D"   # 发包个数：count 按包数计（非微秒），按 1 Mpps 假设折算 SECS 秒
echo "delay 0"                                     > "$D"
echo "pkt_size 64"                                 > "$D"
echo "dst_mac 00:00:00:00:00:02"                   > "$D"   # 按拓扑实际对端 MAC 修改
echo "dst 10.0.0.2"                                > "$D"
echo "start"                                       > "$PG/pgctrl"
grep -E "Result|pps" "$D"
# 指标口径：pps（发送速率）、acked/dropped。
# 方向注意：默认对 veth-fw-c 打流走 TX 路径（veth-fw-c 发送→ns-client），
# 不经过 veth-fw-c 的 XDP（RX）与 veth-fw-s 的 TC egress，与挂载点不交叉；
# 要对照 fwctl stats 的 drop_acl，需在 ns-client 内对 veth-c 打流
# （使流量经 veth-fw-c 的 RX 路径进入防火墙，并相应调整 dst/dst_mac）。
```

- [x] **Step 4: 创建 `verify-build.sh`**（顶层编译级验证，本任务起可运行；练习目录尚不存在时跳过并 WARN）

```bash
#!/usr/bin/env bash
# 顶层编译级验证：逐阶段 make + exercises 编译 + 汇总。无特权可执行。
# 安全警告：勿在物理网卡误挂 XDP/TC。
# 退出码：0=全绿；1=存在 FAIL；Go 网络依赖失败记 WARN（不算失败，需人工复验）。
set -u
cd "$(dirname "$0")/.."                      # 进入 book/examples/chapter-17
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
pass=0; fail=0; warn=0

run() {
    local name="$1"; shift
    if [ ! -d "$name" ]; then
        echo "SKIP  $name（目录未创建）"; return
    fi
    if "$@" >"$LOG" 2>&1; then
        echo "PASS  $name"; pass=$((pass+1))
    elif grep -qE 'module lookup|dial tcp|connection refused|could not resolve|no required module' "$LOG"; then
        echo "WARN  $name（Go 依赖拉取失败：需网络 go mod tidy，编译级待验证）"; warn=$((warn+1))
    else
        echo "FAIL  $name"; sed -n '1,30p' "$LOG"; fail=$((fail+1))
    fi
}

echo "== BTF 前置 =="
if bpftool btf dump file /sys/kernel/btf/vmlinux format c >/dev/null 2>&1; then
    echo "OK    vmlinux BTF 可读"
else
    echo "FAIL  BTF 不可读：全部示例无法编译"; exit 1
fi

echo "== 阶段编译 =="
for s in 01-xdp-drop 02-parse-acl 03-conntrack 04-nat-tc 05-firewall; do
    run "$s" make -C "$s" clean all
done

echo "== 练习编译 =="
for e in exercises/01-ipv6-acl exercises/02-rule-persistence \
         exercises/03-prometheus-export exercises/04-port-rate-limit; do
    run "$e" make -C "$e" clean all
done

echo
echo "== 汇总: PASS=$pass WARN=$warn FAIL=$fail =="
[ "$fail" -eq 0 ]
```

- [x] **Step 5: 首次全量运行（硬性）**

Run: `bash book/examples/chapter-17/scripts/verify-build.sh`
Expected: `PASS 01-xdp-drop` … `PASS 05-firewall`；练习段全 `SKIP`；汇总 `FAIL=0`，退出码 0。

- [x] **Step 6: 提交**

```bash
git add book/examples/chapter-17/scripts/
git commit -m "feat(ch17): 拓扑/压测/顶层编译级验证脚本"
```

---

### Task 9: 章节正文重写（五阶段四段式叙事，§5.1.3）

**tasks.md 条目**：§5.1.3「重写第 17 章正文：按五层环境阐述组织开篇，按五个渐进阶段推进叙事（每阶段四段式：教师视角引入→核心代码原理讲解→学生动手→检查点自测），将骨架式“计划/建议”表述替换为基于已落地实现的陈述式讲解。」＋「为报文解析与 verifier 边界检查写逐段原理讲解：为什么每次访问前必须 `data + offset <= data_end`，verifier 如何推理路径安全。」＋「为 ACL 匹配（阶段 2：hash 精确五元组 + LPM CIDR 黑白名单）写原理讲解与查找路径可视化。」＋「为 conntrack 状态机与 LRU 回收（阶段 3）写原理讲解：正反向 key、状态迁移、超时策略、数据面/控制面分工。」＋「为 SNAT/DNAT 与校验和增量更新（阶段 4）写原理讲解：NAT 映射表设计、改写顺序、checksum 增量算法。」＋「为 Go 控制面（阶段 5）写原理讲解：cilium/ebpf 加载、map pin、原子更新、CLI 命令实现。」＋「为压测写结果解读讲解：iperf/pktgen 命令、指标口径、常见失败模式与定位方法。」

（本任务完成阶段叙事主体；「五层环境开篇」由 Task 10 并入，「双视角补齐」由 Task 11 核验——这是 tasks.md §5.1.3 首条的两处交叉要求，拆分落地。）

**Files:**
- Modify: `book/chapters/17-high-performance-ebpf-firewall.md`（整章重写）

**Interfaces:**
- Consumes: Task 2–8 全部资产（正文代码块必须与示例文件一一对应）；Task 1 教学模板。
- Produces: 最终章节大纲骨架（Task 10/11/14 在其上填充）；五层环境小节标题先占位为一级列表（Task 10 填充内容）。

- [ ] **Step 1: 重写为以下大纲（标题逐字固定；一节内容 Task 10 写，本任务保留节标题与 2–3 句导语）**

```text
# 第 17 章 实战：构建高性能 eBPF 防火墙
## 学习目标（改写为成果导向：可构建/可挂载/可下发规则/可读统计/可解读压测）
## 本章导读（教师怎么教：阶段依赖图与课时建议；学生怎么学：动手前置条件清单）
## 一、环境准备（Task 10 填充：1.1–1.5 五层）
## 二、总体架构（数据面/控制面分工）
## 三、阶段 1：最小 XDP 丢弃（四段式）
## 四、阶段 2：报文解析与 ACL（四段式）
## 五、阶段 3：连接跟踪（四段式）
## 六、阶段 4：TC 与 NAT、校验和（四段式）
## 七、阶段 5：完整系统与 Go 控制面（四段式）
## 八、压测与结果解读
## 可视化：数据路径决策树
## 常见问题与排错
## 课后扩展任务（Task 11 填充）
## 总结
## 本章参考来源
```

- [ ] **Step 2: 写「二、总体架构」**（3–4k 字符）：删除骨架的「建议」措辞，改陈述式；重绘架构图为 05-firewall 实际结构（fw_xdp/fw_tc/fwctl/pin 目录/5 张共享 map，对照 C3）；保留并修正原「XDP 与 TC 的选择分析」为「为什么 XDP 做丢弃与 ACL、TC 做 NAT」的结论性叙述（给出实际挂载表：veth-fw-c XDP + TC ingress、veth-fw-s TC egress）。

- [ ] **Step 3: 写「三、阶段 1」（6–9k）**，四段式内容锚点：
  - 教师引入：痛点=防火墙先要会丢包；类比=门口保安先学会拦人；误区=`XDP_PASS`≠“我处理过了”、XDP 发生在 skb 分配之前（保守措辞：native XDP 于驱动收包路径早期，generic XDP 在 skb 之后，随内核版本而异）。
  - 原理讲解：贴 `01-xdp-drop/xdp_drop.bpf.c` 全文逐段（SEC 推断程序类型、`xdp_md`、`XDP_DROP/PASS` 语义表、per-CPU map 选型）；loader 侧讲 open_and_load→attach_xdp→轮询；文本图「加载-校验-JIT-挂载-触发」。
  - 动手：`make`、`sudo ./xdp_drop -i veth-fw-c`、ping 触发；预期输出示例+运行级待验证标注。
  - 检查点（3 问+提示）：(1) `XDP_DROP` 的报文还会被协议栈看到吗？(提示：native/generic 差异) (2) 为什么 `eth+1 > data_end` 检查不可省？(提示：verifier 定界) (3) per-CPU map 为什么要 loader 求和？

- [ ] **Step 4: 写「四、阶段 2」（6–9k）**：逐段讲 `parse_flow` 的**每一层边界检查**（对应 §5.1.3 第 2 条：verifier 如何沿路径推理、漏检查的报错形态示意）；五元组与字节序规范化（网络字节序存原样即可匹配）；贴 `acl.bpf.c` 的 map 定义与 `acl_verdict`；**查找路径可视化**（text 图：报文→hash 命中?→LPM 最长前缀?→默认 allow）；`--rule/--cidr` 下发流程与动手命令；检查点 3 问（LPM 为何 `BPF_F_NO_PREALLOC`、`ihl` 处理、hash 通配为什么做不到）。

- [ ] **Step 5: 写「五、阶段 3」（6–9k）**：正反 key 互换规则与示意图；状态迁移图（CT_NEW→ESTABLISHED→FIN_WAIT/CLOSED，UDP 简化）；LRU 容量与静默淘汰（`STAT_CT_FULL` 的两种策略取舍）；超时回收归控制面的原因；`dump` 弱遍历语义；动手 dump 预期输出示例；检查点 3 问。

- [ ] **Step 6: 写「六、阶段 4」（6–9k）**：XDP vs TC 上下文差异（`xdp_md` vs `__sk_buff`）；`do_nat` 改写顺序逐条讲解（指针失效约束、store_bytes 绝对偏移、`bpf_l4_csum_replace` 的 `BPF_F_PSEUDO_HDR`、UDP 0 校验和）；增量校验和数学直觉（`~old+new` 进位折叠，配 16 位手算示例）；DNAT/SNAT 挂载方向表；动手 tcpdump 对比预期输出示例；检查点 3 问。

- [ ] **Step 7: 写「七、阶段 5」（8–10k）**：合并策略（快路径先于 ACL 的原因：established 流量免 ACL 查找）；`fw_common.h` 并集与字段来源标注；Go 侧逐文件讲解：cilium/ebpf 加载流程、**spec 合并共享 map**（同名 map 必须同一实例，否则 XDP/TC 规则不一致——常见事故）、`MapOptions.PinPath` 与 pin 生命周期（map/prog/link 三类）、TC 用 `tc filter replace pinned` 的兼容性理由（TCX 需 6.6+）、`rules.go` 冲突检查与原子更新（逐 key Put，不清空重建）、`stats.go` per-CPU 聚合；CLI 总览表（Task 7 Step 8 口径）；动手 attach→rule add→stats 预期输出示例；检查点 3 问（含「为什么 XDP 与 TC 必须共享同一 map 实例」）。

- [ ] **Step 8: 写「八、压测与结果解读」（4–6k）**：iperf3/pktgen 命令与指标口径（吞吐/重传/pps/drop rate/CPU）；四类常见失败模式定位表：XDP 未生效（native/generic 与挂载接口确认）、conntrack 满表静默淘汰（观察旧表项消失而非 `ct_full` 递增，见阶段 3 README 教学点）、NAT 校验和错（tcpdump 能抓到但对端静默丢弃、`tcpdump -v` 显示 bad cksum）、规则不生效（字节序/方向/key 不一致）。全部结果写「预期输出示例+运行级待验证」。

- [ ] **Step 9: 更新「可视化：数据路径决策树」**为与实现一致（快路径先于 ACL、`STAT_*` 计数点标注）、「常见问题与排错」（汇总各 README 失败点表并扩展）、「总结」、「本章参考来源」（保留骨架参考条目，追加 cilium/ebpf 文档与 libb-bootstrap）。

- [ ] **Step 10: 骨架残留检查（硬性）**

Run: `grep -nE "计划|建议|后续完整章节|将补齐|占位" book/chapters/17-high-performance-ebpf-firewall.md | grep -v 预期`
Expected: 仅允许出现于「本书其他章节将…」类全书导航句；第 17 章实现相关表述零命中。
Run: `wc -m book/chapters/17-high-performance-ebpf-firewall.md`
Expected: ≥ 32000（本任务后中段值；Task 14 终检 40–60k）。

- [ ] **Step 11: 提交**

```bash
git add book/chapters/17-high-performance-ebpf-firewall.md
git commit -m "docs(ch17): 正文重写为五阶段四段式教学叙事"
```

---

### Task 10: 五层环境阐述并入正文（§5.1.1）

**tasks.md 条目**：§5.1.1 五条全部——「阐述依赖环境：内核版本与 BTF 要求、root/`CAP_BPF`/`CAP_NET_ADMIN` 权限、veth 与网络命名空间前置条件、依赖工具清单与检查方法。」「阐述编译环境：clang/LLVM、libbpf 头文件、Go 工具链与 cilium/ebpf 库的版本要求、安装与验证命令。」「阐述部署环境：ns-client/ns-fw/ns-server veth 实验拓扑设计、脚本化搭建与销毁步骤、安全警告（避免在物理网卡误挂 XDP）。」「阐述运行环境：XDP/TC 挂载与卸载方式、fwctl 控制面启动流程、规则与 NAT 下发步骤。」「阐述运行效果：每个关键阶段的预期输出、tcpdump 抓包对比、stats 与 conntrack 查询结果示例。」

**Files:**
- Modify: `book/chapters/17-high-performance-ebpf-firewall.md`（填充「一、环境准备」1.1–1.5）

**Interfaces:**
- Consumes: Task 2 `check-env.sh`、Task 8 `setup-veth.sh`/`bench-*`、Task 7 fwctl CLI、C6 验证约定。
- Produces: 1.5 小节的「验证状态约定」表（Task 14 审计依据）。

- [ ] **Step 1: 写 1.1 依赖环境（约 1.2k）**：内核 ≥5.10 与 `CONFIG_DEBUG_INFO_BTF`；权限矩阵表（root / CAP_BPF+CAP_NET_ADMIN / 非 root 三档 × 可编译/可加载/可实验）；veth+netns 前置；工具清单与 `bash book/examples/chapter-17/00-env/check-env.sh` 三档结论解读（含本机 `unprivileged_bpf_disabled=2` 语境示例输出，标注「本地验证」）。
- [ ] **Step 2: 写 1.2 编译环境（约 1k）**：clang/LLVM（18.x）、`libbpf-dev`（头文件 `bpf_helpers.h`）、bpftool（7.x）、Go（1.22+，模块 `cilium/ebpf v0.16.0` 固定于 go.mod）；各版本验证命令（`clang --version` 等，本机实测输出可标「本地验证」）；`apt install` 与 `go mod tidy` 说明（网络依赖注明）。
- [ ] **Step 3: 写 1.3 部署环境（约 1.3k）**：C5 拓扑 ASCII 图 + 地址/路由表；`setup-veth.sh setup|teardown` 用法与「veth 对随 netns 销毁」的清理语义；**显式安全警告段**（只挂 veth，勿挂物理网卡/SSH 接口，后果=失联）。
- [ ] **Step 4: 写 1.4 运行环境（约 1.2k）**：XDP 挂载两种方式（loader 内 `bpf_program__attach_xdp`、fwctl `link.AttachXDP`+pin link）与卸载（destroy/pinned link close）；TC 挂载（C 侧 `bpf_tc_attach`、Go 侧 `tc filter replace pinned`）与 `tc filter del`；fwctl 七个子命令总览表（引用 Task 7 口径）；规则/NAT 下发流程图（attach→map pin→rule add→stats）。
- [ ] **Step 5: 写 1.5 运行效果（约 1.3k）**：表格汇总各阶段关键预期输出（xdp_drop 计数、acl drop_acl、conntrack 状态、nat 后 tcpdump 五元组变化、fwctl stats）；tcpdump 抓包对比方法（改动前后五元组/校验和）；stats 与 conntrack 查询示例；验证状态约定说明（C6 三条，逐字引入）。
- [ ] **Step 6: 核对与提交**

Run: `grep -c "^### 1\." book/chapters/17-high-performance-ebpf-firewall.md`
Expected: 5
Run: `wc -m book/chapters/17-high-performance-ebpf-firewall.md`
Expected: ≥ 38000
Run: `git add book/chapters/17-high-performance-ebpf-firewall.md && git commit -m "docs(ch17): 并入五层环境阐述（依赖/编译/部署/运行/效果）"`

---

### Task 11: 双视角补齐与章末课后任务设计（§5.1.4 前三条）

**tasks.md 条目**：§5.1.4「每个主要小节增加教师视角内容：讲解顺序设计、类比、常见误区与“读者容易在哪里卡住”的提醒。」＋「每个主要小节增加学生视角内容：动手实践清单、阶段性检查点、自测问题与答案提示。」＋「设计章末课后扩展任务（已定 4 项：IPv6 ACL 支持、规则持久化与重启恢复、Prometheus 指标导出、按端口限速）。」

**Files:**
- Modify: `book/chapters/17-high-performance-ebpf-firewall.md`

**Interfaces:**
- Consumes: Task 9/10 的成稿；Task 1 模板第 3/4 条。
- Produces: 「课后扩展任务」节引用 `book/examples/chapter-17/exercises/`（Task 12/13 落地前先写好目录约定与任务描述）。

- [ ] **Step 1: 双视角审计与补齐**：逐一核对「一/二/三/四/五/六/七/八」各主要小节；阶段三~七的四段式已在 Task 9 内置，本步骤补齐「一、环境准备」「二、总体架构」「八、压测」三节的教师/学生内容（教师：顺序设计理由、类比、误区提醒——如「环境层不装齐后面全卡住」「共享 map 不一致事故」；学生：动手清单 checkbox、阶段检查点、自测 2 问+提示）。格式固定：小节内用「**教师视角**」「**学生动手**」加粗引导段，检查点用编号列表。
- [ ] **Step 2: 写「课后扩展任务」节**：固定表格（编号/扩展维度/任务一句话/对应目录/验证状态）：

```markdown
| 任务 | 扩展维度 | 任务描述 | 参考实现 |
|---|---|---|---|
| 1 | 数据面 | IPv6 ACL 支持：解析分支 + map key 扩展 | `book/examples/chapter-17/exercises/01-ipv6-acl/` |
| 2 | 控制面 | 规则持久化与重启恢复：保存/加载/原子替换 | `book/examples/chapter-17/exercises/02-rule-persistence/` |
| 3 | 控制面 | Prometheus 指标导出：per-CPU 聚合 + /metrics | `book/examples/chapter-17/exercises/03-prometheus-export/` |
| 4 | 数据面 | 按端口限速：令牌桶 map + 超额动作 | `book/examples/chapter-17/exercises/04-port-rate-limit/` |
```

加防剧透说明段：先自行实现再看参考；README 结构（任务在前、思路独立成节）。

- [ ] **Step 3: 核对与提交**

Run: `grep -c "教师视角" book/chapters/17-high-performance-ebpf-firewall.md && grep -c "课后扩展任务" book/chapters/17-high-performance-ebpf-firewall.md`
Expected: ≥ 8（八个主要小节均有）；≥ 3
Run: `git add book/chapters/17-high-performance-ebpf-firewall.md && git commit -m "docs(ch17): 双视角补齐与章末课后扩展任务"`

---

### Task 12: 练习参考实现（数据面：01-ipv6-acl、04-port-rate-limit）

**tasks.md 条目**：§5.1.4「为每项课后任务提供完整可编译的参考实现与实现思路说明，存放于 `book/examples/chapter-17/exercises/<nn>-<name>/`，练习 README 采用防剧透结构（任务与提示在前、参考实现思路独立成节），并标注验证状态。」（数据面两项）

**Files:**
- Create: `book/examples/chapter-17/exercises/01-ipv6-acl/`：`fw_common.h`、`acl.bpf.c`、`acl.c`、`Makefile`、`README.md`
- Create: `book/examples/chapter-17/exercises/04-port-rate-limit/`：`fw_common.h`、`ratelimit.bpf.c`、`ratelimit.c`、`Makefile`、`README.md`
- Modify: `.gitignore`：追加 `book/examples/chapter-17/exercises/01-ipv6-acl/acl` 与 `book/examples/chapter-17/exercises/04-port-rate-limit/ratelimit`

**Interfaces:**
- Consumes: Task 4 全部代码（练习 01 为其超集）；C7 Makefile 模板（练习 01 替换表：`<名>=acl`；练习 04：`<名>=ratelimit`）。
- Produces: `verify-build.sh` 可验证的 exercises/01、exercises/04；防剧透 README 结构约定（Task 13 沿用）。

- [ ] **Step 1: exercises/01-ipv6-acl —— 以阶段 2 代码为基线扩展（完整可编译）**：
  - `fw_common.h` 追加 IPv6 结构（新增，不动既有契约）：

```c
/* ===== 练习 01 新增：IPv6 五元组与规则 ===== */
struct flow_key6 {
    __be32 src_ip[4];    /* 128 位地址，网络字节序，4 个 32 位字原样存储 */
    __be32 dst_ip[4];
    __be16 src_port;
    __be16 dst_port;
    __u8   proto;        /* 仅 6(TCP)/17(UDP) */
    __u8   pad[3];
};

struct rule_value6 { __u8 action; __u8 pad[7]; };
struct lpm_key6 { __u32 prefixlen; __be32 addr[4]; };   /* LPM key，前缀最长 128 */
```

  - `acl.bpf.c`：在 `parse_flow` 内 IPv4 分支之外新增 `0x86DD` 分支——`struct ipv6hdr` 固定 40 字节头（无 ihl 类比，`(__u8*)ip6 + sizeof(*ip6)` 后查 L4 边界）、`ip6->nexthdr` 仅处理 6/17（**遇到扩展头（非 6/17）直接 PASS 并计数**，注释说明「完整扩展头链解析超出练习范围，保守放行」）；新增 `acl_rules6`（hash，key `flow_key6`）与 `acl_lpm6`（LPM，key `lpm_key6`，`prefixlen` 0–128）两张 map 与对应 verdict 分支（IPv4 报文查 v4 map，IPv6 查 v6 map，互不混用）。
  - `acl.c`：新增 `--rule6 <action>,<proto>,<src>,<sport>,<dst>,<dport>` 与 `--cidr6 <action>,<cidr>`（`inet_pton(AF_INET6, ...)`；v6 CIDR 前缀 `0–128`），v4 参数与阶段 2 完全兼容。
  - `README.md`：防剧透结构（见 Step 3）；验证状态按 C6（编译级 + 运行级待验证）。
- [ ] **Step 2: exercises/04-port-rate-limit —— 按端口令牌桶（完整可编译）**：
  - `fw_common.h`：复用阶段 2 结构 + 练习专用结构：

```c
/* ===== 练习 04 新增：按端口令牌桶 ===== */
#define RL_MAX_PORTS 256

/* 令牌桶状态：tokens 定点数（1 = 1/1024 个报文额度），避免浮点（verifier 限制） */
struct rl_value {
    __u64 tokens;      /* 当前令牌，Q8.10 定点 */
    __u64 last_ns;     /* 上次补充时间 */
};

struct rl_cfg {
    __u64 rate_pps;    /* 每秒允许报文数（0 = 不限速） */
    __u64 burst;       /* 桶容量（报文数），吸收突发 */
};
```

  - `ratelimit.bpf.c`：解析与计数骨架（`parse_flow`/`bump`）从阶段 2 `acl.bpf.c` 逐字复制，但**省略 `acl_rules`/`acl_lpm` 两张 map 与 `acl_verdict`**——本练习的策略只有限速；`rate_cfg`（hash，key `__u16 dport`，value `rl_cfg`，loader 下发）+ `rate_state`（LRU hash，同 key，value `rl_value`，数据面维护）；主程序在解析成功后调用限速函数，超额 `XDP_DROP`：

```c
/* 令牌补充：tokens(Q8.10) += elapsed * rate 的折算见函数内注释（移位代替除法，
 * 规避 verifier 对除数非零的约束与除零路径）。rate==0 直接放行（未配置端口不限速）。 */
static __always_inline __u8 rl_allow(__u16 dport, __u64 now)
{
    struct rl_cfg *cfg = bpf_map_lookup_elem(&rate_cfg, &dport);
    if (!cfg || cfg->rate_pps == 0)
        return 1;                         /* 未配置端口：放行 */
    struct rl_value *st = bpf_map_lookup_elem(&rate_state, &dport);
    if (!st) {
        struct rl_value init = { .tokens = cfg->burst << 10, .last_ns = now };
        bpf_map_update_elem(&rate_state, &dport, &init, BPF_ANY);
        return 1;
    }
    __u64 elapsed = now - st->last_ns;
    /* tokens(Q8.10) += elapsed_ns * rate_pps / 1e9 * 1024 ≈ (elapsed * rate) >> 20。
     * 量级分析：elapsed ≤ 1e11（约 100s）、rate ≤ 1e7 时乘积 ≤ 1e18 < u64 上限，
     * 不会溢出；超长空闲后首包的少量少补由 burst 封顶掩盖，教学可接受 */
    st->tokens += (elapsed * cfg->rate_pps) >> 20;
    if (st->tokens > (cfg->burst << 10))
        st->tokens = cfg->burst << 10;               /* 上限封顶 */
    st->last_ns = now;
    if (st->tokens < (1 << 10))
        return 0;                                    /* 不足 1 个额度：超额 DROP */
    st->tokens -= (1 << 10);
    return 1;
}
```

  - `ratelimit.c`：`--limit <port>,<pps>,<burst>` 下发（可重复）+ attach + 统计轮询；burst 缺省 = rate（1 秒突发）。
  - `README.md`：防剧透结构；讲解令牌桶定点数、`elapsed` 折算、多 CPU 写竞争近似。
- [ ] **Step 3: 练习 README 防剧透结构（两项统一，逐节固定）**：
  1. `## 任务`（面向学生的任务描述与验收标准）　2. `## 提示`（涉及哪些 helper/map/结构，不给答案）　3. `## 环境前提与构建`　4. `## 预期输出示例`（运行级待验证）　5. `## 参考实现思路`（独立成节，防剧透分界线 `---` 在第 4 节之后）　6. `## 验证状态`　7. `## 参考来源`。
- [ ] **Step 4: 编译验证（硬性）**

Run: `make -C book/examples/chapter-17/exercises/01-ipv6-acl && make -C book/examples/chapter-17/exercises/04-port-rate-limit`
Expected: 两个目录退出码均 0。

- [ ] **Step 5: 追加 `.gitignore` 并提交**

```bash
printf '%s\n' 'book/examples/chapter-17/exercises/01-ipv6-acl/acl' \
              'book/examples/chapter-17/exercises/04-port-rate-limit/ratelimit' >> .gitignore
git add book/examples/chapter-17/exercises/ .gitignore
git commit -m "feat(ch17): 课后练习参考实现（IPv6 ACL 与按端口限速）"
```

---

### Task 13: 练习参考实现（控制面：02-rule-persistence、03-prometheus-export）与 exercises 总览

**tasks.md 条目**：§5.1.4「为每项课后任务提供完整可编译的参考实现与实现思路说明，存放于 `book/examples/chapter-17/exercises/<nn>-<name>/`，练习 README 采用防剧透结构（任务与提示在前、参考实现思路独立成节），并标注验证状态。」（控制面两项）

**Files:**
- Create: `book/examples/chapter-17/exercises/02-rule-persistence/`：`bpf/`（复制 05-firewall 的三份 BPF 源，构建体系复用其 Makefile 模式）、`user/`（复制 05-firewall 四个 Go 文件 + 新增 `persist.go`）、`rules.example.json`、`Makefile`、`README.md`
- Create: `book/examples/chapter-17/exercises/03-prometheus-export/`：同上结构 + 新增 `exporter.go`（`stats.go` 不改动）
- Create: `book/examples/chapter-17/exercises/README.md`（四项总览 + 防剧透结构说明）
- Modify: `.gitignore`：追加两个练习产出的 `fwctl`

**Interfaces:**
- Consumes: Task 7 的 user/ 四文件与 Makefile 模式（Makefile 目标 `fwctl`，`user` 目录结构一致）。
- Produces: `persist.go`（`Save/Load`，JSON 格式）；`exporter.go`（`ServeMetrics(listen, statsMap)`）。

- [ ] **Step 1: exercises/02-rule-persistence**：`persist.go` 核心（其余为 05-firewall user/ 复制品）：

```go
package main

// 规则持久化：保存为 JSON，重启后 --restore 恢复。
// 「原子替换」口径：恢复时逐条 Put（hash map 逐 key 原子），不清空重建，
// 避免「先清空后下发」的瞬时全放行窗口。

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/cilium/ebpf"
)

type PersistedRule struct {
	Proto  string `json:"proto"`  // tcp/udp
	Src    string `json:"src"`    // 点分 IPv4
	Sport  int    `json:"sport"`
	Dst    string `json:"dst"`
	Dport  int    `json:"dport"`
	Action string `json:"action"` // allow/drop
}

func SaveRules(path string, m *ebpf.Map) error {
	var k FlowKey
	var v RuleValue
	var out []PersistedRule
	it := m.Iterate()
	for it.Next() {
		if err := it.Decode(&k); err != nil {
			return err
		}
		if err := it.Decode(&v); err != nil {
			return err
		}
		out = append(out, PersistedRule{
			Proto: protoName(k.Proto), Src: int2ip(k.SrcIP),
			Sport: int(be16(k.SrcPort)), Dst: int2ip(k.DstIP),
			Dport: int(be16(k.DstPort)),
			Action: map[uint8]string{0: "allow", 1: "drop"}[v.Action],
		})
	}
	if err := it.Err(); err != nil {
		return err
	}
	data, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	// 先写临时文件再 rename：rename 原子，避免半截规则文件
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func LoadRules(path string, m *ebpf.Map) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var rules []PersistedRule
	if err := json.Unmarshal(data, &rules); err != nil {
		return err
	}
	for i, r := range rules {
		k, v, err := ParseRule(r.Proto, r.Src, fmt.Sprint(r.Sport),
			r.Dst, fmt.Sprint(r.Dport), r.Action)
		if err != nil {
			return fmt.Errorf("第 %d 条规则非法: %w", i+1, err)
		}
		if err := m.Put(k, v); err != nil { // 已存在的 key 报冲突，逐条定位
			return fmt.Errorf("第 %d 条下发失败: %w", i+1, err)
		}
	}
	return nil
}
```

CLI 增补：`fwctl rules save --file rules.json` / `fwctl rules load --file rules.json`；`rules.example.json` 给出 2 条示例（一 allow 一 drop）。`protoName/int2ip/be16` 为 Task 7 rules.go 已有共享小工具，随 user/ 其余文件一并复制。

- [ ] **Step 2: exercises/03-prometheus-export**：`exporter.go`（标准库 net/http + Prometheus 文本格式，不引入第三方依赖）：

```go
package main

// Prometheus 指标导出：GET /metrics 输出 fw17_packets_total{reason="..."} 计数器。
// 设计要点：per-CPU 聚合放请求时做（导出频率低，不值得后台聚合）；
// reason label 取值固定为 statNames（C1 契约），不暴露动态 label。

import (
	"fmt"
	"net/http"
	"strings"

	"github.com/cilium/ebpf"
)

func metricsHandler(stats *ebpf.Map) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var b strings.Builder
		b.WriteString("# HELP fw17_packets_total firewall packet counters.\n")
		b.WriteString("# TYPE fw17_packets_total counter\n")
		for i, name := range statNames {
			v, err := SumPerCPU(stats, uint32(i))
			if err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			fmt.Fprintf(&b, "fw17_packets_total{reason=%q} %d\n", name, v)
		}
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		_, _ = w.Write([]byte(b.String()))
	}
}

func ServeMetrics(listen string, stats *ebpf.Map) error {
	http.HandleFunc("/metrics", metricsHandler(stats))
	return http.ListenAndServe(listen, nil) // 默认 :9100，需在容器/主机放行端口
}
```

CLI 增补：`fwctl export --listen :9100`（阻塞运行）。

- [ ] **Step 3: `exercises/README.md`**：四项任务总览表（与章末表格一致）+ 防剧透阅读法（先做后看）+ 各练习构建命令 + 验证状态汇总表（编译级/运行级）。
- [ ] **Step 4: 编译验证（硬性）**：`make -C book/examples/chapter-17/exercises/02-rule-persistence && make -C book/examples/chapter-17/exercises/03-prometheus-export` 退出码均 0（Go 依赖失败按 C6 处理）。
- [ ] **Step 5: 追加 `.gitignore` 并提交**

```bash
printf '%s\n' 'book/examples/chapter-17/exercises/02-rule-persistence/fwctl' \
              'book/examples/chapter-17/exercises/03-prometheus-export/fwctl' >> .gitignore
git add book/examples/chapter-17/exercises/ .gitignore
git commit -m "feat(ch17): 课后练习参考实现（规则持久化与 Prometheus 导出）+ exercises 总览"
```

---

### Task 14: 交叉链接、全量编译级验证与状态标注（§5.1.5 前两条）

**tasks.md 条目**：§5.1.5「Cross-link Chapter 17 prose to the standalone `book/examples/chapter-17/` stage directories and exercise assets after they are generated.」＋「Run `book/examples/chapter-17/scripts/verify-build.sh` compile-level verification locally (all stages + exercises); fix any compile failures. Verify load/runtime behavior in a privileged Linux eBPF environment or mark each component with explicit verification status and reason.」（运行级行为的落点=按 C6 全量标注，见 Step 3）

**Files:**
- Modify: `book/chapters/17-high-performance-ebpf-firewall.md`（交叉链接）

**Interfaces:**
- Consumes: Task 2–13 全部产物；chapter-09 的链接惯例（相对链接 `../examples/chapter-17/...` + 仓库根路径文本）。
- Produces: 全绿 verify-build 记录；各 README 验证状态区终稿。

- [ ] **Step 1: 交叉链接**：章首加「本章独立示例已全部落地到 [`book/examples/chapter-17/`](../examples/chapter-17/)」与目录总览；每个阶段小节末尾加「**独立示例**：`../examples/chapter-17/<stage>/`」链接；每个正文代码块前后标注对应文件路径；课后扩展任务节链接 exercises/ 各目录。格式与第 9 章一致（相对路径链接 + 仓库根路径文本）。
- [ ] **Step 2: 全量编译级验证（硬性验收）**

Run: `bash book/examples/chapter-17/scripts/verify-build.sh`
Expected: 9 个目标（5 阶段 + 4 练习）全 PASS；WARN 仅允许出现在 Go 网络依赖且需在对应 README 标注；`FAIL=0`、退出码 0。任何 FAIL 回到对应任务修复后重跑。
记录：把本次运行摘要（PASS/WARN/FAIL 数与命令版本）写入 `book/examples/chapter-17/README.md` 验证状态说明区。

- [ ] **Step 3: 验证状态全量标注核对**：逐个打开 6 阶段 + 4 练习 README，确认每份都有 C6 口径的「验证状态」节（编译级=本机已过；运行级=待验证+原因 `unprivileged_bpf_disabled=2`/无 root）；正文 1.5 节验证状态表与各 README 一致。矩阵条目「Verify firewall examples in isolated privileged network lab or mark each component with explicit verification status.」以本步骤 + Task 8/14 记录为完成口径。
- [ ] **Step 4: 篇幅终检（硬性）**

Run: `wc -m book/chapters/17-high-performance-ebpf-firewall.md`
Expected: 40000–60000。低于下限：优先补原理讲解而非注水（设计 §7）；高于上限：压缩重复叙述，不删原理。
Run: `grep -c "](../examples/chapter-17/" book/chapters/17-high-performance-ebpf-firewall.md`
Expected: ≥ 7（五个阶段目录 + exercises + 顶层 README）。

- [ ] **Step 5: 提交**

```bash
git add book/chapters/17-high-performance-ebpf-firewall.md book/examples/chapter-17/
git commit -m "docs(ch17): 交叉链接示例资产并完成全量编译级验证标注"
```

---

### Task 15: 主审（teaching completeness）与 tasks.md 勾选（§5.1.5 收口）

**tasks.md 条目**：§5.1.5「Main-agent review: audit Chapter 17 against the teaching completeness requirement (five-layer environments, core-code annotations, dual perspective, exercises with reference implementations).」；同时收口矩阵节五条（引用见下方清单）。

**Files:**
- Modify: `openspec/changes/generate-ebpf-book/tasks.md`（勾选）
- 产出：审查报告进入最终回复（不创建报告文件）

- [ ] **Step 1: 按 §8「Chapter Execution Workflow」要求逐项审计并记录结论**：
  1. 五层环境（1.1–1.5 内容口径齐备，check-env/setup-veth/fwctl/预期输出全部被引用）；
  2. 核心代码注释（抽查 3 份 `.bpf.c` + 2 份 Go 文件：字段语义/边界检查原因/helper 选型/map 选型/失败模式五类注释在场）；
  3. 双视角（八个主要小节教师视角 + 学生动手/检查点齐备）；
  4. 课后练习（4 项目录、防剧透 README 结构、参考实现可编译、验证状态标注）；
  5. 矩阵专项：`Main-agent review for safety warnings, NAT/checksum accuracy, and benchmark interpretation.` —— 安全警告（1.3 节 + 各 README + 两个 bench 脚本头）、NAT/校验和讲解（阶段 4/六节改写顺序与增量算法）、压测解读（八节指标口径与失败模式表）；
  6. 结构级审计（设计 §9）：学习目标/导读/可视化决策树/排错/总结/参考来源在场；代码块与示例文件一一对应（抽查 5 处路径）；
  7. 语言规范：术语一致（映射/辅助函数/挂载点）、无空泛提纲句。
  
  审计发现问题回对应任务修复后复验；全绿才进入 Step 2。

- [ ] **Step 2: 勾选 tasks.md**：将 §5.1.0（1 条）、§5.1.1（5 条）、§5.1.2（8 条）、§5.1.3（7 条）、§5.1.4（4 条）、§5.1.5（3 条）与矩阵节 Chapter 17 的 5 条未勾选项全部置为 `[x]`（仅当对应交付物真实存在且验收通过；Go 网络依赖 WARN 的项在勾选同时在该行末尾加注「（Go 依赖拉取待网络复验）」）。

- [ ] **Step 3: 最终提交与交付报告**

```bash
git add openspec/changes/generate-ebpf-book/tasks.md
git commit -m "chore(ch17): 勾选 §5.1 与 Chapter 17 矩阵任务（build 阶段完成）"
```

按 §8 workflow 在回复中列出：变更文件清单、参考来源、已执行验证（编译级全绿记录）、已知限制（运行级待验证清单、Go 网络依赖状态）。

---

## 计划自查记录（writing-plans Self-Review）

1. **Spec 覆盖**：§5.1.0→Task 1；§5.1.2 八条→Task 2–8（README/00-env/四阶段 C/05/scripts 逐条对应）；§5.1.3 七条→Task 9（其中「五层开篇」交叉要求由 Task 10 承接）；§5.1.1 五条→Task 10；§5.1.4 四条→Task 11（前两条+练习设计）与 Task 12/13（参考实现）；§5.1.5 三条→Task 14（前两条）与 Task 15（主审）；矩阵 5 条→Task 2–14（生成/验证/交叉链接）与 Task 15（upgrade 收口 + 专项主审 + 勾选）。无遗漏条目。
2. **占位符扫描**：全部任务给出实际内容（代码/口径/命令/预期结果）；唯一带「按口径增补」性质的是 Task 13 的两条 CLI 子命令（已给出 persist.go/exporter.go 完整核心与 CLI 口径），无未定义引用；计划中无「TBD/TODO/类似 Task N」表述。
3. **类型一致性**：`enum stat_idx` 顺序与 Go `statNames` 对齐（C1）；map 名/类型/max_entries 单一来源（C3）；SEC/函数名与 skeleton 前缀固定（C4）；Go 镜像结构与 C 结构逐字节对应（C2）；Makefile 目标名替换表（C7）；拓扑地址单一来源（C5）；验证状态口径（C6）被所有 README/正文任务引用。
4. **硬性验收**：编译级验证在 Task 3/4/5/6/7/12/13（单目录）与 Task 8/14（全量）均为显式步骤，退出码 0 为通过标准；运行级全部「预期输出示例 + 运行级待验证」。

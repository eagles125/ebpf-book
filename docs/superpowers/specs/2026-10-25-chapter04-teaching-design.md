---
comet_change: generate-ebpf-book
role: technical-design
canonical_spec: openspec
---

# 第 4 章完整教学章节技术设计（eBPF 程序类型与钩子点深入，机制章 + 类型画廊实战化）

- 日期：2026-10-25
- 上游事实源：`openspec/changes/generate-ebpf-book/`（proposal / design / specs / tasks）
- 设计范围：tasks.md「Chapter 4: Program Types and Hook Points」全部 7 条任务——第 4 章正文从零写为完整教学章节，落地独立示例资产（类型画廊 + 两个集成锚点）与课后扩展任务参考实现；完成即勾选 Batch 2（第 2–4 章）最后一项。
- 模式先例：沿用第 2/3/17 章已验证的工作模式（设计文档 → 实施计划 → subagent 逐任务构建 + thorough 逐任务审查 → 终审修复波），教学规范遵循 `book/STYLE.md`（完整章节模板 + 实战章节教学模板）。

## 1. 背景与目标

第 2 章以 tracepoint 一类展开「类型 = 上下文 + helper 面 + 返回值语义」，并把完整谱系（XDP、TC、cgroup、kprobe、fentry/fexit、LSM 等）交给第 4 章逐类展开；第 3 章末尾立桥：「读第 4 章时随身带上两件行李——2.3 类型表当字典，排错速查表当地图」。第 4 章《eBPF 程序类型与钩子点深入》必须兑现：把「类型决定什么」从一句话变成可操作的选择体系（`bpf_prog_type` 与 `bpf_attach_type` 的两层模型、ctx 布局差异、返回值语义、helper 可用面），并让读者对同一事件亲手戴上多副「眼镜」。本设计同时为第 15/16 章（XDP/TC 深入）与第 20 章（CO-RE 依赖 fentry/BTF）留出接缝。

本设计属章级分叉决策（按推荐项采用，**待用户复核，可推翻**）：

| 决策 | 内容 |
|------|------|
| 章节定位 | 机制章（完整章节模板）+ 完整实战化（示例资产触发教学模板四条规范） |
| 覆盖划分 | 矩阵 14 类全覆盖：「深入」7 类由两个集成锚点承载（tracepoint/raw_tp/kprobe/fentry/fexit + XDP/TC），「目录级」8 类由类型画廊承载（kretprobe/uprobe/cgroup-skb/socket-filter/sockops/sk-msg/lsm/netfilter，compile-only）；perf event 不占画廊（第 9 章主场），对比表中一行带过 |
| 集成锚点 | ①`01-multi-observe`：同一事件（openat2）五副眼镜——tracepoint/raw_tp/kprobe/fentry/fexit 同挂 do_sys_openat2，一个 loader 挂 5 个 link、共享计数 map 分镜展示（矩阵「≥2 集成示例」之追踪类）②`02-xdp-tc`：同一报文两种 ctx——XDP 与 TC 同挂 lo，xdp_md vs __sk_buff 对照（网络类）。安全类（LSM）以画廊 compile-only 呈现 |
| 画廊形态 | 正面画廊（与第 3 章失败画廊区分）：每类一个最小可编译程序 + 六段 README（任务一句话/代码走读/挂载方式与前提/预期输出示例/验证状态/参考来源）——「每类怎么写对」，失败形态回指第 3 章 |
| 练习规模 | 2 项：①选型决策（场景→类型+attach+骨架论证，参考实现=选对类型的完整可编译程序）②类型错位修复挑战（编译通过但加载被拒的错位程序，衔接第 3 章日志诊断），各带完整参考实现 + 防剧透 README |
| 篇幅 | 35–54k 字符（`wc -m` 口径【Task 7 实现期回写：三节 brief 硬性结构（109 行全文贴入+14 card×六段+三协调点）两轮压缩后实测 wc 17.0k 超 §3.3 预算 ~2k，裁定放宽章级上限保结构完整，分节预算不动】；14 类目录级覆盖 + 全类型大对比表天然偏大，分节预算见 §3.3） |
| 验证策略 | 沿用第 2/3 章：编译级真实验证（本机 clang 18.1.3 / bpftool 7.7.0 / libbpf-dev 1.3.0，内核 7.0.0-38 BTF 可用，CONFIG_BPF_LSM=y）+ 加载级一律「预期输出示例」标注「运行级待验证」（无 root，`unprivileged_bpf_disabled=2`） |
| 签名基准 | fentry/fexit/kprobe 目标函数签名以**本机 BTF 解析**为准并注明「签名随内核版本演进」（实测例证：本机 do_unlinkat 已内联缺席 BTF、vfs_unlink 第 4 参数为 `struct delegated_inode *` 而上游 6.x 为 `struct inode **`——漂移本身进教学） |

## 2. 示例资产设计

### 2.1 目录结构

```text
book/examples/chapter-04/
├── README.md                # 章级导读：示例总览表（2 锚点 + 8 画廊 + 2 练习）、构建模式、
│                            #   验证状态约定（画廊 compile-only 口径）、加载级待验证边界说明
├── 00-env/
│   └── check-env.sh         # 环境自检：内核/BTF/clang/bpftool/libbpf/权限分级（无特权可跑），
│                            #   增加 tracefs 与 CONFIG_BPF_LSM 检查项（ch04 特有）
├── 01-multi-observe/        # 集成锚点①：同一事件五副眼镜（追踪类）
│   ├── multi_observe.bpf.c  #   一个对象 5 个程序同挂 openat2 事件：tracepoint（typed ctx，
│   │                        #   bpf_probe_read_user_str 读文件名）/raw_tp（无类型 args 强转）/
│   │                        #   kprobe（BPF_KPROBE 从 pt_regs 取参）/fentry（BTF 直接类型化）/
│   │                        #   fexit（+返回值 fd/错误码）；共享 ARRAY map 按镜编号计数并存
│   │                        #   各镜最近一次读到的文件名
│   ├── multi_observe.c      #   C loader：open→load→bpf_program__attach() ×5（libbpf 自动按
│   │                        #   SEC 挂载）→每秒读 map 打印 5 行分镜计数；Ctrl-C 卸载
│   ├── Makefile             #   vmlinux.h → .bpf.o → skel.h → loader（C4 全链模板）
│   └── README.md            #   十段结构（ch02 口径）
├── 02-xdp-tc/               # 集成锚点②：同一报文两种 ctx（网络类）
│   ├── xdp_count.bpf.c      #   XDP 视角：xdp_md、data/data_end 手工解析、包计数+UDP 计数
│   ├── tc_count.bpf.c       #   TC 视角：__sk_buff、data/data_end 同样可用但语义不同
│   ├── xdp_tc.c             #   C loader：XDP 挂 lo + TC 挂 lo clsact（bpf_tc_attach，沿 ch17 先例）
│   ├── Makefile             #   全链模板（两个 .bpf.o 进一个 loader）
│   └── README.md            #   十段结构；运行需 root（待验证），无 root 用画廊观察 ctx 类型
├── 03-type-gallery/         # 目录级画廊：8 类 compile-only（clang 必须通过）
│   ├── README.md            #   画廊总览：每类一行——prog_type/attach_type/ctx 类型/返回值语义/
│   │                        #   helper 面一句话/挂载前提；阅读顺序建议
│   ├── 01-kretprobe/        #   BPF_KRETPROBE 读返回值（编译级；返回值读取的架构差异注）
│   ├── 02-uprobe/           #   用户态探针：SEC("uprobe/<二进制>:<偏移>") 命名规则讲清（编译级）
│   ├── 03-cgroup-skb/       #   cgroup/skb：__sk_buff + 返回值 1/0 语义
│   ├── 04-socket-filter/    #   socket filter：返回值=是否投递（经典 tcpdump 载体）
│   ├── 05-sockops/          #   sockops：连接生命周期回调（TCP 状态切换点）
│   ├── 06-sk-msg/           #   sk_msg：sockmap 重定向判决（返回 SK_PASS/SK_DROP）
│   ├── 07-lsm/              #   LSM：BPF_PROG(lsm/file_open) 返回 0/1 决策语义；CONFIG_BPF_LSM
│   │                        #   与 lsm=bpf 启动参数前提（本机 =y 实证，运行级仍待验证）
│   └── 08-netfilter/        #   netfilter：bpf_nf_ctx + NF_ACCEPT/NF_DROP；与 ch09 09-netfilter-go
│                            #   完整示例交叉引用；本机 7.0 ≥ 6.6 满足内核要求
├── scripts/
│   └── verify-build.sh      # 单章编译级验证：00-env + 两锚点全链 + 画廊 8 目录 + exercises
│                            #   逐目录 make 汇总（模式同 ch02/ch03）
└── exercises/               # 课后扩展任务 + 完整可编译参考实现
    ├── README.md            #   任务总览（与正文课后表逐字一致）+ 防剧透结构说明
    ├── 01-type-selection/   # 练习1：选型决策——三个场景（每容器文件访问审计/出站连接限速/
    │   │                    #   内核函数入口参数抓取）各选类型+attach 并写骨架
    │   ├── solution.bpf.c   #   参考实现：按正确选型合并的最小可编译程序（分 SEC 三程序）
    │   ├── Makefile
    │   └── README.md        #   防剧透七段；参考实现思路=选型论证表（含错误选项为何不行）
    └── 02-type-mismatch/    # 练习2：类型错位修复挑战——一段逻辑放进错误 SEC/用错 ctx 类型
        ├── broken.bpf.c     #   编译通过、加载预期被拒（如 tracing 程序里按 xdp_md 解引用）
        ├── fixed.bpf.c      #   参考实现：类型/SEC 归位版
        ├── Makefile
        └── README.md        #   防剧透七段；预期日志（加载级待验证，口径沿第 3 章）
```

### 2.2 设计原则

- **「同一事件多副眼镜」是本章的教学主轴**：01 锚点让 ctx 布局差异、类型化程度（typed→untyped→pt_regs→BTF 直参）、返回值语义（fexit 才有返回值）全部落在一组可 diff 的实物上；对比表与画廊再横向铺开。
- **正面画廊与失败画廊分工**：第 3 章讲「为什么被拒」，第 4 章讲「每类怎么写对」；画廊 README 的失败模式段一律回指第 3 章对应 card，不重复展开。
- **compile-only 口径同第 3 章**：画廊 8 类与练习 broken/fixed 必须 clang 编译通过；锚点 01/02 为全链（skeleton + loader 编译链接通过），加载运行级一律「预期输出示例 / 运行级待验证」。
- **签名以 BTF 为准 + 漂移进教学**：fentry/fexit/kprobe 的目标函数签名由 plan 提供的 BTF 解析脚本产出；本机实测漂移例证（do_unlinkat 内联缺席、vfs_unlink 签名演进）写进正文与代码注释，配「签名随内核版本演进，加载期由 BTF 校验」保守口径。
- **代码注释规范**（STYLE.md 实战模板规则 2）：全部 `.bpf.c` 中文注释覆盖五类要点（字段语义/边界检查原因/helper 选型/map 选型/失败模式）；loader 覆盖 attach 选型与失败模式。
- **产物不入库**：`.gitignore` 已有全局规则（vmlinux.h/*.skel.h/*.o）；loader 可执行物按任务追加条目。

## 3. 章节正文设计

### 3.1 章节结构（完整章节模板）

```text
# 第 4 章 eBPF 程序类型与钩子点深入
学习目标
本章导读（回收 ch02:72/ch03:1123 两处预告；本章问题——「同一个 BPF 程序为什么不能
  到处挂」；学习路线：先类型系统、再内核注册机制、再画廊逐类、最后实战选型）
一、程序类型系统：类型决定什么（bpf_prog_type 与 bpf_attach_type 两层模型；类型
  三元组——ctx 布局/helper 可用面/返回值语义，各配最小例；14 类总对比表：触发源/
  ctx/返回值/稳定性/典型用途；选型决策树）
二、类型与钩子的内核实现分析（include/uapi/linux/bpf.h 的 bpf_prog_type 枚举与
  kernel/bpf/bpf_types.c 的 BPF_PROG_TYPE 注册表→bpf_verifier_ops 按类型登记
  [ch03 2.6 回指]；is_valid_access/ctx 转换 convert_ctx_accesses——ctx 字段如何
  变成安全的内部偏移；SEC 命名与 libbpf 解析[bpf_helpers.h SEC 名义法]；bpf_link
  与挂载：link_create/自动 attach；返回值校验与 enforce]）
三、类型画廊：十四类逐类走读（3.1–3.7 五副眼镜对比[01 锚点五程序逐段]；3.8–3.14
  目录级 8 类 card[背景/代码/挂载前提/返回值语义/与相邻类型的选择边界]；与
  03-type-gallery 一一对应）
四、实战：为任务选对类型并挂上钩子（5.1 五层环境[紧凑]：依赖[tracefs/CONFIG_BPF_LSM]/
  编译/部署[挂载即部署，XDP/TC 需 veth 或 lo] /运行/效果与验证状态约定；5.2 代码
  走读[教师视角引入 + 核心代码原理讲解 + 学生动手 + 检查点]：先 01-multi-observe
  同一事件看五镜计数，再 02-xdp-tc 同一报文看两种 ctx）
可视化：类型谱系与挂载路径（bpf_prog_type→bpf_attach_type→挂载点三层文本图；
  同一事件五副眼镜数据流图）
常见问题与排错（选型速查表：症状→类型规则→修复；SEC 名写错的加载报错形态；挂载
  点不存在/权限档位差异；fentry 签名漂移的报错形态与 BTF 解析法）
课后扩展任务（2 项，指向 exercises/，表格与 exercises/README.md 逐字一致）
总结
本章参考来源（kernel docs program types/attach 点文档、bpf_helpers/bpf_tracing 头
  文档、BPF/XDP/LSM 经典 LWN 与 Patchwork 讨论、Andrii Nakryiko libbpf 系列文章）
```

### 3.2 教学规范落点

- **五层环境阐述**（实战模板规则 1）：「四、实战」5.1 承载（紧凑版 3–4k）：依赖层补 ch04 特有项——tracefs 可读性、CONFIG_BPF_LSM 与 `lsm=bpf` 启动参数、netfilter 需 6.6+；部署层讲 XDP/TC 的挂载位置差异（驱动层 vs qdisc）。
- **双视角**：一、三、四节落「**教师视角**」「**学生动手**」引导段与编号检查点（2–4 问+提示）。
- **四段式**（规则 3）：不约束本章逐类 card（非渐进阶段章节）；「四、实战」5.2 内以四段组织（同 ch03 先例）。

### 3.3 篇幅预算

目标 35–54k 字符（`wc -m`）：目标+导读 2–2.5k、一（类型系统）6–8k、二（内核实现）8–10k、三（画廊走读）12–15k、四（实战含五层）6–8k、可视化 1.5–2k、排错 2.5–3.5k、课后任务与总结/参考 2.5–3.5k。

## 4. 课后练习设计

| 练习 | 扩展维度 | 参考实现要点 |
|------|---------|-------------|
| 01-type-selection | 选型决策 | 三个真实场景（cgroup 容器文件审计→cgroup/lsm 类、出站连接限速→sockops/sk_msg 或 TC、内核函数入口抓参→fentry/kprobe 对比）各给出选型论证与骨架；参考实现=合并三个 SEC 的可编译程序，README 论证「错误选项为什么不行」（选型边界而非分类学） |
| 02-type-mismatch | 类型错位修复 | broken：一段报文解析逻辑放在 tracing SEC 且按 xdp_md 字段解引用（编译通过、加载预期被拒——ctx 访问越权/is_valid_access 拒绝路径）；fixed：SEC 与 ctx 类型归位；预期日志逐行解读回指第 3 章 2.3/3.6 |

- 每个练习：完整可编译源码 + 防剧透 README（任务与提示在前，参考实现思路独立成节放后部）。
- 验证状态：编译级「已验证（本机）」+ 运行级一律「待验证」。

## 5. 验证策略（编译级）

`scripts/verify-build.sh`（单章版，模式同第 2/3 章）：

1. BTF 前置检查（`/sys/kernel/btf/vmlinux`）；
2. `00-env` 自检、`01-multi-observe`、`02-xdp-tc`（全链 make）、画廊 8 目录、exercises 两目录逐个 `make`；
3. 汇总 `PASS/FAIL`，退出码 0=全绿；结果回填章级 README 验证状态说明区。

本机全部可执行（无网络依赖）；加载级（实际挂载与事件触发）按约定标注「预期输出示例 / 运行级待验证」。

## 6. 技术风险与边界

| 风险 | 缓解 |
|------|------|
| fentry/fexit/kprobe 目标签名随内核版本漂移（实证：本机 do_unlinkat 内联、vfs_unlink 签名演进） | 锚点选 5.10+ 签名稳定的 do_sys_openat2；plan 提供机器可复现的 BTF 解析脚本；正文/注释双口径「以本机 BTF 为准，加载期由 BTF 校验」 |
| 14 类逐一深入导致章节膨胀 | 深入/目录两级覆盖（2 锚点 vs 8 画廊 card）；对比表横向压缩；单 card 1.2–1.7k |
| 与 ch02/ch03 重复（tracepoint 已用过、白名单已讲） | 分工：ch02 用类型不展开、ch03 讲白名单机制；ch04 逐类展开 + 选型边界；回指不重复 |
| LSM/netfilter 等运行前提复杂（lsm=bpf、6.6+） | 前提写进画廊 README 环境段与 check-env.sh 检查项；运行级一律待验证 |
| raw_tp/tracepoint 事件命名在 libbpf 与 tracefs 间的差异 | 01 锚点 loader 注释讲清 SEC→事件查找路径；预期报错形态进排错节 |
| TC 挂载接口（bpf_tc_attach）版本差异 | 沿 ch17 04-nat-tc 已验证先例；保守措辞 |

## 7. tasks.md 回写清单

- 完成后统一勾选 tasks.md「Chapter 4: Program Types and Hook Points」7 条（由 plan 收口任务执行）；Batch 2（第 2–4 章）随之完成
- delta spec 无需变更（无 Spec Patch）；
- 第 5 章及以后、第 9 章教学改造项不在本设计范围。

## 8. 测试策略汇总

- **编译级（本机执行）**：verify-build.sh 全绿为 plan 完成条件之一；冒烟结论（15 类 SEC 全编译通过、BPF_PROG 在 bpf_tracing.h、TC_ACT_OK/NF_ACCEPT 需手工 define、do_unlinkat 内联/do_sys_openat2 签名实证、CONFIG_BPF_LSM=y）记入 plan 现状基线。
- **结构级**：完整章节模板九节齐备；五层环境、双视角、注释五类要点、可视化、排错、总结、参考、课后任务全部在场；正文代码块与示例文件一一对应；14 类覆盖齐全（2 锚点 7 类 + 画廊 8 类，矩阵口径 fentry/fexit 计一）。
- **运行级**：预期输出 + 待验证标注；由特权环境/读者复验。

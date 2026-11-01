# 第 7 章独立示例资产

本目录是《第 7 章 辅助函数与映射的内部实现》的配套资产,对应正文「一、辅助函数:从 call 指令到白名单」「二、hash 映射内部:并发与内存」「三、per-CPU、LRU 与内存记账」「四、事件输出:perf buffer 与 ring buffer」四节。本章是**运行章**:与第 5/6 章的纯走读不同,这里既有切片器与机检(走读轨),也有三个可编译可加载的运行型示例(运行轨)——BPF 侧用 clang 编译、用户态用 gcc+libbpf 链接,构建在本机(无 root)即可完整复现;加载与运行需 root,输出按「预期输出示例」口径标注。

## 演示目标

把「程序依赖的两样东西——辅助函数与映射——怎么实现」拆成四件可操作的事:

- **helper 分发与白名单**(`04-helper-dispatch/`):切片器五站——`__bpf_call_base` 基址、`bpf_base_func_proto` 公共兜底、验证器 `get_func_proto` 解析链、按程序类型的白名单实例、`struct bpf_func_proto` 原型结构;
- **hash 映射内部**(`05-map-internals/`):切片器六站——`bpf_map_types[]` 注册表、`bpf_map_ops` 接口表、per-CPU 桶锁、更新三语义、删除与 RCU 回收、memcg 内存记账;
- **运行型示例三件**(`01/`、`02/`、`03/`):hash 查找/更新闭环(XDP on lo)、per-CPU 计数器与用户态聚合、事件输出双变体(ringbuf vs perf buffer)——编译级已验证,运行为预期档;
- **可机检的引用**(`scripts/` + 树根 `citations.txt`):符号机检同第 5/6 章契约。

## 目录结构

| 目录 | 类别 | 内容 | 落地状态 |
|---|---|---|---|
| `00-env/` | 环境自检 | `check-env.sh`:走读树八源文件 / 双侧工具链五件 / 权限档位 / BTF / CONFIG 两项,末尾三档结论 | 已落地:本机实跑通过(走读级+编译级 PASS;加载级预期档) |
| `01-hash-lookup-update/` | 运行型示例① | `xdp_hash.bpf.c`+`loader.c`+`Makefile`:XDP on lo 按以太类型计数到 BPF_MAP_TYPE_HASH,BPF 侧 update(NOEXIST→EXIST)、用户态周期读出 | Task 2 落地 |
| `02-percpu-counter/` | 运行型示例② | `xdp_percpu.bpf.c`+`loader.c`+`Makefile`:BPF_MAP_TYPE_PERCPU_ARRAY 每包计数,用户态逐 CPU 读出聚合 | Task 6 落地 |
| `03-event-output/` | 运行型示例③ | `ringbuf.bpf.c`+`ringbuf.c`+`perf.bpf.c`+`perf.c`+`Makefile`:同一事件源两条通道,用户态分别 `ring_buffer__poll` / `perf_buffer__poll`,README 给选型对照 | Task 7 落地 |
| `04-helper-dispatch/` | 源码切片器 | `trace-helpers.sh`:helper 分发五站(call_base/base_func_proto/解析链/白名单实例/func_proto 结构) | Task 4 落地 |
| `05-map-internals/` | 源码切片器 | `trace-map-internals.sh`:映射内部六站(类型注册表/ops 接口/桶锁/更新/删除 RCU/记账) | Task 5 落地 |
| `scripts/` | 符号机检 | `verify-citations.sh`:机检树根 citations.txt,输出 TOTAL/PASS/FAIL 汇总 | Task 3 落地 |
| `citations.txt` | 锚点清单 | 树根,五组约 40 行(helper 分发/白名单/hash 内部/per-CPU·LRU·记账/事件输出与 UAPI) | Task 2 落地 |
| `exercises/` | 课后扩展 | `01-map-op-chain/`(调用链查证:README 防剧透任务书 + `answer.md` 9 跳链表 + `check-chain.sh` 六锚点机检)、`02-extend-ringbuf/`(ringbuf 切片扩展:`trace-ringbuf.sh` 两站 + `check-ringbuf.sh` 七项验收),防剧透结构 | Task 13 落地:两练习机检实跑全绿(check-chain TOTAL=6 PASS=6;check-ringbuf TOTAL=8 PASS=8,均 exit 0) |

## 学习路径

1. `bash 00-env/check-env.sh` 自检:确认走读级+编译级两档 PASS(本机无 root 即可);加载级档位看③。
2. 走读轨:`scripts/verify-citations.sh` 机检全绿后,对照正文一节跑 `04-helper-dispatch/trace-helpers.sh`、二/三节跑 `05-map-internals/trace-map-internals.sh`。
3. 运行轨:`01-hash-lookup-update/` 先跑通 `make`(编译级),再按各示例 README 的运行命令上机(需 root;运行为预期档)。
4. `03-event-output/` 对照阅读:同一事件源、两条通道、两种用户态轮询——正文四节的实证面。
5. `exercises/`:先自行完成任务,再看各练习 README 后部的参考实现(防剧透)。

## 验证状态约定(三档口径)

- **走读级与编译级:已验证可复现**——机检/切片器对走读树实跑;三示例 `make` 全链(clang .bpf.o → bpftool gen skeleton → gcc -lbpf)本机真实构建通过,零告警。
- **加载级与运行验证:预期输出示例(运行级待验证)**——加载/attach/map 读数/事件输出需 root(本机 `kernel.unprivileged_bpf_disabled=2`,非特权加载彻底禁用);各 README 中的「预期输出」为教学说明,不是本地运行结果。
- **边界口径**:映射内存记账的 memcg 形态、helper 白名单构成均随内核版本演进,以所用内核源码为准;行号一律锚定 v6.6.78 走读树。

## 机检汇总

本机终跑实测(脚本 `scripts/verify-citations.sh`,数据源为本目录树根 `citations.txt`):

```
TOTAL=44 PASS=44 FAIL=0 WALK_TREE=/work/linux/linux-6.6.78 TREE_VERSION=v6.6.78
```

退出码 0;负路径 `WALK_TREE=/nonexistent` 时逐条 FAIL、退出码 1(门禁行为符合设计)。44 条锚点分五组:①helper 分发 8 条;②白名单 5 条;③hash 内部 11 条;④per-CPU/LRU/记账 7 条;⑤事件输出与映射总纲 13 条。逐条说明见 `citations.txt` 行尾注释。

## 走读树版本声明

本目录全部机检与切片脚本默认针对 `/work/linux/linux-6.6.78` 实跑;正文所有内核路径/行号引用锚定该树,行号一律标注「(v6.6.78)」并注明随版本漂移。换用其他版本树时以 `WALK_TREE=<路径>` 覆盖默认值——机检按符号不按行号,不受行号漂移影响。

## 安全与只读声明

切片器与机检脚本只读源码树与系统文件,不构建、不加载。运行型示例只编译不加载(构建期无任何内核交互);加载示例时 **XDP 只允许挂 lo 回环口,严禁挂物理网卡**(后果与规范见第 17 章 00-env 的安全警告);root 档运行请在实验环境进行。

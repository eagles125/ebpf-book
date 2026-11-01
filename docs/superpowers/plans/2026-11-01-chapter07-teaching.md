---
change: generate-ebpf-book
design-doc: docs/superpowers/specs/2026-11-01-chapter07-teaching-design.md
base-ref: c0b14e7
---

# 第 7 章完整教学章节(build 阶段)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 将第 7 章《辅助函数与映射的内部实现》从零写为 35–50k 字符(wc -m)的完整教学章节(机制章 + 编译级实证),并落地 `book/examples/chapter-07/` 全部资产:00-env 自检、01-hash-lookup-update / 02-percpu-counter / 03-event-output 三个运行型示例(libbpf-bootstrap 构建,编译级已验证/运行级待验证)、04-helper-dispatch 与 05-map-internals 两个切片器、scripts/verify-citations.sh 符号机检、2 项练习(防剧透+机检)。

**架构:** 双轨资产——走读轨(切片器+机检,同 ch05/ch06 契约)与运行轨(三个可编译示例,libbpf-bootstrap 模式,bpftool gen skeleton 依赖本机 BTF)。citations.txt 在章级树根,是机检唯一数据源;切片器五站/六站对走读树实跑,正文一/二/三节逐站引用;示例 01/02 供一/二/三节「独立示例」链接,示例 03 是四节 perf vs ringbuf 对照的实证面。

**技术栈:** bash(grep/sed/wc)、C 双侧(BPF 侧 clang `-target bpf`;用户侧 gcc 13.3+libbpf 1.3.0,`-lbpf -lelf -lz`)、bpftool 7.7.0(btf dump/gen skeleton)、Markdown(中文正文)。走读树 Linux 6.6.78。

## 范围与边界

- 本计划只覆盖 tasks.md「Chapter 7: Helpers and Map Internals」8 条;第 8 章另立计划。
- delta spec 无需变更,不修改 `openspec/changes/generate-ebpf-book/specs/`。
- 不触碰第 1–6、9、17 章正文与 examples。
- 只修改:`book/chapters/07-helpers-and-map-internals.md`(新建)、`book/examples/chapter-07/**`(新建)、`.gitignore`(追加三个示例可执行,逐任务)、`openspec/changes/generate-ebpf-book/tasks.md`(末任务勾选)。

## 全局约束

1. **语言**:正文与脚本注释全中文;术语 map=映射、helper=辅助函数;首现「英文(中文解释)」。
2. **走读树口径(硬性)**:锚定 `/work/linux/linux-6.6.78`,行号标「(v6.6.78)」,机检按符号;运行内核(7.0.0-38)差异保守措辞。
3. **验证策略(硬性)**:已验证=机检实跑、切片器实跑、三示例编译级全链构建(clang .bpf.o + bpftool gen skeleton + gcc 链接)、只读 sysctl/CONFIG/BTF;预期=加载/attach/map 读数/事件输出(需 root,`unprivileged_bpf_disabled=2`)。零「已验证通过」式运行级表述,预期一律「预期输出示例 / 运行级待验证」。
4. **篇幅**:35–50k;预算:目标+导读 2–2.5k、一 8–9k、二 9–11k、三 5–6.5k、四 6–7.5k、可视化 1.5–2k、排错 2.5–3.5k、课后 1–1.5k、总结+参考 1.5–2k;分节服从章级。
5. **叙事**:双视角落「一、二、三、四」四节;各段主轴末「**独立示例**」链接。
6. **脚本注释**:bash 中文注释四类要点;头部「只读源码树,不构建、不加载」;运行型示例 Makefile/README 头部声明「编译级已验证,加载运行为预期(需 root)」+ XDP 安全注(只挂 lo)。
7. **任务对应**:每任务引用 tasks.md 矩阵片段;Task 13 统一勾选;`.comet/**` 不入库;每任务一提交。
8. **示例构建**:沿用 ch09 libbpf-bootstrap Makefile 模式(vmlinux.h ← bpftool btf dump;skeleton ← bpftool gen);构建产物已入 .gitignore(vmlinux.h/**/*.skel.h/**/*.o 全局规则在),**可执行需逐任务追加 .gitignore**。

## 现状基线(编写计划时已在 6.6.78 树/本机实证)

| 事实 | 值 |
|---|---|
| HEAD | `c0b14e7`(ch07 设计文档;工作区仅 .comet 变更) |
| helper 总数 | uapi `___BPF_FUNC_MAPPER` 214 个 FN |
| helper 分发锚点 | `__bpf_call_base` core.c:1500(noinline);`__bpf_call_base_args` :2245(解释器调用点 :1976);`bpf_base_func_proto` helpers.c:1757;`bpf_map_lookup_elem_proto` helpers.c:44;解析链 verifier.c:9965-9966(:19377 复用);`syscall_prog_func_proto` syscall.c:5730(内 :5665 回调 base);`kprobe_prog_func_proto` bpf_trace.c:1568、`tracing_prog_func_proto` :1948;`struct bpf_func_proto` include/linux/bpf.h:797 |
| 映射锚点 | `bpf_map_types[]` syscall.c:63;`map_update_elem` :1509;`struct bpf_map_ops` bpf.h:75;hashtab.c 2630 行:`htab_is_prealloc` :131、`htab_lock_bucket` :149、ma/pcpu_ma :88-89、prealloc 判定 :422、`__htab_map_lookup_elem` :677、`htab_elem_free`(bpf_mem_cache_free) :892、`htab_map_update_elem` :1098、`htab_map_delete_elem` :1424、`htab_lru_map_ops` :2301;bpf_lru_list.c `bpf_lru_pop_free` :499 |
| per-CPU/记账 | `percpu_array_map_ops` arraymap.c:775;`perf_event_array_map_ops` :1245;memcg 记账 syscall.c:`bpf_map_save_memcg` :389/`bpf_map_release_memcg` :400/`bpf_map_get_memcg` :406、`bpf_map_area_alloc` :312(bpf_memcg_flags);JIT 记账 `bpf_jit_uncharge_modmem` core.c:1020(呼应 ch06 bpf_jit_limit) |
| 事件输出锚点 | perf:`bpf_perf_event_output` bpf_trace.c:660(proto :698)、`__bpf_perf_event_output` :618、tp 变体 :1627;ringbuf.c 809 行:`struct bpf_ringbuf` :26、`__bpf_ringbuf_reserve` :408、`bpf_ringbuf_commit` :489、`BPF_CALL_4(bpf_ringbuf_output` :543(proto :562)、submit/discard proto :523/:536、`bpf_ringbuf_notify` irq_work :150、`ringbuf_map_mmap_kern` :261、prod/cons pos 读数 :301-305 |
| UAPI | BPF_MAP_TYPE_PERCPU_ARRAY :930、LRU_HASH :933、RINGBUF :958 |
| 本机工具链 | gcc 13.3.0、libbpf 1.3.0(头+/usr/lib/x86_64-linux-gnu/libbpf.so 与 .a)、bpftool 7.7.0、clang 18.1.3、BTF /sys/kernel/btf/vmlinux 在场;无 root、unprivileged_bpf_disabled=2、perf_event_paranoid=4 |
| .gitignore | vmlinux.h/**/*.skel.h/**/*.o 全局规则在;可执行逐文件登记(ch09/ch17 先例) |
| 可复用契约 | ch05/ch06 的 C1(citations)/C2(机检)/C3(切片器)/C4(练习)/C5(章级 README)/C7(标题)/C8(交叉链接)+ ch09 的 libbpf-bootstrap Makefile 模式 |

## 共享接口契约(跨任务定义)

### C1. citations.txt(chapter-07 树根,Task 2 产出,Task 3 消费)

行格式 `<文件>|<符号>|<中文说明>`,`#` 注释;机检=文件存在+`grep -F -q`。约 40 行五组:①helper 分发(core.c:1500/:2245;helpers.c:1757/:44;verifier.c:9965;bpf.h:797)②白名单(syscall.c:5730;bpf_trace.c:1568/:1948)③hash 内部(hashtab.c :131/:149/:88/:422/:677/:892/:1098/:1424/:2301;bpf_lru_list.c:499)④per-CPU/LRU/记账(arraymap.c :775/:1245;syscall.c :389/:400/:312;core.c:1020)⑤事件输出与 UAPI(bpf_trace.c :618/:660/:698;ringbuf.c :26/:150/:261/:408/:489/:543;uapi :930/:933/:958;syscall.c :63;map_update_elem :1509)。Task 2 逐行实测修正留痕。

### C2. verify-citations.sh(scripts/,Task 3)

同 ch06 C2 逐字(数据源 `../citations.txt`,汇总行 `TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78`;FAIL≠0 exit 1;头部「只读源码树」+失败三类;中文注释四类要点)。

### C3. 切片器接口(04-helper-dispatch/trace-helpers.sh 五站、05-map-internals/trace-map-internals.sh 六站,Task 4/5)

同 ch05/ch06 C3:`bash trace-*.sh [WALK_TREE]`,只读树;每站=「## 站名」+grep -n 定位(标 v6.6.78)+sed 区间(≤25 行/段)+一行中文职责注;定位失败 `STATION n: NOT FOUND`+exit 1;汇总 `TREE_VERSION=v6.6.78 STATIONS=<n>`。helper 五站:①__bpf_call_base(core.c:1500+args :2245)②bpf_base_func_proto(helpers.c:1757 区间)③解析链(verifier.c:9965 区间)④白名单实例(tracing_prog_func_proto bpf_trace.c:1948 头部)⑤bpf_func_proto 结构(bpf.h:797 区间)。maps 六站:bpf_map_types[](syscall.c:63 区间)/bpf_map_ops(bpf.h:75 区间)/htab_lock_bucket(:149 区间)/htab_map_update_elem(:1098 头部)/htab_map_delete_elem+RCU(:1424 区间)/bpf_map_save_memcg(:389 区间)。

### C4. 练习接口(exercises/,Task 13)

- `01-map-op-chain/`:README(防剧透:任务=产出「bpf(2)→htab_map_update_elem」调用链表;提示=从 syscall.c map_update_elem :1509 与 map->ops 入手;参考思路在后)+`answer.md`(链表 ≥6 站,含文件:行号)+`check-chain.sh`(对走读树 grep -F 断言链上 ≥4 锚点;WALK_TREE 适用;汇总同 C2)。
- `02-extend-ringbuf/`:README(防剧透同构)+`trace-ringbuf.sh`(两站:`__bpf_ringbuf_reserve` :408 区间/`bpf_ringbuf_commit`+notify :489 与 :150;STATIONS=2)+`check-ringbuf.sh`(bash -n+实跑断言两锚点与汇总)。

### C5. 章级 README 骨架(Task 1)

同 ch06 C5:七行目录表(含落地状态列)/学习路径/三档验证状态约定(本章「编译级已验证」细化到构建三步)/机检汇总占位「由 Task 12 回填」/走读树声明/安全声明(XDP 只挂 lo)。

### C6. 运行型示例构建契约(Task 2/6/7)

Makefile 沿用 ch09 模式:`vmlinux.h`(bpftool btf dump)→`.bpf.o`(clang -g -O2 -target bpf -D__TARGET_ARCH_x86)→`.skel.h`(bpftool gen skeleton)→可执行(gcc -lbpf -lelf -lz)。目标 `all/clean`;头部注释四类要点+「编译级已验证;加载运行为预期(需 root)」;XDP 示例 README 安全注(只挂 lo,勿挂物理网卡)。程序规格:
- 01 `hash_count`:XDP on lo,`BPF_MAP_TYPE_HASH` key=__be16(以太类型) value=__u64;BPF 侧 lookup+update(NOEXIST→EXIST);用户态周期读 3 键并打印。文件:`xdp_hash.bpf.c`+`loader.c`+`Makefile`+`README.md`。
- 02 `pcpu_count`:XDP on lo,`BPF_MAP_TYPE_PERCPU_ARRAY` value=__u64[ncpu];BPF 侧递增本 CPU 槽;用户态逐 CPU 读出+聚合打印。文件:`xdp_percpu.bpf.c`+`loader.c`+`Makefile`+`README.md`。
- 03 `event_ringbuf`+`event_perf`:tp_btf/exec 或 kprobe 触发;ringbuf 变体 `bpf_ringbuf_reserve`+`submit`(演示 reserve/commit)用户态 `ring_buffer__poll`;perf 变体 `bpf_perf_event_output`+`BPF_MAP_TYPE_PERF_EVENT_ARRAY` 用户态 `perf_buffer__poll`。文件:`ringbuf.bpf.c`+`ringbuf.c`+`perf.bpf.c`+`perf.c`+`Makefile`+`README.md`(对照表)。
- 硬性:每示例 `make` 全链零告警通过=编译级已验证;运行命令与预期输出标注「运行级待验证」。

### C7. 章节标题固定

`# 第 7 章 辅助函数与映射的内部实现`;十一小节:学习目标/本章导读/一、辅助函数:从 call 指令到白名单/二、hash 映射内部:并发与内存/三、per-CPU、LRU 与内存记账/四、事件输出:perf buffer 与 ring buffer/可视化:六类映射与两条事件通道/常见问题与排错/课后扩展任务/总结/本章参考来源。

### C8. 交叉链接

章首示例目录链接、各段主轴末「独立示例」、附录 B/C ≥1;`grep -c "](../examples/chapter-07/" ≥ 8`、`grep -c "](../appendix/" ≥ 1`。

## Tasks

### Task 1: 00-env 自检 + 章级 README 骨架

- [ ] Task 1: 00-env 自检 + 章级 README 骨架

**tasks.md 条目**:矩阵④(独立示例基座)+ C5。

**Files:** Create `book/examples/chapter-07/00-env/check-env.sh`(可执行)、`book/examples/chapter-07/README.md`

- [ ] **Step 1: check-env.sh**——①走读树版本+本章走读文件在场(helpers/hashtab/arraymap/ringbuf/bpf_trace/bpf_lru_list)②工具链五件:clang、gcc、libbpf(头+so)、bpftool(btf dump 探针:对 /sys/kernel/btf/vmlinux 前 1 行)、llvm-objdump ③权限档:unprivileged_bpf_disabled、perf_event_paranoid、id -u ④BTF 在场 ⑤CONFIG 三项(CONFIG_BPF_SYSCALL 等);三档结论(编译级可跑/加载级需 root/预期档声明)。只读;无树 exit 2。
- [ ] **Step 2: 实跑(硬性)**——bash -n+实跑 exit 0,输出与基线一致。
- [ ] **Step 3: 章级 README 骨架**(C5)。
- [ ] **Step 4: 提交** `feat(ch07): 00-env 环境自检与章级示例 README 骨架`

### Task 2: 01-hash-lookup-update 示例 + citations.txt

- [ ] Task 2: 01-hash-lookup-update 示例 + citations.txt

**tasks.md 条目**:矩阵③(hash lookup/update)+④;C1/C6/①。

**Files:** Create `book/examples/chapter-07/01-hash-lookup-update/{xdp_hash.bpf.c,loader.c,Makefile,README.md}`;Create `book/examples/chapter-07/citations.txt`;Modify `.gitignore`(追加 `book/examples/chapter-07/01-hash-lookup-update/hash_count`)

- [ ] **Step 1: 四件套**(C6 规格①;注释中文,头部声明)。
- [ ] **Step 2: 编译级实跑(硬性)**——make 全链零告警产 hash_count;贴全程输出进报告。
- [ ] **Step 3: citations.txt 逐行实测**(C1 五组);`grep -vc "^#\|^$"` 计数。
- [ ] **Step 4: 提交** `feat(ch07): hash 查找更新示例(编译级全链)与引用锚点清单`

### Task 3: verify-citations.sh 符号机检

- [ ] Task 3: verify-citations.sh 符号机检

**Files:** Create `book/examples/chapter-07/scripts/verify-citations.sh`(可执行)

- [ ] **Step 1: 写脚本**(C2 逐字)。**Step 2: 首跑(硬性)**全 PASS exit 0;`WALK_TREE=/nonexistent` exit 1。**Step 3: 提交** `feat(ch07): 符号机检脚本(引用锚点全绿)`

### Task 4: 04-helper-dispatch 切片器五站

- [ ] Task 4: 04-helper-dispatch 切片器五站

**tasks.md 条目**:矩阵①②(helper dispatch/prototype)资产面。

**Files:** Create `book/examples/chapter-07/04-helper-dispatch/{trace-helpers.sh,README.md}`(脚本可执行)

- [ ] **Step 1: 写脚本**(C3 五站;基线行号自查校准)。**Step 2: 实跑(硬性)**`STATIONS=5` exit 0 无 NOT FOUND;负路径 exit 1。**Step 3: README**(用途/调用/实跑摘录首尾 10 行/失败模式)。**Step 4: 提交** `feat(ch07): helper 分发切片器(五站实跑通过)`

### Task 5: 05-map-internals 切片器六站

- [ ] Task 5: 05-map-internals 切片器六站

**Files:** Create `book/examples/chapter-07/05-map-internals/{trace-map-internals.sh,README.md}`(可执行)

- [ ] **Step 1–4 同 Task 4 口径**(C3 六站);提交 `feat(ch07): 映射内部切片器(六站实跑通过)`

### Task 6: 02-percpu-counter 示例

- [ ] Task 6: 02-percpu-counter 示例

**tasks.md 条目**:矩阵③(per-CPU counters)。

**Files:** Create `book/examples/chapter-07/02-percpu-counter/{xdp_percpu.bpf.c,loader.c,Makefile,README.md}`;Modify `.gitignore`(追加 pcpu_count)

- [ ] **Step 1: 四件套**(C6 规格②)。**Step 2: 编译级实跑(硬性)**make 全链。**Step 3: 提交** `feat(ch07): per-CPU 计数器示例(编译级全链)`

### Task 7: 03-event-output 示例(ringbuf+perf 双变体)

- [ ] Task 7: 03-event-output 示例(ringbuf+perf 双变体)

**tasks.md 条目**:矩阵③⑤(event output;ring vs perf 实证面)。

**Files:** Create `book/examples/chapter-07/03-event-output/{ringbuf.bpf.c,ringbuf.c,perf.bpf.c,perf.c,Makefile,README.md}`;Modify `.gitignore`(追加 event_ringbuf、event_perf)

- [ ] **Step 1: 六件套**(C6 规格③;README 含两通道对照表)。**Step 2: 编译级实跑(硬性)**make 产两个可执行。**Step 3: 提交** `feat(ch07): 事件输出双变体示例(编译级全链)`

### Task 8: 正文——骨架+目标导读+一节

- [ ] Task 8: 正文——骨架+目标导读+一节

**Files:** Create `book/chapters/07-helpers-and-map-internals.md`

- [ ] **Step 1: C7 骨架(HTML 注释占位)**。**Step 2: 目标+导读(2–2.5k)**——目标 4–5 条;导读回收 ch04 白名单/ch06 站五固定地址/ch05 三节文件地图分工预告;章首示例目录链接。**Step 3: 一节(8–9k)**——call 指令旅程(__bpf_call_base :1500/args :2245)、JIT 固定地址之谜回收(ch06 站五)、解析链(verifier.c:9965→类型 ops→base)、214 白名单与 proto 结构、教师视角/学生动手(切片器 3 命令)/检查点 3 问;节末链接 04-helper-dispatch。**Step 4: 篇幅自查**。**Step 5: 提交** `docs(ch07): 目标导读与一节(helper 分发与白名单)`

### Task 9: 正文——「二、hash 映射内部」

- [ ] Task 9: 正文——「二、hash 映射内部」

**Files:** Modify 同文件

- [ ] **Step 1: 写二节(9–11k)**——htab 布局、查找无锁 RCU(:677)、更新三语义+桶锁(:1098/:149 per-CPU raw spinlock 动机)、prealloc 对比(:422)、bpf_mem_alloc 与 RCU 回收(:892)、删除(:1424);与切片器六站对应;示例 01 链接;双视角+检查点。**Step 2: 篇幅自查+行号实测**。**Step 3: 提交** `docs(ch07): 二节(hash 并发与内存)`

### Task 10: 正文——「三、per-CPU、LRU 与内存记账」

- [ ] Task 10: 正文——「三、per-CPU、LRU 与内存记账」

**Files:** Modify 同文件

- [ ] **Step 1: 写三节(5–6.5k)**——per-CPU 语义与聚合(percpu_array_map_ops :775;示例 02)、LRU 三链(bpf_lru_list :499;htab_lru_map_ops :2301)、memcg 记账(:389/:400;版本演进注)、bpf_jit_limit 呼应(ch06);生命周期三段收口;示例 02 链接;双视角+检查点。**Step 2–3 自查+提交** `docs(ch07): 三节(per-CPU、LRU 与记账)`

### Task 11: 正文——「四、事件输出」+可视化

- [ ] Task 11: 正文——「四、事件输出」+可视化

**Files:** Modify 同文件

- [ ] **Step 1: 写四节(6–7.5k)**——perf 路径(bpf_trace.c :660/:618/:698)、ringbuf 路径(ringbuf.c :26/:408/:489/:543/:150/:261)、五点对比、示例 03 跑法与预期输出标注、选型结论;双视角+检查点。**Step 2: 可视化两图(1.5–2k)**——六类映射对比图(矩阵⑥)/两条事件通道数据流图,各 ≥150 字解读。**Step 3: 自查+提交** `docs(ch07): 四节(事件输出对照)与可视化`

### Task 12: 排错/课后/总结/参考 + 交叉链接终检 + README 回填

- [ ] Task 12: 排错/课后/总结/参考 + 交叉链接终检 + README 回填

**Files:** Modify 正文;Modify `book/examples/chapter-07/README.md`

- [ ] **Step 1: 排错(2.5–3.5k)**四类——运行类/并发类/走读类(回指 ch05 机检三类)/编译类(libbpf/skeleton)。**Step 2: 课后+总结+参考**(2 练习描述链接 exercises/;总结 5 点;参考含 Documentation/bpf/maps.rst、ringbuf.rst——文件名 ls 实证)。**Step 3: C8 终检(≥8/≥1)+ 机检终跑 + README 回填(机检实测/落地状态)+ 篇幅终检(35000–50000)+ 保守措辞 grep(硬性)**。**Step 4: 提交** `docs(ch07): 排错课后总结参考收尾与终检回填`

### Task 13: 两项练习 + 主审(API/类型名专项)+ tasks.md 勾选(收口)

- [ ] Task 13: 两项练习 + 主审(API/类型名专项)+ tasks.md 勾选(收口)

**Files:** Create `exercises/01-map-op-chain/{README.md,answer.md,check-chain.sh}`、`exercises/02-extend-ringbuf/{README.md,trace-ringbuf.sh,check-ringbuf.sh}`(脚本可执行);Modify tasks.md(勾选 8 条)

- [ ] **Step 1–2: 练习①落地+实跑(硬性)**(C4;正路径全 PASS,断言 ≥4 锚点)。**Step 3–4: 练习②落地+实跑(硬性)**(STATIONS=2;check 全绿)。**Step 5: 主审**——矩阵⑧专项(API 名/helper 名/BPF_MAP_TYPE_* 对照 uapi 源头)+行号抽检 ≥15 处+验证双清单(编译级/预期)+术语;问题回修。**Step 6: 提交练习** `feat(ch07): 两项练习参考实现(机检全绿)`;**Step 7: 勾选提交** `chore(ch07): 勾选 Chapter 7 矩阵任务`

---

## 自审记录

1. **Spec 覆盖**:矩阵 8 条 → ①T8–T10+T4/T5;②T4/T5/T9/T10;③T2/T6/T7;④T1–T7;⑤T7+T11;⑥T11 可视化;⑦T12;⑧T13。无缺口。
2. **占位符扫描**:全部锚点基线实测;示例规格给定;无 TBD。
3. **类型一致性**:C1 位置(树根)与 C2 数据源一致;C3 与 T4/T5/C4 练习②一致;C6 与 T2/T6/T7 一致;汇总行口径 C2/T12 一致。

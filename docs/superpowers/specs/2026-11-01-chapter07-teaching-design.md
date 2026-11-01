# 第 7 章完整教学章节技术设计(辅助函数与映射的内部实现,机制章 + 编译级实证 + 切片器走读)

日期:2026-11-01|前置:第 5 章(源码地图)、第 6 章(JIT)已收口|走读树:/work/linux/linux-6.6.78

## 1. 背景与目标

### 1.1 矩阵任务(openspec tasks.md「Chapter 7: Helpers and Map Internals」,8 条)

1. 完整章节正文:helper 分发、按程序类型的 helper 白名单、映射操作、并发、内存生命周期;
2. 剖析:map ops、helper 原型、RCU 保护、自旋锁、per-CPU 存储、内存记账;
3. 集成示例:hash 查找/更新、per-CPU 计数器、经 perf 或 ring buffer 的事件输出;
4. `book/examples/chapter-07/` 独立示例;
5. 对比 ring buffer 与 perf buffer 的实现与用例;
6. 文本可视化:对比 hash、array、LRU、per-CPU、perf event array、ring buffer 六类;
7. 排错、总结、参考;
8. 主审:API 名、helper 名、映射类型一致性专项。

### 1.2 前章预告回收清单(正文必须兑现)

| 埋线 | 位置 | 本章兑现 |
|---|---|---|
| 「helper 白名单跟着执行现场走」 | 第 4 章 1.2(类型三元组)与第 3 章失败画廊 | 一节 1.3:白名单的内核机制(get_func_proto 链) |
| 「调 helper 就是一次原生 call」 | 第 6 章站三/站五(reg2hex、get_func_addr) | 一节 1.4:固定地址之谜(__bpf_call_base 基址)回收 |
| 「映射在内核里的实现结构是第 5 章正题」 | 第 2 章 | 第 5 章已对文件;本章进文件内部:并发与内存策略(不重复铺地图,回指 ch05 三节) |
| 「八类映射」 | 第 2 章 4.1 | 二/三/四节与可视化:六类逐个进内部(hash/array/per-CPU/LRU/perf/ringbuf) |
| 「map=映射、helper=辅助函数」 | 全书术语 | 沿用;首现「英文(中文)」 |

### 1.3 定位与本章独有的验证口径

定位:机制章(第 6 章讲「程序怎么跑」,本章讲「程序依赖的两样东西怎么实现」——辅助函数与映射)。第 7 章首次引入**运行型示例**(BPF 程序+用户态加载器),验证口径分两档:

- **编译级已验证**(本机可复现,无 root):全部 `.bpf.o` 经 clang `-target bpf` 编译成功;全部用户态加载器经 gcc+libbpf 编译成功(bpftool gen skeleton 依赖 BTF,本机 /sys/kernel/btf/vmlinux 在场);切片器/机检/只读 sysctl/CONFIG 实跑。
- **运行级待验证**(预期输出示例):加载、attach、map 读数、事件输出——本机 `unprivileged_bpf_disabled=2` 且无 root,一律标注「预期输出示例 / 运行级待验证」,零运行级表述。

工具链实证:gcc 13.3.0、libbpf 1.3.0(头 + .so + .a 齐全)、bpftool 7.7.0、clang 18.1.3;示例 Makefile 沿用 ch09 的 libbpf-bootstrap 模式(bpftool btf dump → vmlinux.h → gen skeleton → gcc -lbpf -lelf -lz)。

## 2. 示例资产设计(方案:运行型三件 + 切片器两件 + 机检)

目录(book/examples/chapter-07/):

| 目录 | 类别 | 内容与要点 |
|---|---|---|
| `00-env/` | 环境自检 | check-env.sh:走读树/JIT 源文件(回指 ch06)→ 工具链(gcc/libbpf/bpftool/clang 版本探测)→ 权限档(unprivileged_bpf_disabled、CAP)→ BTF;无 root 可跑,三档结论 |
| `01-hash-lookup-update/` | 运行型示例① | XDP(lo)按以太类型计数到 BPF_MAP_TYPE_HASH:BPF 侧 `bpf_map_update_elem`(BPF_NOEXIST 首包建项→EXIST 更新)、用户态周期 `bpf_map_lookup_elem` 读出——「BPF 写、用户态读」的完整闭环。只挂 lo(安全注:勿挂物理网卡,回指 ch17 警告) |
| `02-percpu-counter/` | 运行型示例② | BPF_MAP_TYPE_PERCPU_ARRAY 每包计数:解释 per-CPU 语义(零锁计数的代价=读取聚合),用户态逐 CPU 读出求和 |
| `03-event-output/` | 运行型示例③ | 双变体对照:ringbuf(`bpf_ringbuf_reserve`+`commit` → 用户态 `ring_buffer__poll`)与 perf(`bpf_perf_event_output` → 用户态 `perf_buffer__poll`);一个 Makefile 出两个可执行,README 给矩阵⑤的选型对照 |
| `04-helper-dispatch/` | 切片器 | trace-helpers.sh 五站:①`__bpf_call_base`(core.c:1500)与 `__bpf_call_base_args`(:2245)②`bpf_base_func_proto`(helpers.c:1757)③proto 解析(verifier.c:9965)④白名单实例(tracing_prog_func_proto bpf_trace.c:1948)⑤`struct bpf_func_proto`(include/linux/bpf.h:797) |
| `05-map-internals/` | 切片器 | trace-map-internals.sh 六站:①`bpf_map_types[]`(syscall.c:63)②`bpf_map_ops`(bpf.h:75)③htab 桶锁(`htab_lock_bucket` hashtab.c:149)④更新路径(`htab_map_update_elem` :1098)⑤删除与 RCU(`htab_map_delete_elem` :1424、`htab_elem_free` :892)⑥memcg 记账(`bpf_map_save_memcg` :389) |
| `scripts/` | 机检 | verify-citations.sh(同 ch05/ch06 契约,数据源=章级树根 citations.txt) |
| `citations.txt` | 锚点清单 | 约束同前章:树根,格式「文件\|符号\|说明」,约 40 行五组(①helper 分发 ②白名单 ③hash 内部 ④per-CPU/LRU/记账 ⑤ringbuf/perf/UAPI) |
| `exercises/` | 练习 | 01-map-op-chain(调用链查证+机检)、02-extend-ringbuf(切片器扩展+check 脚本),防剧透结构 |

命名与编号沿用 ch05/ch06 惯例(00-env 起,运行型在前、切片器居后,scripts/exercises 收尾)。

## 3. 章节正文设计

### 3.1 结构(完整模板十一小节)

```text
# 第 7 章 辅助函数与映射的内部实现
## 学习目标
## 本章导读
## 一、辅助函数:从 call 指令到白名单        ← 矩阵①②(helper dispatch/allowlist)
## 二、hash 映射内部:并发与内存             ← 矩阵①②(spin lock/RCU/prealloc/bpf_mem_alloc)
## 三、per-CPU、LRU 与内存记账              ← 矩阵②(per-CPU storage/memory accounting)
## 四、事件输出:perf buffer 与 ring buffer  ← 矩阵③⑤(实现对比与选型)
## 可视化:六类映射与两条事件通道            ← 矩阵⑥
## 常见问题与排错                            ← 矩阵⑦
## 课后扩展任务
## 总结
## 本章参考来源
```

各段主轴末「**独立示例**」链接(一→04-helper-dispatch;二、三→05-map-internals;四→03-event-output;导读→00-env/01/02)。

### 3.2 各节要点

**一、辅助函数(8–9k)**:一条 `call` 指令的旅程——BPF 侧 `call imm`(imm=helper id);通用层 `__bpf_call_base`(core.c:1500,noinline 兜底)+ `__bpf_call_base_args`(:2245,带参版);JIT 侧回指 ch06 站五 `bpf_jit_get_func_addr`(:1210)——「固定地址」的谜底:214 个 helper 全在核心内核,`__bpf_call_base + imm` 相对寻址;解析链:验证器 `env->ops->get_func_proto`(verifier.c:9965)→ 各程序类型的 ops(实例:`syscall_prog_func_proto` syscall.c:5730、`tracing_prog_func_proto` bpf_trace.c:1948)→ 公共兜底 `bpf_base_func_proto`(helpers.c:1757);`struct bpf_func_proto`(bpf.h:797)五字段与参数类型检查(回指 ch03 参数校验);白名单随类型滚动的教学点(同 id 不同类型可见性不同);教师视角(为什么设计成 proto 查询表而非函数指针直调)/学生动手(切片器 3 命令)/检查点 3 问。

**二、hash 映射内部(9–11k)**:`htab` 布局(buckets 数组 + 元素 `htab_elem`,ma/pcpu_ma 双 `bpf_mem_alloc` :88-89);查找(`__htab_map_lookup_elem` :677,无锁 RCU 读——为什么可以无锁:读侧只认「已发布」的 next 指针);更新(`htab_map_update_elem` :1098:BPF_NOEXIST/EXIST/ANY 三语义、bucket 锁 `htab_lock_bucket` :149——**per-CPU raw spinlock + irqsave** 的设计动机(哈希到 CPU,防 cacheline 争用,RT 安全);prealloc vs `BPF_F_NO_PREALLOC`(:422)的内存换延迟;发布/替换的 RCU 语义(旧元素 `bpf_mem_cache_free` :898 走 memalloc 的 RCU 回收);删除(:1424)与生命周期。失败出口:更新冲突/内存耗尽。教师视角(为什么不是一把大锁)/学生动手/检查点 3 问。

**三、per-CPU、LRU 与记账(5–6.5k)**:per-CPU 语义(每 CPU 一份值,lookup 返回聚合视角、用户态逐 CPU 读——`percpu_array_map_ops` arraymap.c:775;示例 02 对应);LRU(`htab_lru_map_ops` hashtab.c:2301;`bpf_lru_list` 本地/外部/自由三链 bpf_lru_list.c:499 pop_free——per-CPU 本地链避免争用,与二节桶锁同思想);内存记账:**v6.6 已从 memlock rlimit 迁到 memcg**——`bpf_map_save_memcg`/`bpf_map_release_memcg`(syscall.c:389/:400,创建者 cgroup 担保全部映射内存)+ JIT 侧 `bpf_jit_charge/uncharge_modmem`(core.c,受 `bpf_jit_limit` 约束——回指 ch06 一节 1.3);三节收尾把「内存生命周期」矩阵点收齐(分配-记账-回收三段)。

**四、事件输出(6–7.5k)**:两条通道的实现——perf buffer:`bpf_perf_event_output`(bpf_trace.c:660/proto :698)→ `__bpf_perf_event_output`(:618)→ perf_event_array 映射(:1245 arraymap.c)逐 CPU 环、lost 计数、用户态 `perf_buffer__poll`;ring buffer:`struct bpf_ringbuf`(ringbuf.c:26,meta 页+双数据区)、`__bpf_ringbuf_reserve`(:408,prod/cons pos 与 busy 位)、commit/notify(irq_work :150)、mmap(:261)、用户态 `ring_buffer__poll`;对比五点(所有权模型/跨 CPU 排序/丢失语义/内存开销/轮询接口);示例 03 跑法(编译级)+预期输出标注(运行级待验证);选型结论一句。教师视角/学生动手/检查点。

**可视化(1.5–2k)**:图①六类映射对比表图(hash/array/LRU/per-CPU hash/perf event array/ring buffer:寻址方式/并发策略/内存策略/典型用途,矩阵⑥六类全覆盖);图②两条事件通道数据流图(BPF 侧→内核缓冲→用户态 poll,标注丢失语义差异);各配 ≥150 字解读。

**排错(2.5–3.5k)**:运行类(map 读回全 0=没触发/attach 失败/unprivileged 禁用)、并发类(偶发丢计数=per-CPU 聚合顺序理解错;hash 更新 -E2BIG=容量)、走读类(切片器锚点漂移/机检三类回指 ch05)、编译类(libbpf 版本差异/skeleton 过期)。

**课后(1–1.5k)+总结(5 点呼应目标)+参考(源码树 helpers.c/hashtab.c/arraymap.c/ringbuf.c/bpf_trace.c、Documentation/bpf/maps.rst 与 ringbuf.rst、uapi bpf.h、ch02 4.1/ch03/ch05 三节/ch06 站五回指)**。

### 3.3 篇幅预算(总 35–50k,wc -m)

目标+导读 2–2.5k|一 8–9k|二 9–11k|三 5–6.5k|四 6–7.5k|可视化 1.5–2k|排错 2.5–3.5k|课后 1–1.5k|总结+参考 1.5–2k。分节服从章级。

### 3.4 课后练习设计(2 项,防剧透+机检)

- 练习① `exercises/01-map-op-chain/`:给定 `bpf_map_update_elem` 系统调用路径(syscall.c `map_update_elem` → ops),产出「从 bpf(2) 到 htab_map_update_elem」调用链表;check 脚本对链上 ≥4 个锚点做 grep 断言。
- 练习② `exercises/02-extend-ringbuf/`:仿 trace-map-internals.sh 写 `trace-ringbuf.sh` 两站(reserve :408 区间/notify+commit),check 脚本断言两锚点与 STATIONS=2。

## 4. 验证策略

- 已验证(硬性):机检实跑全绿;三示例**编译级**全过(clang .bpf.o + bpftool gen skeleton + gcc 链接,含 Makefile `make -n`/真实构建);两切片器实跑全站;只读 sysctl/CONFIG/BTF 读取。所有「编译级已验证」结论均以实跑输出为证。
- 预期(运行级待验证,需 root):加载/attach、map 读数、事件输出、perf/ringbuf 行为——全部「预期输出示例」标注;本机 `unprivileged_bpf_disabled=2`、`perf_event_paranoid=4` 为限制注脚。
- 保守措辞:零「已验证通过」式运行级表述;每处运行结果旁标「预期输出示例」。
- 主审矩阵⑧专项:helper 名(214 表内)、API 名(bpf_map_*_elem 系)、映射类型名(BPF_MAP_TYPE_*)逐一对照 uapi bpf.h 源头。

## 5. 技术风险与边界

1. **示例无法本机运行**:运行为预期档;编译级真实验证兜底——示例代码本身必须零告警编译,这是质量门禁。
2. **vmlinux.h 巨大**:bpftool btf dump 产物约 2 万+ 行,Makefile 生成、.gitignore 排除(沿用 ch09 模式),不入库。
3. **XDP attach 教学风险**:示例只挂 lo;README 显著安全注(勿挂物理网卡,回指 ch17)。
4. **行号漂移**:全部锚点 v6.6.78 实测;机检按符号不按行号(同前章契约)。
5. **记账 API 版本敏感**:memcg 记账是 6.x 特有形态(5.11 前 memlock rlimit),正文标注「随内核版本演进,以所用内核源码为准」。
6. **不重复 ch05**:映射文件地图与生命周期 fd 链路回指不展开;本章篇幅聚焦 ops 内部。

## 6. tasks.md 回写清单

「Chapter 7: Helpers and Map Internals」8 条全 [x]:①T8–T11 正文(分发/白名单/ops/并发/生命周期)②T9–T10+切片器(map ops/RCU/锁/per-CPU/记账)③T2/T6/T7 示例三件④T1–T7 独立示例⑤T11 四节对比⑥T11 可视化⑦T12 排错/总结/参考⑧T13 主审专项。

## 7. 测试策略汇总

每任务硬性:脚本 `bash -n` + 实跑断言;示例 `make` 全链真实构建;切片器全站 NOT FOUND=0 + 负路径 exit 1;机检 FAIL=0;正文 wc -m 分节与章级双查;保守措辞 grep;T13 主审含 API/类型名一致性专项 + 行号抽检 ≥15 处。

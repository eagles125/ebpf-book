# 练习 2：LRU hash 记录每进程被调度次数并打印 top-N

第 2 章《eBPF 架构与核心概念》课后扩展任务 2（扩展维度：映射类型）：用 `BPF_MAP_TYPE_LRU_HASH` 记录每个进程被调度的次数，用户态定期遍历、排序并打印 top-N 榜单——体验映射容量与静默淘汰。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

以 01-trace-stat 的构建体系为模板，从零实现一个「每进程调度次数 top-N」观测器：

1. 内核态：`SEC("tracepoint/sched/sched_switch")` 程序里，以被调度切入的进程 pid 为 key，在一张 `BPF_MAP_TYPE_LRU_HASH` 映射里累计其被调度次数；容量自定（参考实现用 4096），并过滤不应入榜的 pid。
2. 用户态：loader 每 2 秒遍历整张映射，按计数降序排序，打印前 N 名（N 由命令行参数指定，缺省 10）。
3. 保持与基线相同的生命周期：open_and_load → attach（SEC 推断）→ Ctrl-C 退出即卸载。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] top-N 榜单随系统负载变化：计数随调度持续增长；在另一终端跑编译等负载后，对应 pid 的名次/计数上升。
- [ ] 能解释「旧 pid 从榜单消失」的两种可能：该进程退出（表项随之消亡），或 LRU 满表静默淘汰最久未用项（3.3 的口径）——并知道两种情况如何区分。

## 提示

只给「会用到什么」，不给写法：

- 映射类型涉及 `BPF_MAP_TYPE_LRU_HASH`（3.3）：对照 3.1 的普通 hash 想一想——满表时两者对「新插入」的处理有什么决定性差异？这正是本练习要体验的容量语义。
- key/value 的选取：key 是「被调度切入的进程 pid」（`ctx->next_pid`），value 是计数；想想为什么 pid 0 不能入榜。
- 用户态遍历用 `bpf_map_get_next_key` 逐 key 迭代（`<bpf/bpf.h>` 接口，非 skeleton 专用）；排序用标准库 `qsort`，比较函数自己写。
- LRU 静默淘汰发生在内核侧：遍历期间表项可能随时消失——想想「读到什么算什么」的弱遍历语义该如何实现（每轮快照，不做增量）。
- 榜单是 top-N 而非全量：收集快照时留心上限，排序后截取前 N。
- 本练习 loader 侧用到 `bpf_map_lookup_elem` 等用户态映射接口：头文件 `#include <bpf/bpf.h>` 别漏（01-trace-stat 的冒烟结论）。

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`（`vmlinux.h` 由 BTF 生成）。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `bash ../../00-env/check-env.sh` 自检）。
- 运行需要 root 或 `CAP_BPF`（+ `CAP_PERFMON`/`CAP_SYS_ADMIN`）；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。
- 安全警告：tracepoint 程序只读观测、不修改内核行为；但加载未经验证的 eBPF 程序会占用内核资源并影响系统状态，只应在实验环境操作，勿在生产主机随意加载。

```bash
cd book/examples/chapter-02/exercises/02-lru-top-proc
make
sudo ./top_proc        # 每 2 秒打印 top-10，Ctrl-C 退出
sudo ./top_proc 20     # 打印 top-20（教学上限 64）
```

触发：无需专门负载——系统空闲时也有持续调度；想看榜单剧烈变化，可在另一终端执行 `while :; do :; done` 之类的负载再观察。

## 预期输出示例

（预期输出示例 / 运行级待验证）

```text
--- 每进程被调度次数 top-10 ---
910     5231
1823    3120
3456    1877
789     954
2210    645
```

- 左列为 pid、右列为被调度次数，按计数降序；每 2 秒刷新一轮，计数单调增长（除非表项被淘汰后重新入榜）。
- pid 0（idle/swapper）不应出现在榜单上——若出现，说明你的过滤分支写漏了。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`top_proc.bpf.c`/`top_proc.c`），建议自行实现后再对照：

1. **先查后插（`top_proc` 程序体）**：`bpf_map_lookup_elem` 命中则 `(*c)++` 直接返回；未命中才 `bpf_map_update_elem(..., BPF_NOEXIST)` 插入初值 1。`BPF_NOEXIST` 下存在并发竞争：另一 CPU 可能抢先插入同一 pid，本事件收到 `-EEXIST` 后**本次不计数**——对「被调度次数 top-N」这种教学观测是可接受的近似（每次竞争至多漏计一次），不值得为此在调度路径上加自旋。
2. **pid 0 过滤**：`next_pid == 0` 即 idle/swapper，空闲机器上它占比极高，不过滤则榜单永远被刷屏、反而看不到真实进程。
3. **LRU 容量语义（与 3.3/第 17 章阶段 3 同口径）**：`PROC_MAX 4096` 满表后，内核**静默淘汰最久未用表项**，插入仍成功——没有失败分支可感知，观测上表现为「旧进程从榜单消失」。这与普通 hash「满则插入失败」是决定性差异，也是本练习的核心体验点：要区分「旧 pid 消失」的两种可能——进程退出（表项被删除）与 LRU 淘汰（进程还在、只是被挤出），可用 `ps` 核对该 pid 是否仍存活。
4. **弱遍历（`snapshot`）**：`bpf_map_get_next_key` 从起点哨兵 `0` 开始逐 key 迭代（pid 0 永不入榜，天然可作哨兵）；每拿到一个 key 再 `bpf_map_lookup_elem` 取值。**表项可能在遍历期间被静默淘汰**——不追求一致性快照，读到什么算什么，淘汰只可能让个别表项漏进本轮榜单，2 秒后自然恢复。
5. **排序与打印**：快照收进 `struct entry` 数组（上限 `TOP_MAX 64`），`qsort` 按计数降序比较后截取前 N；N 的合法性检查（1–64）放在使用前，非法值回落缺省 10。
6. **观察实验设计**：把 `PROC_MAX` 调小（如 16，两侧无需同步——loader 不引用该宏）再运行，跑一批短命进程即可肉眼观察到「旧 pid 从榜单消失但进程仍存活」的淘汰行为；这是体验 LRU 语义最直接的实验。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带数条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，LRU hash 的容量与淘汰语义、`bpf_map_get_next_key` 遍历规则）。
- Linux kernel documentation：tracepoints（`Documentation/trace/tracepoints.rst`）。
- `qsort(3)`：标准库排序接口与比较函数约定。
- libbpf 文档：skeleton 加载模式与用户态映射接口（`<bpf/bpf.h>`）。
- 本章正文 3.1/3.3（hash 与 LRU hash card）、3.9 选型决策表与本目录 README 的容量语义讲解。
- 基线示例 `book/examples/chapter-02/01-trace-stat/`：本练习复用其构建体系与生命周期骨架。

# 01-type-selection：选型决策——三个场景各选程序类型与挂载点

《第 4 章 eBPF 程序类型与钩子点深入》「课后扩展任务」练习 1 的任务目录。正文
四节用三步法走通了「选对」的路径（5.2），本练习把它变成动手题：三个真实场景
各选一类程序类型与挂载点，先写选型论证、再动代码。`solution.bpf.c` 是按正确
选型合并为单个可编译对象的参考实现（剧透——先做后看）；本目录为
compile-only：`make` 只产出 `solution.bpf.o`，不生成 skeleton、不加载。

## 任务

三个场景，每个都要给出「程序类型 ＋ SEC/挂载点 ＋ 怎么挂」的选型，并写一段
选型论证：

- **场景A 容器内文件打开审计**：审计容器内发生的文件打开（只审计不拦截，
  恒放行）；
- **场景B TCP 连接建立观测**：观测本机 TCP 连接建立并计数（服务端视角，
  握手完成即可见）；
- **场景C 内核函数入口参数抓取**：长期抓取 `do_sys_openat2` 的入口参数
  （`dfd`/`filename`/`how`）。

选型论证须过两道工序（正文口径）：

1. **三元组三问**（5.2 三步法）：要什么数据 → 谁产生它 → 哪个类型的 ctx 给它
   （回 1.3 总表 ctx 列）；
2. **怎么挂**（2.3）：SEC 前缀对应的自动/显式挂载是哪条路线。

验收标准：

- 每场景一段选型论证，含「为什么是它」与「错误选项为何不行」两侧；
- 三场景的正确选型合并为单个可编译对象（参考实现即 `solution.bpf.c` 的形态：
  三个程序共享一张 ARRAY 映射），本目录 `make` 通过；
- 加载级预期（运行级待验证）：三程序各按其挂载方式挂上并计数——场景A 另有
  三前提（root＋`CONFIG_BPF_LSM=y`＋`lsm=bpf`，本机启动参数未启用）、场景B
  需 root＋cgroup v2（1.3 表稳定边界列）。

## 提示

- 每个场景按三元组三问逐项过：**要什么数据**（内核对象？状态机事件？函数
  参数？）、**谁产生它**（安全决策点？TCP 状态机？内核函数入口？）、
  **哪个类型的 ctx 给它**（1.3 表 ctx 列——`struct file *`、`bpf_sock_ops`、
  BTF 直参各是哪一类的形状）。
- 三场景各自的候选类型集合（不给答案——逐一回 1.3 表与画廊 card 检验）：
  - 场景A：追踪族（kprobe/fentry 挂打开路径符号）、tracepoint、LSM——
    「审计」语义最终落在哪一类？（1.4 决策树「安全」枝）
  - 场景B：XDP、TC、cgroup/skb、sockops、追踪族（挂 connect 路径符号，
    5.2 检查点第 3 问同源）——报文级与 socket 级的分野（3.12 背景问题）；
  - 场景C：kprobe、fentry、tracepoint（`sys_enter` 埋点）——临时调试与
    长期生产、宏解包与 BTF 直参各差在哪（3.3/3.4 与 1.5 第 2 问）。
- 别漏三元组第三项：**返回值语义跟着类型走**（1.2）——论证里要有「返回什么、
  什么意思」一行；sockops 的返回值是教学约定，保守口径见 1.2/3.12。
- 合并对象的映射选型想一想：三个程序共享计数、key 取值恒定——哪类映射够用
  （画廊各目录同款问题）？

## 环境前提与构建

- 依赖：clang（含 BPF 后端）、bpftool（`make` 自动由 `/sys/kernel/btf/vmlinux`
  生成 `vmlinux.h`）。环境自检与依赖版本见
  [`../../00-env/`](../../00-env/)（无 root 可跑，含本章特有的 tracefs 与
  LSM 前提检查项）。
- 构建（本目录内直接执行）：

```bash
make            # 产出 solution.bpf.o（三场景三程序合并对象，compile-only）
```

- `make` 只编译不加载（无挂载风险）。**别急着打开 `solution.bpf.c`**——它是
  分界线之后的参考实现；先自己完成选型论证与实现，卡住再对照。
- 加载级前提（届时需要）：root 或 CAP_BPF；场景A 另需 `CONFIG_BPF_LSM=y` 且
  启动参数含 `lsm=bpf`（本机配置 =y 已实证、启动参数未启用）；场景B 需
  cgroup v2；场景C 依赖 `CONFIG_DEBUG_INFO_BTF`（本机 BTF 在场）。

## 预期输出示例

（预期输出示例 / 运行级待验证）满足各场景加载前提、按各自挂载方式挂载后
（本目录不含 loader——挂载与读数参考画廊⑤⑦与锚点①的形态），读共享映射：

```text
$ sudo bpftool map dump name scenario_hits
key: 00 00 00 00  value: 7b 00 00 00 00 00 00 00   # 场景A：file_open 审计计数
key: 01 00 00 00  value: 05 00 00 00 00 00 00 00   # 场景B：passive established 计数
key: 02 00 00 00  value: 7b 00 00 00 00 00 00 00   # 场景C：do_sys_openat2 入口计数
Found 3 elements
```

三个计数随各自事件持续增长（跑几条 `ls`/`cat`、建立几条 TCP 连接），具体数值
随负载，以实测为准；满足不了某场景前提（如 `lsm=bpf` 未启用）时对应程序挂载
失败、对应 key 恒 0——属预期失败，非缺陷（画廊⑦同口径）。

---

## 参考实现思路

**选型论证表**（场景 → 参考选型 → 为什么 → 错误选项为何不行）：

| 场景 | 参考选型（SEC/挂载） | 为什么是它（1.4 决策树落点） | 错误选项为何不行 |
|---|---|---|---|
| A 文件打开审计 | LSM，`SEC("lsm/file_open")`（load 后随 attach 进入该 hook 裁决链，画廊⑦） | 「安全：对操作做裁决」叶——LSM 站在内核安全决策点上，hook 原型参数直列、`struct file *` 内核对象不经转接直接可读；返回 0=允许，正合「只审计不拦截」 | 用 kprobe/fentry 挂 `do_sys_openat2` 一类符号：能拿到参数，但那是系统调用入口、不是安全决策点——没有 LSM 的决策点语义与 audit 语义，也覆盖不了不经 openat2 的打开路径；hook 全局生效（画廊⑦口径），「容器内」的按容器划线还需在程序内按 cgroup/进程维度另行过滤（本练习最小实物只做审计计数） |
| B 连接建立观测 | sockops，`SEC("sockops")` 判 `BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB`（显式 attach 到 cgroup，画廊⑤） | 「socket 级状态或重定向」叶——连接建立是 TCP 状态机事件，sockops 由状态机驱动回调、服务端握手完成即可见，socket 级字段现成可读 | 用 XDP（或 TC/cgroup-skb）：报文级类型，看不见 socket 状态（3.12 背景问题）——「连接建立」不是报文事件；用 fentry/kprobe 挂 `tcp_v4_connect`：能数，但那是 5.2 检查点第 3 问的另一落点——数的是「本机发起 connect 调用」（客户端视角），与本场景「服务端握手完成」不是同一批事件；两个落点都过三步法，论证的关键正是把「任务要的到底是什么事件」问清（三问第一问） |
| C 入口参数抓取 | fentry，`SEC("fentry/do_sys_openat2")`（tracing link 自动挂载） | 「内核函数，长期跑、要类型安全」叶——BTF 类型化直参：`dfd`/`filename`/`how` 直接命名成形参，签名加载期按本机 BTF 校验、漂移即拒（2.4） | 用 kprobe：能抓（`BPF_KPROBE` 宏从 pt_regs 解包出形参），但非 BTF 类型化直参——原型只是宏解包约定、无加载期签名校验，符号内联/改名 attach 才报错（本机 `do_unlinkat` BTF 缺席实证）、签名静默漂移时读数失真——「长期抓取」不合本叶；tracepoint（`sys_enter_openat2`）是「固定埋点，跨版本稳定优先」叶的正当落点（锚点①镜 0 同款），任务若以稳定为先取它亦成理，参考实现按「入口参数抓取」考点取 fentry |

**代码走读**（`solution.bpf.c`）：

- **合并形态**：三个程序（`audit_open`/`so_conn`/`grab_params`）共存于一个
  对象、共享一张 ARRAY 映射 `scenario_hits`（key=场景编号 0/1/2）——映射
  选型 ARRAY：key 恒定 3 个、无扩缩需求（画廊各目录同款选型理由）。
- **场景A**：`BPF_PROG(audit_open, struct file *file)`——`<bpf/bpf_tracing.h>`
  的 `BPF_PROG` 宏按 hook 原型把参数直列成形参（画廊⑦同款）；恒 `return 0`
  =允许，失败分支 `if (!v) return 0` 与「只审计」职责一致（fail-open）。
- **场景B**：`ops->op != BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB` 先滤事件码，只
  数服务端握手完成；`return 1` =本回调已处理——sockops 返回值语义是画廊教学
  约定，内核实际消费面较窄（1.2/3.12 保守口径），纯事件类回调的返回值多被
  忽略，计数不受影响。
- **场景C**：`BPF_PROG(grab_params, int dfd, const char *filename,
  struct open_how *how)`——原型即函数原型、无 pt_regs 中转（3.4）；类型精确
  不豁免 probe 界限，真要读 `filename` 字符串内容仍走 probe 系辅助函数，本例
  只计数不读串。
- **怎么挂**：场景A load 后随 attach 自动进入裁决链（前提三件套缺一 load 即
  拒）、场景B 显式 attach 到 cgroup v2、场景C tracing link 自动挂载——2.3
  自动/显式之分的三种实物。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、
  libbpf 1.3）`make` 通过，产出 `solution.bpf.o`。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行
  加载与运行；「预期输出」为教学示例。三场景的加载前提见「环境前提与构建」
  （场景A 的 `lsm=bpf` 启动参数本机未启用——即便特权环境也需先补该前提）。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/prog_lsm.rst`（BPF LSM 语义
  与内核配置前提）、`include/uapi/linux/bpf.h`（`BPF_SOCK_OPS_*` 事件码与
  辅助函数原型注释）。
- 内核源码 `include/linux/lsm_hook_defs.h`：`file_open` hook 原型与裁决语义
  以所用内核此文件为准（画廊⑦同口径）。
- 正文对位：1.4 决策树与教师视角、1.2 三元组三问与 sockops 返回值保守口径、
  1.3 总表、2.3 SEC 前缀与自动/显式挂载、3.3/3.4（kprobe/fentry 选择边界）、
  3.12/3.14（sockops/LSM card）、5.2 三步法与检查点第 3 问。
- 画廊对照：[`../../03-type-gallery/05-sockops/`](../../03-type-gallery/05-sockops/)、
  [`../../03-type-gallery/07-lsm/`](../../03-type-gallery/07-lsm/)；锚点①
  （[`../../01-multi-observe/`](../../01-multi-observe/)）五镜对照表。

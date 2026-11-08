# 00-env：第 9 章环境自检（check-env.sh）

对应 ebpf.top BPF 入门系列第 3 篇《eBPF 开发环境搭建》。脚本按五段盘点本章 9 个示例的编译前提：
内核版本、BTF/CO-RE、工具链六件、运行时环境（tracefs 挂载与 root 档）、汇总。进入
01-hello-world 之前先跑一次自检，可以把「依赖缺失」和「代码写错」分开——前者看本页装包，
后者才需要回到对应示例的排错节。

- **验证状态：编译级（只读自检）**——脚本只读 `uname`、`/sys` 与命令可用性，不加载任何 BPF 程序，无 root 可跑；下节输出为本机实跑实录。
- **本机实况声明：文中数值（内核版本、文件存在性、WARN 项）为编写环境实况，其他内核/发行版数值不同。**
- **退出码约定：脚本尾部 `exit "$fail"`——0=无 FAIL，1=存在 FAIL**（与 ch02–04 自检脚本同一约定）；`scripts/verify-build.sh` 机检 00-env 时仍以输出的 `FAIL` 行 grep 兜底判定，不依赖退出码。

## 用法

在仓库根目录执行（或进入本目录后 `bash check-env.sh`）：

```bash
bash book/examples/chapter-09/00-env/check-env.sh
```

无参数、无 root 要求；任何一段出现 FAIL 都不会中断脚本，六项工具链会全部查完，便于一次看清缺什么。

## 本机实跑实录（2026-11-08）

```text
== 内核 ==
kernel: 7.0.0-42-generic
OK    内核版本 >= 6.x，满足全部示例要求（Netfilter 示例需 6.6+）
== BTF / CO-RE ==
OK    /sys/kernel/btf/vmlinux 存在（CONFIG_DEBUG_INFO_BTF 已启用）
== 工具链 ==
OK    clang（编译 BPF C）
OK    gcc（编译用户态 loader）
OK    bpftool（BTF dump / skeleton）
OK    make
OK    libbpf 头文件（libbpf-dev）
OK    Go（仅 09-netfilter-go 需要）
== 运行时环境 ==
WARN  tracefs 未挂载，可执行: sudo mount -t tracefs tracefs /sys/kernel/tracing
WARN  当前非 root：示例可编译，但运行 loader 需要 sudo 或 CAP_BPF/CAP_PERFMON 等能力
== 汇总 ==
环境满足第 9 章示例的编译要求。
```

对应版本实况：clang 18.1.3、bpftool 7.7.0、gcc 13.3.0、make 4.3、libbpf 1.3.0、Go 1.27.3（均为本机实况）。

## 输出逐段解读

### 第一段：内核

脚本取 `uname -r` 的主版本号分三档（不解析次版本）：

| 输出 | 含义 | 缺了怎么办 / 对本章的影响 |
|---|---|---|
| `OK 内核版本 >= 6.x` | 满足全部示例要求；09-netfilter-go 的 Netfilter 程序类型另需 6.6+（verify-build.sh 门禁按主+次版本精确判 6.6+，此处仅按主版本提示，6.0–6.5 内核上两者判定会分歧） | — |
| `OK 内核版本 5.x` | 示例 01–08 可用 | 09-netfilter-go 的加载会被 verifier 以 unknown program type 拒绝，只能走读其数据面代码 |
| `WARN 内核版本低于 5.10` | 多数示例可能不受支持 | 升级发行版内核（方法论级）；低版本缺 tracepoint/kprobe 特性时，对应示例编译或加载失败 |

这一行是 WARN 而不是 FAIL：内核版本无法用包管理器「补装」，脚本只提示、不拦截。

### 第二段：BTF / CO-RE

`/sys/kernel/btf/vmlinux` 存在且可读 → `OK（CONFIG_DEBUG_INFO_BTF 已启用）`；缺失 → `FAIL`，脚本提示参见附录 C。

- **FAIL 意味着什么**：本章示例 `make` 的第一步就是 `bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h`，BTF 缺失则 01–09 没有一个能编译。它与下一段的工具链项同属 FAIL 级；区别在于 BTF 取决于内核配置，不是装一个包就能补齐的。
- **缺了怎么办**：换用开启 `CONFIG_DEBUG_INFO_BTF=y` 的内核（主流发行版通用内核多已开启；自编内核的配置路径见[附录 C 各发行版内核编译 BTF 配置指南](../../../appendix/c-distro-btf-build-guide.md)——方法论级）。

### 第三段：工具链（六项）

| 检查项 | 缺失后果 | 影响的示例 | 补装（Ubuntu/Debian，方法论级） |
|---|---|---|---|
| clang | BPF 侧编译失败，`*.bpf.o` 出不来 | 01–09 全部 | `sudo apt install clang llvm` |
| gcc | loader 链接失败 | 01–08 | `sudo apt install gcc` |
| bpftool | vmlinux.h 生成与 skeleton 生成两步都依赖它，缺失时停在第一步 | 01–09 全部 | `sudo apt install bpftool`（或发行版 linux-tools 包） |
| make | 构建无法启动 | 01–09 全部 | `sudo apt install make` |
| libbpf 头文件（`/usr/include/bpf/bpf_helpers.h`） | loader 编译报找不到头 | 01–08 | `sudo apt install libbpf-dev` |
| Go | 控制面 `go build` 失败 | 仅 09-netfilter-go | `sudo apt install golang-go`（示例 README 要求 Go 1.22+） |

任一项 FAIL 都不终止脚本；末段汇总会给出与上表一致的 `apt install` 提示行。09-netfilter-go 的 `go.sum` 已随仓库提供，常规构建不强制联网；`go mod tidy` 仅在增删依赖或更换版本时需要，用于校验并补齐 indirect 条目，见其 README 的编译节。

### 第四段：运行时环境（两项 WARN）

**tracefs 未挂载（WARN）**：`/sys/kernel/tracing/events` 目录不存在。不影响编译，但影响两类运行级动作：一是 `bpf_printk` 类示例（01、02、03）的输出读取——`cat /sys/kernel/tracing/trace_pipe` 无从谈起；二是查看 tracepoint 事件格式（如 `cat /sys/kernel/tracing/events/syscalls/sys_enter_execve/format`）。脚本给出的解决命令 `sudo mount -t tracefs tracefs /sys/kernel/tracing` 需要 root——本机无 root，该命令在本机属预期档，未执行。

**当前非 root（WARN）**：示例可编译，但运行 loader 需要 sudo 或 `CAP_BPF`/`CAP_PERFMON`（tracing 类）等能力。本机实况比这更严一层：`kernel.unprivileged_bpf_disabled=2`，非特权加载被内核彻底禁用——不存在「不 root 降级运行」的路径。因此本章全部示例的运行输出在本机只能给「预期输出示例（运行级待验证）」，与章级 README 的验证状态说明一致。

### 第五段：汇总

`fail` 计数为 0 时输出「环境满足第 9 章示例的编译要求」——准确含义是**编译要求齐备**，不代表运行级就绪（第四段两项 WARN 仍然在场）；有 FAIL 项时输出 `apt install` 提示行。WARN 项不进入计数，也不改变退出码。

## 本机限制与示例可达档

本机实况：无 root、`kernel.unprivileged_bpf_disabled=2`、tracefs 未挂载、内核 7.0.0-42-generic。按上述自检结果，本章示例的可达档位：

- **编译级**：示例 01–08 的 `make` 全链（clang 编译 BPF → bpftool gen skeleton → gcc 链接 loader）无 root 即可完成，由 `scripts/verify-build.sh` 逐项机检；09-netfilter-go 的 BPF 数据面同理，Go 控制面依赖 `cilium/ebpf`（`go.sum` 已随仓库提供）。
- **运行级（一律预期输出示例 / 运行级待验证）**：01–08 的 loader 加载与事件读取、09 的 netns 拓扑实验，都需要 root 或对应能力集（tracing 类 `CAP_BPF`/`CAP_PERFMON`，09 另需 `CAP_NET_ADMIN`）；本机一项都不具备，各示例 README 中的「预期输出」均为教学说明，不是本机运行结果。
- **09-netfilter-go 内核门禁**：该示例需内核 6.6+——verify-build.sh 门禁按主+次版本精确判定；check-env.sh 内核段仅按主版本行内提示，6.0–6.5 内核上两者判定会分歧（以 verify-build.sh 为准）；内核不满足时机检记 SKIP，不记 FAIL。

## 与 verify-build.sh 的关系

`scripts/verify-build.sh` 对本章逐目录做编译级机检，00-env 的检查项即本脚本：运行 `bash 00-env/check-env.sh`（脚本以 `exit "$fail"` 退出）后，机检按输出的 `FAIL` 行 grep 兜底判定（见顶部退出码约定）。编译级机检只回答「能不能编出来」；「编出来之后能不能跑」按上一节的档位声明，不由机检回答。

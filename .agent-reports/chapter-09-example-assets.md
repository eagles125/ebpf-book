# 第 9 章独立示例资产生成报告

> 对应 OpenSpec change `generate-ebpf-book` §4.0（Chapter 9 Standalone Example Assets）。
> 本报告遵循 `.agent-reports/chapter-task-expansion.md` 的总体工作流要求：
> 列出变更文件、使用的参考来源、执行的验证和已知限制。

## 变更文件

新增（`book/examples/chapter-09/` 下，共 41 个文件）：

- `README.md`：目录总览、示例与 ebpf.top 系列文章对照表、统一构建模式、验证状态说明。
- `00-env/check-env.sh`：环境自检（内核/BTF/工具链/tracefs/权限）。
- `01-hello-world/`：`hello.bpf.c`、`hello.c`、`Makefile`、`README.md`。
- `02-pid-filter/`：`pid_filter.bpf.c`、`pid_filter.c`、`Makefile`、`README.md`。
- `03-container-security/`：`container_guard.bpf.c`、`container_guard.c`、`Makefile`、`README.md`。
- `04-ring-buffer/`：`ringbuf_exec.bpf.c`、`ringbuf_exec.c`、`ringbuf_exec_common.h`、`Makefile`、`README.md`。
- `05-open-file-kprobe/`：`opensnoop_kprobe.bpf.c`、`opensnoop_kprobe.c`、`opensnoop_kprobe_common.h`、`Makefile`、`README.md`。
- `06-open-file-perf-event/`：`opensnoop_perf.bpf.c`、`opensnoop_perf.c`、`opensnoop_perf_common.h`、`Makefile`、`README.md`。
- `07-open-file-return/`：`openret.bpf.c`、`openret.c`、`openret_common.h`、`Makefile`、`README.md`。
- `08-open-file-tracepoint/`：`opensnoop_tp.bpf.c`、`opensnoop_tp.c`、`opensnoop_tp_common.h`、`Makefile`、`README.md`。
- `09-netfilter-go/`：`bpf/nf_guard.bpf.c`、`cmd/nfctl/main.go`、`go.mod`、`Makefile`、`scripts/netns-lab.sh`、`README.md`。

修改：

- `.gitignore`：排除示例构建产物（`vmlinux.h`、`*.skel.h`、`*.o`、`nfctl`）。
- `book/chapters/09-observability-getting-started-ebpf-top.md`：
  顶部说明更新为“示例已落地”；“实验目录规划”改为“实验目录（已落地）”实际清单；
  第 3-12 节每节开头添加对应独立示例链接；验收清单勾选已落地项并新增运行验证条目；
  总结同步更新。
- `openspec/changes/generate-ebpf-book/tasks.md`：勾选 §4.0 全部 12 项与第 9 章小节的
  示例生成/交叉链接/验证标注 3 项（保留 Main-agent review 待本报告流程完成）。

## 参考来源

- ebpf.top BPF 入门系列 12 篇（章节骨架中已核对的文章标题与顺序）。
- libbpf-bootstrap 上游构建模式（vmlinux.h -> BPF 编译 -> skeleton -> loader）。
- libbpf 头文件：`bpf_helpers.h`、`bpf_tracing.h`（BPF_KPROBE/BPF_KRETPROBE 宏）。
- 内核 BTF（本机 `/sys/kernel/btf/vmlinux`，用于验证 `bpf_nf_ctx`、`BPF_HDR_START_NET`、
  `trace_event_raw_sys_enter/exit` 等类型与枚举确实存在于 vmlinux.h 生成物）。
- cilium/ebpf（Go 控制面：CollectionSpec、PinByName、link.AttachNetfilter、LoadPinnedMap）。

## 执行的验证

- **编译验证（已执行）**：示例 01-08 每个目录均执行完整 `make`
  （bpftool 生成 vmlinux.h -> clang 18 `-target bpf` 编译 -> bpftool gen skeleton ->
  gcc 13 链接 `-lbpf -lelf -lz`），全部通过；唯一警告来自 vmlinux.h 自身的
  BTF 生成物（已知现象，非示例代码问题）。
- **09 BPF 侧编译验证（已执行）**：`bpf/nf_guard.bpf.o` 编译通过。
- **环境**：x86_64、内核 7.0.0-30、bpftool 7.7.0、libbpf-dev 1.3.0。
- **脚本自检**：`00-env/check-env.sh` 在本机运行通过（工具链全 OK；提示非 root）。
- **README 命令抽查**：03 的简化 cgroup id 获取命令在本机验证可输出 inode 号。

## 未执行的验证（已知限制）

- **运行验证**：编写环境非 root，未加载/运行任何 BPF 程序。所有 README 中的
  “预期输出”均为教学说明，符合 `book/STYLE.md` 的验证标注约定。
- **09 Go 侧构建**：`go mod tidy` + `go build` 因会话工具暂时不可用未完成，
  `cilium/ebpf` 的 `link.AttachNetfilter` 字段名等 API 细节未经编译器核对，
  已在 09 README“常见失败点”中给出核对路径。
- **09 运行语义**：Netfilter BPF（6.6+）的 verifier 行为、netns 内 attach 语义、
  per-CPU stats 汇总未实测；标注待特权环境验证。
- 第 9 章正文的 ebpf.top 原文逐篇核对仍属骨架阶段任务（见 tasks.md 未来批次）。

## 主代理复核结论

- 章节与示例目录双向交叉引用已核对（`../examples/chapter-09/` 相对路径、
  目录清单与实际文件一致、每个示例 README 反向引用章节小节）。
- 发现并修正：示例与 ebpf.top 教程编号错位问题（Hello World 应为实践教程一，
  而非“教程四”；全部 9 组 README/.bpf.c/go 注释已统一为
  “实践教程 N（系列第 M 篇）”格式）。
- 验证标注符合 STYLE.md：编译验证/待运行验证区分明确，无“已验证通过”的越界声明。

# Chapter 17 Non-Snap Go Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使用可执行的非 Snap Go 工具链完成 Chapter 17 的 9 个编译目标验证，并留下可复现证据。

**Architecture:** 保持示例的 Makefile 和源码不变；通过 Make 变量 `GO` 将 Go 命令指向可启动的绝对路径。先识别或在项目局部安装 Go 1.27.1，再分别验证三个 Go 控制面目标，最后执行顶层汇总脚本。

**Tech Stack:** Go 1.27.1、clang 18、bpftool 7.7、libbpf 1.3、GNU Make、Bash。

## Global Constraints

- Go 二进制不得位于 `/snap/`，且 `go version` 必须返回 0。
- 工具链安装位置固定为 `book/.tools/go-1.27.1/`；不使用 root、不修改系统 Go。
- 不修改 `book/examples/chapter-17/**` 的源码或 Makefile；通过 `GO=/绝对路径/go` 覆盖变量。
- 仅执行编译级验证；不得加载 BPF 程序、创建网络命名空间或操作网卡。
- 只有 `verify-build.sh` 返回 0 且输出 `PASS=9 WARN=0 FAIL=0` 时，才更新验证文档。

---

### Task 1: 解析并提供可启动的非 Snap Go

**Files:**
- Create (only if no reusable binary exists): `book/.tools/go-1.27.1/`
- Test: command-line Go version probe

**Interfaces:**
- Produces: `GO_BIN`，一个非 `/snap/` 路径、可通过 `GO_BIN version` 启动的 Go 1.22+ 二进制。
- Consumes: `GO` Make 变量；所有受影响 Makefile 使用 `GO ?= go`。

- [x] **Step 1: 搜索现有非 Snap Go，并逐个验证**

```bash
for candidate in /usr/local/go/bin/go /usr/bin/go /opt/go/bin/go; do
    if [ -x "$candidate" ]; then
        "$candidate" version
    fi
done
```

Expected: 找到一个返回 0 且路径不含 `/snap/` 的候选；将其绝对路径赋给 `GO_BIN`。

- [x] **Step 2: 候选缺失时安装项目局部 Go 1.27.1**

```bash
mkdir -p book/.tools
curl --fail --location --retry 3 --output /tmp/go1.27.1.linux-amd64.tar.gz https://go.dev/dl/go1.27.1.linux-amd64.tar.gz
tar -C book/.tools -xzf /tmp/go1.27.1.linux-amd64.tar.gz
mv book/.tools/go book/.tools/go-1.27.1
book/.tools/go-1.27.1/bin/go version
```

Expected: `go version go1.27.1 linux/amd64`，并将 `GO_BIN` 设为 `/work/ai/ebpf-book/book/.tools/go-1.27.1/bin/go`。

- [x] **Step 3: 确认局部工具链不污染 Git 状态**

```bash
git check-ignore -q book/.tools/go-1.27.1/bin/go
git status --short
```

Expected: `git check-ignore` 返回 0；除已知 Comet 状态文件外不出现工具链文件。

- [x] **Step 4: Commit**

不提交下载的工具链；如为满足忽略规则而修改 `.gitignore`，仅提交该规则。

### Task 2: 复验 Go 控制面构建路径

**Files:**
- Test: `book/examples/chapter-17/05-firewall/`
- Test: `book/examples/chapter-17/exercises/02-rule-persistence/`
- Test: `book/examples/chapter-17/exercises/03-prometheus-export/`

**Interfaces:**
- Consumes: Task 1 的 `GO_BIN`。
- Produces: 三个目录的 `fwctl` 二进制，并证明 `GO` 覆盖变量被 Makefile 接受。

- [x] **Step 1: 验证阶段 05 Go 控制面**

Run `make clean all GO="$GO_BIN"` in `book/examples/chapter-17/05-firewall`.

Expected: 返回 0 并生成 `fwctl`。

- [x] **Step 2: 验证规则持久化练习**

Run `make clean all GO="$GO_BIN"` in `book/examples/chapter-17/exercises/02-rule-persistence`.

Expected: 返回 0 并生成 `fwctl`。

- [x] **Step 3: 验证 Prometheus 导出练习**

Run `make clean all GO="$GO_BIN"` in `book/examples/chapter-17/exercises/03-prometheus-export`.

Expected: 返回 0 并生成 `fwctl`。

- [x] **Step 4: Commit**

本任务不修改版本化源文件；记录命令输出到后续验证报告，不创建空提交。

### Task 3: 全量编译验证与证据同步

**Files:**
- Modify: `book/examples/chapter-17/README.md`（仅在 9/9 通过时更新最近一次验证记录）
- Modify: `book/chapters/17-high-performance-ebpf-firewall.md`（仅在验证日期或工具链信息需要同步时）
- Create: `docs/superpowers/reports/2026-09-26-chapter17-non-snap-go-verification.md`

**Interfaces:**
- Consumes: Task 1 的 `GO_BIN`，Task 2 的三个构建成功结果。
- Produces: `verify-build.sh` 的完整输出、退出码和运行级限制说明。

- [x] **Step 1: 运行全部 9 个构建目标**

Run `GO="$GO_BIN" bash book/examples/chapter-17/scripts/verify-build.sh`.

Expected: BTF 前置检查通过，汇总为 `PASS=9 WARN=0 FAIL=0`，进程返回 0。

- [x] **Step 2: 写入验证报告**

报告记录非 Snap `GO_BIN` 的绝对路径和 `go version`、完整汇总、命令退出码、clang/bpftool/libbpf 版本，以及运行级仍因普通用户和 `unprivileged_bpf_disabled=2` 待验证。

- [x] **Step 3: 仅在成功时同步读者文档**

将 README 的“最近一次全量验证”日期、Go 路径类型和 9/9 结果更新为本次实际命令证据；正文仅同步不会夸大运行级验证的事实。

- [x] **Step 4: 复核并提交文档证据**

Run `git diff --check` and `git status --short`, then commit only report and successful-verification documentation updates with message `docs(ch17): record non-Snap Go build verification`.

# Chapter 17 非 Snap Go 全量编译验证记录

日期：2026-10-09

## 状态

编译级验证通过。9 个构建目标均成功，验证脚本退出码为 `0`。

## 命令与结果

执行命令：

```bash
GO="/work/ai/ebpf-book/book/.tools/go-1.27.1/bin/go" \
  bash book/examples/chapter-17/scripts/verify-build.sh
```

完整输出：

```text
== BTF 前置 ==
OK    vmlinux BTF 可读
== 阶段编译 ==
PASS  01-xdp-drop
PASS  02-parse-acl
PASS  03-conntrack
PASS  04-nat-tc
PASS  05-firewall
== 练习编译 ==
PASS  exercises/01-ipv6-acl
PASS  exercises/02-rule-persistence
PASS  exercises/03-prometheus-export
PASS  exercises/04-port-rate-limit

== 汇总: PASS=9 WARN=0 FAIL=0 ==
```

退出码：`0`。

## 工具链证据

- Go（非 Snap）：`/work/ai/ebpf-book/book/.tools/go-1.27.1/bin/go`，`go version go1.27.1 linux/amd64`
- clang：`Ubuntu clang version 18.1.3 (1ubuntu1)`
- bpftool：`bpftool v7.7.0`（其版本输出显示 `using libbpf v1.7`）
- libbpf 开发包：`1.3.0`

## 验证范围与运行级风险

本记录仅证明 BTF 前置检查和 5 个阶段加 4 个练习的编译成功。编写环境为普通用户，且
`kernel.unprivileged_bpf_disabled=2`；未执行 BPF 程序加载、XDP/TC 挂载、NAT 或压测。
因此所有运行级验证仍待具备 root 或必要 BPF/网络能力的特权环境完成，文档中的运行输出仍仅为教学示例。

## 文档与提交

本次证据同步更新了以下文件：

- `book/examples/chapter-17/README.md`
- `book/chapters/17-high-performance-ebpf-firewall.md`
- `docs/superpowers/reports/2026-09-26-chapter17-non-snap-go-verification.md`

提交信息：`docs(ch17): record non-Snap Go build verification`。提交范围仅限上述 3 个文档文件。

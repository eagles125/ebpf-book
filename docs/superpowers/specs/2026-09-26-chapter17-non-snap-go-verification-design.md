# 第 17 章非 Snap Go 编译验证设计

## 目标

在不修改 eBPF 示例源码、不加载 BPF 程序、不改变网络拓扑的前提下，为第 17 章的 Go 控制面找到可在当前环境执行的非 Snap Go 工具链，并重新完成编译级验证。

## 背景

`/snap/bin/go` 在当前受限环境启动时被 `snap-confine` 拒绝（缺少 `cap_dac_override`）。阶段 05 与练习 02、03 因共享该命令而无法完成 Go 编译；阶段 01--04 和练习 01、04 已通过 C 语言构建。

## 方案

1. 探测工作区依赖运行时、系统路径和常见受控位置，选择可执行且非 Snap 的 `go` 二进制；先运行 `go version` 确认其可启动。
2. 不改 Makefile。阶段 05 及两个 Go 练习的 Makefile 已允许通过 `GO=/绝对路径/go` 覆盖默认命令。
3. 先分别执行三个受影响目标的 `make clean all`，确认每个 Go 控制面能够完成编译。
4. 通过 `GO=/绝对路径/go bash book/examples/chapter-17/scripts/verify-build.sh` 执行 9 个目标的全量复验。
5. 仅在全量命令实际返回 0 后，更新 Chapter 17 的验证记录；否则保留现有陈述并记录精确阻塞原因。

## 边界与失败处理

- 不下载、安装或升级 Go；若未找到能运行的非 Snap Go，则停止并请求用户批准该额外操作。
- 不把环境启动失败误判为源码编译失败。
- 无 root 且 `unprivileged_bpf_disabled=2` 的条件不变，因此本轮仍只验证编译级，不执行 BPF 加载、挂载或网络实验。

## 验收标准

- 所选 `go version` 成功运行，且路径不在 `/snap/` 下。
- 阶段 05、练习 02、练习 03 各自的 `make clean all` 返回 0。
- 全量 `verify-build.sh` 输出 `PASS=9 WARN=0 FAIL=0` 并返回 0。
- 若任一条件不满足，报告命令和真实输出，不更新“全量通过”记录。

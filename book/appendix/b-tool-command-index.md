# 附录 B 常用工具与命令索引

## 目标

本附录后续用于整理学习和生产排障中常用的 eBPF 工具命令。

## 计划内容

- `bpftool prog/map/link/btf/feature`。
- `clang -target bpf` 编译命令。
- `tc qdisc add dev <iface> clsact` 与 `tc filter add ... bpf`。
- `ip link set dev <iface> xdp obj ...`。
- `xdp-loader`。
- BCC 工具：`execsnoop`、`opensnoop`、`biolatency`、`biosnoop`、`tcpconnect`。
- bpftrace 一行命令模板。
- `perf` 与 FlameGraph 生成流程。

## 参考来源

- Linux kernel documentation。
- BCC project documentation。
- bpftrace documentation。
- xdp-tools documentation。

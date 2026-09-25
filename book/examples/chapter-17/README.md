# 第 17 章独立示例资产

本目录是《第 17 章 实战：构建高性能 eBPF 防火墙》的配套示例代码，
按渐进搭建式教学组织为 6 个阶段（00–05），每个阶段独立可编译、独立成篇。

## 目录结构

| 目录 | 阶段主题 | 数据面重点 | 学到什么 | 挂载点 | 输出机制 |
|---|---|---|---|---|---|
| `00-env/` | 环境自检 | - | 权限分级与工具链诊断 | - | - |
| `01-xdp-drop/` | 最小 XDP 丢弃 | `XDP_DROP`/`XDP_PASS` | XDP 挂载位置、最小闭环 | XDP（veth-fw-c） | per-CPU stats map |
| `02-parse-acl/` | 报文解析 + ACL | 边界检查解析 + hash 精确 + LPM CIDR | 五元组、两类 map 查找 | XDP | stats map |
| `03-conntrack/` | 连接跟踪 | 正反 key 状态机 + LRU | 有状态防火墙 | XDP | stats + conntrack dump |
| `04-nat-tc/` | TC NAT | SNAT/DNAT + 增量校验和 | XDP/TC 分工、改写顺序 | TC clsact ingress/egress | stats map |
| `05-firewall/` | 完整系统 | XDP 防火墙 + TC NAT | 生产级数据面/控制面分离 | XDP + TC | pin map + Go CLI |
| `scripts/` | 拓扑与验证 | - | 实验方法学 | - | - |
| `exercises/` | 课后扩展任务 | 4 项完整参考实现 | 见章末「课后扩展任务」 | - | - |

## 学习路径

1. `bash 00-env/check-env.sh` 自检（无 root 也能给出三档结论）。
2. 按 01 → 05 顺序推进；每个阶段目录内 `make` 后阅读其 README 的「编译与运行」。
3. 运行/压测需要 root + `scripts/setup-veth.sh` 拓扑；只有编译验证可在无特权机器完成。

## 安全警告

- XDP/TC 程序会直接丢包或改包：**只挂载到 `setup-veth.sh` 创建的 veth 接口，
  绝不要挂到物理网卡或 SSH 使用的接口**，否则可能立即失联。
- NAT 实验会改写报文内容；实验完毕执行 `sudo ./fwctl detach` 与 `setup-veth.sh teardown` 清理。

## 统一构建模式

阶段 01–04 与练习的 C 侧构建采用 libbpf-bootstrap 三步（各目录内直接 `make`）：

vmlinux.h（bpftool 由 /sys/kernel/btf/vmlinux 生成） -> *.bpf.o（clang -target bpf）
    -> *.skel.h（bpftool gen skeleton） -> 可执行 loader（gcc -lbpf -lelf -lz）

阶段 05 为 BPF + Go 联合构建（`make` 一次完成，见其 Makefile）。

## 验证状态说明

遵循 `book/STYLE.md` 约定：

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3、Go 1.27.1）
  通过 `scripts/verify-build.sh` 全量编译（结果见各 README 验证状态区）。
- **最近一次全量验证**：2026-09-25 运行 `bash scripts/verify-build.sh`
  （clang 18.1.3 / bpftool v7.7.0 / Go 1.27.1，逐阶段 `make clean all`）：
  **PASS=9（5 阶段 + 4 练习）、WARN=0、FAIL=0，退出码 0**；
  Go 依赖已在本地模块缓存，未出现网络拉取 WARN。
- **待运行验证**：加载/挂载/NAT/压测需要特权内核环境；编写环境为非 root
  （`unprivileged_bpf_disabled=2`），各 README 中「预期输出」均为教学示例。

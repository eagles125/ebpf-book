# 《eBPF 从入门到精通：原理、内核实现与实战可移植开发》

本仓库用于生成和维护一本中文 eBPF 自学书籍。全书目标是覆盖从基础概念、Linux 内核实现、可观测性实践，到网络性能优化、CO-RE 可移植开发、Go/Rust 互操作和生产化部署的完整学习路径。

## 目标读者

- 熟悉基本 Linux 命令、进程、文件系统和网络概念的工程师或学生。
- 具备 C 语言基础，希望理解 eBPF 程序如何被编译、加载、验证、挂载和执行。
- 希望将 eBPF 用于系统观测、网络优化、安全策略、可移植工具开发或 Kubernetes 平台工程的人。

## 前置知识与实验环境

建议准备一台 Linux 实验机或虚拟机：

- Linux kernel 5.10+；学习 CO-RE、ring buffer、现代 libbpf 工作流时建议使用更新的 LTS 或发行版内核。
- clang/LLVM、bpftool、libbpf、make、gcc、iproute2。
- 可选工具：BCC、bpftrace、perf、FlameGraph、Prometheus、Grafana、Cilium、Hubble、Pixie。
- 权限：许多实验需要 root，或需要 `CAP_BPF`、`CAP_PERFMON`、`CAP_NET_ADMIN`、`CAP_SYS_ADMIN` 等能力。
- BTF：CO-RE 章节需要内核启用 `CONFIG_DEBUG_INFO_BTF`，或提供匹配内核的 BTF 文件。

本书中的代码示例会标注环境要求、编译命令、运行方式和预期输出。若示例尚未在当前仓库环境实际运行，会明确说明“预期输出”是教学说明，不冒充本地验证结果。

## 全书目录

### 第一部分：基础与原理

1. [第 1 章 eBPF 概述与历史](chapters/01-ebpf-overview-and-history.md)
2. 第 2 章 eBPF 架构与核心概念
3. 第 3 章 验证器与安全保障
4. 第 4 章 eBPF 程序类型与钩子点深入

### 第二部分：Linux 内核实现细节

5. 第 5 章 eBPF 子系统内核代码结构
6. 第 6 章 JIT 编译与性能优化
7. 第 7 章 辅助函数与映射的内部实现
8. 第 8 章 BTF 与 CO-RE 原理

### 第三部分：可观测性与诊断实战

9. [第 9 章 eBPF 可观测性入门：基于 ebpf.top 入门系列](chapters/09-observability-getting-started-ebpf-top.md)
10. 第 10 章 BCC 工具集：系统观测的瑞士军刀
11. 第 11 章 bpftrace：高级单行诊断与脚本
12. 第 12 章 系统性能诊断：CPU、内存与 I/O
13. 第 13 章 网络性能观测与诊断
14. 第 14 章 可视化平台与监控集成

### 第四部分：网络性能优化

15. 第 15 章 XDP 深度实战
16. 第 16 章 TC 与 eBPF
17. [第 17 章 实战：构建高性能 eBPF 防火墙](chapters/17-high-performance-ebpf-firewall.md)
18. 第 18 章 Cilium eBPF 网络方案剖析
19. 第 19 章 网络加速高级主题

### 第五部分：跨内核可移植性与多语言互操作

20. 第 20 章 CO-RE：一次编译，到处运行
21. 第 21 章 libbpf 开发实战：编写可移植的 eBPF 程序
22. 第 22 章 eBPF 与 Go 语言的互操作性
23. 第 23 章 eBPF 与 Rust 语言的互操作性

### 第六部分：高级主题与生产实践

24. 第 24 章 eBPF 生产化部署与安全加固
25. 第 25 章 eBPF 前沿与展望

## 附录

- [附录 A eBPF 指令集速查表](appendix/a-ebpf-instruction-set.md)
- [附录 B 常用工具与命令索引](appendix/b-tool-command-index.md)
- [附录 C 各发行版内核编译 BTF 配置指南](appendix/c-distro-btf-build-guide.md)
- [附录 D 参考资源详细链接列表](appendix/d-reference-links.md)

## 推荐阅读路径

初学者建议按章节顺序阅读第 1 至第 4 章，再进入第 9 章做最小可运行实验。已经有 Linux 性能分析经验的读者可以先读第 9 至第 13 章，随后回到第 3、5、6、7 章补齐 verifier、JIT 和 map 内核实现。网络工程师可以重点阅读第 15 至第 18 章。希望开发可分发工具的读者应重点阅读第 8、20、21、22、23 章。

## 主要参考资源

本书优先参考以下资料体系：

- ebpf.io 官方门户与白皮书。
- Brendan Gregg 的性能分析文章和火焰图实践。
- Linux 内核 BPF 文档、BTF 文档和 UAPI 头文件。
- LWN.net 的 BPF 设计演进文章。
- bpf 邮件列表归档。
- BCC 项目和教程。
- bpftrace 项目和一行命令教程。
- Cilium、Hubble 和相关网络/可观测性文档。
- Andrii Nakryiko 关于 BTF、CO-RE、libbpf 的文章。
- libbpf 与 libbpf-bootstrap。
- ebpf.top 中文教程，特别是 BPF 入门系列。

## 分批生成计划

- 第一批：书稿 scaffold、写作规范、第 1 章完整样章、第 9/17 章深度骨架、附录 stub。
- 第二批：完成第 2 至第 4 章。
- 第三批：完成第 5 至第 8 章。
- 第四批：完成第 9 至第 14 章，并尽可能验证观测示例。
- 第五批：完成第 15 至第 19 章，重点补齐第 17 章防火墙代码和压测。
- 第六批：完成第 20 至第 23 章。
- 第七批：完成第 24 至第 25 章和附录。
- 第八批：全书一致性、引用、代码示例、术语和字数审计。

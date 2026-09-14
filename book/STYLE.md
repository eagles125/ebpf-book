# 书稿写作规范

## 语言与术语

- 全书使用中文书写。
- 专业术语首次出现时采用“英文（中文解释）”或“中文（English）”格式，例如：`验证器（verifier）`、`CO-RE（Compile Once - Run Everywhere，一次编译，到处运行）`。
- 同一概念保持译名一致：map 译为“映射”，helper 译为“辅助函数”，program type 译为“程序类型”，attachment point 译为“挂载点”。
- 避免空泛表达。优先解释具体机制、命令、数据结构、失败模式和排错路径。

## 完整章节模板

```markdown
# 第 N 章 标题

## 学习目标

## 本章导读

## 原理解析

## 内核实现分析

## 实战：主题

## 可视化：主题

## 常见问题与排错

## 总结

## 本章参考来源
```

章节可根据主题调整小节名称，但完整章节必须包含学习目标、正文原理、实现分析、可复现实验或操作说明、可视化内容、总结和参考来源。

## 代码块与命令

- 代码块必须标注语言，例如 `c`、`bash`、`python`、`go`、`rust`、`text`。
- 命令示例应写出环境假设、编译命令、运行命令和预期输出。
- 示例代码必须嵌入章节正文，并在代码后解释关键行、数据结构、helper 调用、挂载点和输出路径。不能只列文件名或命令。
- 完整章节不得写成提纲。每个核心小节至少应包含“背景问题 -> 示例/机制 -> 关键代码或命令 -> 输出解释 -> 常见失败点”中的多数要素。
- 未在当前环境实际运行的 eBPF 程序，不得写成“已验证通过”。使用“预期输出示例”或“在满足环境要求时应看到类似输出”。
- 复杂示例优先同时提供内联代码和 `book/examples/<chapter>/` 下的独立文件计划。

## 内核实现说明

- 提到内核路径时使用反引号，例如 `kernel/bpf/verifier.c`、`kernel/bpf/syscall.c`、`include/uapi/linux/bpf.h`、`net/core/filter.c`。
- 对于会随内核版本变化的细节，使用保守措辞，并在参考来源中列出上游文档或项目。
- verifier、JIT、map、BTF、CO-RE、XDP、TC、LSM 等主题必须同时解释用户态 API、内核路径和运行时数据流。

## 可视化

首批以文本图、流程图、拓扑图、火焰图生成流程描述为主。后续若引入图片或仪表板截图，应保留 Markdown 可读替代说明。

示例：

```text
用户态 loader
    |
    v
bpf(BPF_PROG_LOAD)
    |
    v
verifier -> JIT -> 挂载点 -> 事件触发
```

## 参考来源格式

每章末尾使用 `## 本章参考来源`，列出本章主要来源。优先写具体页面、项目或文档类型：

- Linux kernel documentation: BPF subsystem, instruction set, maps, helpers, BTF。
- ebpf.io: What is eBPF, infrastructure, use cases。
- Andrii Nakryiko: BTF and CO-RE articles。
- BCC/bpftrace/Cilium/libbpf/libbpf-bootstrap upstream documentation。
- ebpf.top: BPF 入门系列相关文章。

## 验证标注

- “本地验证”：仅用于确实在当前工作环境运行过的命令。
- “预期输出”：用于教学推导或需要特权内核环境的示例。
- “待验证”：用于后续批次需要在专门环境编译或压测的代码。

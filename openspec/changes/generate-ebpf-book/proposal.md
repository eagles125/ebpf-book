## Why

用户需要一本体系化的中文 eBPF 自学书籍，内容覆盖基础原理、Linux 内核实现、可观测性、网络优化、CO-RE 可移植开发以及 Go/Rust 互操作。该书目标总量大、章节多、代码示例多，必须先建立可持续的 Markdown 书稿结构和质量约束，避免一次性生成难以审阅和维护的超大文档。

## What Changes

- 新增 `book/` 目录作为书稿根目录，按章节和附录拆分 Markdown 文件。
- 新增全书入口 `book/README.md`，包含书名、目标读者、前置知识、完整 25 章目录、附录和分批生成计划。
- 新增 `book/STYLE.md`，约束中文术语、章节结构、代码块、预期输出、参考来源和未验证示例标注方式。
- 生成第 1 章完整样章，用于校准后续章节的中文风格、技术深度、引用格式和可视化表达。
- 生成第 9 章深度骨架，锁定 ebpf.top BPF 入门系列必须覆盖的 Hello World、kprobe、tracepoint、map、perf event 路径。
- 生成第 17 章深度骨架，锁定高性能 eBPF 防火墙原型的 XDP/TC、conntrack、ACL、NAT、校验和、控制面和压测内容。
- 将第 17 章从深度骨架升级为完整教学章节：按依赖、编译、部署、运行、效果五层阐述环境，核心代码段配原理讲解与中文注释，采用教师-学生双视角叙事，章末布置扩展任务并提供参考实现，落地 `book/examples/chapter-17/` 全部独立示例资产。
- 建立全书教学式写作规范（五层环境阐述、核心代码原理注释、教师-学生双视角、课后扩展任务与参考实现），写入 `book/STYLE.md` 并约束后续批次章节。
- 新增附录 stub，作为后续完善指令集、工具命令、BTF 配置和参考链接的承载位置。

## Capabilities

### New Capabilities

- `ebpf-book-manuscript`: 生成和维护《eBPF 从入门到精通：原理、内核实现与实战可移植开发》的分章 Markdown 书稿、写作规范、首批样章与关键章节骨架。

### Modified Capabilities

无。

## Impact

- 新增书稿文件：`book/README.md`、`book/STYLE.md`、`book/chapters/*.md`、`book/appendix/*.md`。
- 新增 OpenSpec 规划文件：`openspec/changes/generate-ebpf-book/`。
- 不引入应用运行时依赖，不修改已有代码接口。
- 首批输出不承诺完整 25 万字终稿；后续按批次继续生成和验证。

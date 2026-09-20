# Brainstorm Summary

- Change: generate-ebpf-book
- Date: 2026-09-20
- Status: 已定稿（用户已确认设计方案）

## 确认的技术方案

**方案 A：渐进搭建式教学章节**（用户已确认）。

三项前置澄清（用户已确认）：
1. **验证策略**：编译级真实验证 + 运行效果写「预期输出示例」+「运行级待验证」标注。本机可验证：clang 编译 BPF C、bpftool 生成 vmlinux.h/skeleton、Go 构建；不可验证：加载/挂载/veth 拓扑/压测（无 root，`unprivileged_bpf_disabled=2`）。
2. **课后参考实现**：完整可编译——每练习独立目录、完整源码、复用主示例构建体系、附思路讲解与验证状态。
3. **示例组织**：渐进式阶段目录（与第 9 章模式一致）。

**设计要点**：
- 示例资产：`book/examples/chapter-17/` 下 `00-env`（check-env.sh）、`01-xdp-drop`、`02-parse-acl`、`03-conntrack`、`04-nat-tc`、`05-firewall`（完整系统 + Go/cilium-ebpf 控制面）、`scripts/`（setup-veth.sh、bench-iperf.sh、bench-pktgen.sh、verify-build.sh）、`exercises/`（4 项练习）。
- 阶段 1–4 用 C loader（libbpf-bootstrap 风格），阶段 5 用 Go 控制面。
- 章节结构：学习目标 → 导读（双视角路线图）→ 环境准备（五层）→ 总体架构 → 五个渐进阶段 → 压测解读 → 可视化 → 排错 → 课后任务 → 总结 → 参考。
- 每阶段四段式教学节奏：教师视角引入 → 核心代码原理讲解（源码中文注释）→ 学生动手（命令+预期输出）→ 检查点自测。此节奏沉淀为 STYLE.md 教学模板。
- 课后练习 4 项：IPv6 ACL、规则持久化与重启恢复、Prometheus 指标导出、按端口限速；练习 README 任务与提示在前、参考实现思路独立成节（防剧透）。
- 篇幅目标：章正文 40–60k 字符（现骨架 8.3k）。

## 关键取舍与风险

- 渐进阶段间存在构建文件重复 → 接受（教程仓库标准做法，换取每阶段独立可编译）。
- cilium/ebpf 依赖需网络拉取 → go.mod 固定版本；构建时若网络不可用则补充说明。
- pktgen 压测脚本无法本地验证 → 标注待验证 + 预期输出。
- 篇幅膨胀风险 → 四段式模板 + STYLE.md 统一结构控制。
- tasks.md §5.1.2/5.1.3 需从单一 firewall/ 项目重组为渐进阶段结构（已确认，随 Design Doc 回写）。

## 测试策略

- 编译级验证（本机真实执行）：每阶段 Makefile（clang -target bpf 编译 BPF C、bpftool gen skeleton、C loader 构建、Go 构建）+ 顶层 `scripts/verify-build.sh` 汇总。
- 运行级：预期输出示例 + 「待验证」标注；00-env/check-env.sh 在无特权环境给出明确诊断。

## Spec Patch

无（delta spec 现有需求已覆盖渐进阶段设计；tasks.md 任务清单结构调整为设计回写，非 delta spec 变更）。

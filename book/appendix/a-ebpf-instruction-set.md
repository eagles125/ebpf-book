# 附录 A eBPF 指令集速查表

## 目标

本附录后续用于整理 eBPF 指令编码、寄存器约定、ALU/JMP/LD/ST 指令类别、调用约定和常见反汇编阅读方法。

## 计划内容

- eBPF 11 个通用寄存器的约定：`r0` 至 `r10`。
- `r0` 返回值、`r1` 至 `r5` helper 参数、`r10` 只读帧指针。
- 指令格式：opcode、dst/src register、offset、immediate。
- ALU、JMP、LD、ST、STX、LDX 指令速查。
- helper call、BPF-to-BPF call、exit 指令。
- `bpftool prog dump xlated` 输出解读。

## 参考来源

- Linux kernel documentation: BPF instruction set。
- `include/uapi/linux/bpf.h`。

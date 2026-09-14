# 附录 C 各发行版内核编译 BTF 配置指南

## 目标

本附录后续用于说明不同发行版上如何确认、安装或生成内核 BTF，以支持 CO-RE。

## 计划内容

- 检查 `/sys/kernel/btf/vmlinux`。
- `CONFIG_DEBUG_INFO_BTF`、`CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT` 等配置说明。
- Ubuntu/Debian、Fedora/RHEL、Arch、openSUSE 的 BTF 获取路径。
- 使用 `pahole` 从 DWARF 生成 BTF 的注意事项。
- 容器镜像中分发 BTF 或依赖宿主 BTF 的取舍。

## 参考来源

- Linux kernel BTF documentation。
- Andrii Nakryiko: BTF and CO-RE articles。
- libbpf documentation。

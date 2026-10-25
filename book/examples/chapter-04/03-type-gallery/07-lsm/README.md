# 07-lsm：把 BPF 程序变成一条安全策略——file_open 审计计数（类型画廊⑦）

《第 4 章 eBPF 程序类型与钩子点深入》「三、类型画廊：十四类逐类走读」3.14 节
（lsm+netfilter 合并 card）的 lsm 半。LSM BPF 把程序插进内核安全决策点（hook）：
每次文件打开都先问一句「允许吗」。与追踪/网络类最大的差别在 ctx——hook 原型的
参数直接直列成形参，内核对象（`struct file`）不经 pt_regs 转接、直接可读，
这是安全类类型独有的视角。

## 任务一句话

写一个 `SEC("lsm/file_open")` 程序（`BPF_PROG` 宏）：每次文件打开计数 +1，恒返回
0（允许）——只审计不拦截的教学示例。

## 代码走读

- **hook 原型直列**：`int BPF_PROG(lf, struct file *file)`——`BPF_PROG` 宏
  （`<bpf/bpf_tracing.h>`）按 hook 原型把参数直列成形参；file_open hook 的原型
  即 `struct file *`，内核对象直接可读——安全类类型与追踪/网络类的最大差别。
- **返回值语义**：0 允许；非 0 阻断该操作。各 hook 的裁决语义以所用内核源码
  `include/linux/lsm_hook_defs.h` 为准（本程序恒 0=允许，教学只审计不拦截）。
- **SEC 命名**：`SEC("lsm/file_open")`——前缀 `lsm/` 决定程序类型，斜杠后是
  hook 名，按内核 hook 表解析；想换 hook（如 `lsm/bprm_check_security`）改 SEC
  串与形参原型即可。
- **map 选型**：单 key ARRAY（`lsm_opens`）——审计计数；`__sync_fetch_and_add`
  原子递增。
- **失败分支必检**：`if (!v) return 0`——lookup 检查不可省（第 3 章画廊 card ①）；
  此处失败返回 0=允许，审计计数缺失不影响放行，与「只审计」职责一致。

## 挂载方式与前提

- **预期加载前提（本类型特有，三缺一 load 即失败——预期失败，非缺陷）**：
  1. root/特权档；
  2. 内核配置 `CONFIG_BPF_LSM=y`（本机已实证 =y，`/boot/config-7.0.0-38-generic`）；
  3. 内核启动参数 `lsm=` 列表含 `bpf`（核对点：`cat /sys/kernel/security/lsm`
     输出须含 `bpf`；编写环境当前列表为
     `lockdown,capability,landlock,yama,apparmor,ima,evm`，不含 `bpf`——运行级
     实验还需以 `lsm=bpf` 启动参数重启，故整体运行级待验证）。
- **挂载形态**：BPF LSM 程序 load 后即进入该 hook 的裁决链（skeleton attach
  自动完成，无 cgroup/进程级选择粒度——hook 是全局的）；程序释放即退出裁决链。
- **失败模式回指**：前提缺失类失败见上（load 即拒）；程序内 map 异常回 0=允许
  （fail-open，见代码走读）——与画廊⑥判决类 fail-closed 对照。

## 预期输出示例

（预期输出示例 / 运行级待验证）满足上述前提后随便跑几条打开文件的命令
（`ls`/`cat`），读 map：

```text
$ sudo bpftool map dump name lsm_opens
key: 00 00 00 00  value: 7b 00 00 00 00 00 00 00   # file_open 审计计数
Found 1 element
```

计数随「本机发生的文件打开」持续增长；具体数值随负载，以实测为准。

## 验证状态

- **编译验证**：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）
  `make` 通过，产出 `lsm.bpf.o`。
- **本章补充（C6 口径）**：画廊程序验证状态为「编译级已通过；加载级/运行级待特权环境验证」。
- **待运行验证**：编写环境为非 root（`unprivileged_bpf_disabled=2`），且当前
  `lsm=` 启动参数不含 `bpf`（见「挂载方式与前提」），未执行加载与运行；
  上方「预期输出」为教学示例。

## 参考来源

- Linux kernel documentation：`Documentation/bpf/prog_lsm.rst`（BPF LSM 语义与
  内核配置前提）。
- 内核源码 `include/linux/lsm_hook_defs.h`：hook 原型表——`file_open` 的参数与
  裁决语义以所用内核此文件为准（同本程序头注口径）。
- `<bpf/bpf_tracing.h>`：`BPF_PROG` 宏定义（hook 原型参数直列）。
- 第 3 章失败画廊 card ①：`book/examples/chapter-03/02-fail-gallery/01-map-null/`
  （本程序 `if (!v)` 分支的出处）。

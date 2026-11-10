# 练习 2：两级事件 opensnoop——成功 minimal、失败带 errno

第 9 章《eBPF 可观测性入门：基于 ebpf.top 入门系列》课后扩展任务 2（新增类）：以 05-open-file-kprobe（`../../05-open-file-kprobe/`）为基线，给事件结构增加 `failed`/`ret` 字段，构成「两级事件」——成功路径保持 minimal 信息，失败路径带 `-errno` 并在用户态映射成 errno 名。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在 05 的 opensnoop 基础上完成事件结构演化：

1. 共享头里的事件结构增加 `failed`（0/1）与 `ret`（open 返回值）两个字段，注意 ring buffer 记录的 8 字节对齐硬性要求在新布局下仍然成立。
2. 内核态：返回值在 kretprobe 才拿得到——enter 阶段把 comm/filename 暂存、exit 阶段查回后输出完整事件；`failed` 按 `ret < 0` 置位。成功与失败**共用同一个事件结构、同一条事件流**。
3. 用户态：成功行按 05 同形的 minimal 三列打印（`PID COMM FILE`）；失败行在行尾追加 `ret` 与 errno 名（如 `-2` → `ENOENT`）。表外错误码的回退行为由你定，并在代码注释里说清。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] 触发成功与失败两类 open（如 `cat /etc/hosts` 与 `cat /not-exist-file`）：成功行为三列 minimal 形态，失败行行尾带 `ret=-N` 与 errno 名。
- [ ] 能说明 `failed` 字段与 `ret < 0` 的信息冗余关系，以及为什么仍然把 `failed` 显式放进事件结构。
- [ ] 能说明 exit 阶段「即使 ringbuf 满、事件没发出去也要删临时项」的两个后果（对照正文 §10 检查点 3）。

## 提示

只给「会用到什么」，不给写法：

- 返回值语义见正文 §9/§10：非负是 fd，负值是 `-errno`。只挂 kprobe 拿不到返回值；kretprobe 的上下文里又没有 filename 参数——「enter 暂存 + exit 查回」的关联机制正文 §10 已讲过一遍，key 该用什么、为什么不用裸 tgid？
- 新字段怎么排才能不引入显式 padding：把两个 4 字节字段放在一起、`long ret` 放在最后，数一数自然对齐是否恰好 8 的倍数。
- 临时项（inflight）的清理时机：成功路径和失败路径都要清理；ringbuf reserve 失败的分支呢？
- errno 名的映射放用户态：`<errno.h>` 里的枚举常量可以直接当 switch 的 case；不必把错误码硬编码成数字。
- 与 07-open-file-return（`../../07-open-file-return/`）的关系要自己想清楚：机制同源，口径不同——07 每个 open 输出一条带 `RET` 列的完整事件；本练习是两级输出。不要把练习做成 07 的第二份拷贝。

## 环境前提与构建

同 `../../05-open-file-kprobe/`：Linux kernel 5.10+（`do_sys_openat2` 需 5.6+），开启 `CONFIG_DEBUG_INFO_BTF`；`clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `bash ../../00-env/check-env.sh` 自检）。运行 loader 需要 root 或 `CAP_BPF`/`CAP_PERFMON`；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。

```bash
cd book/examples/chapter-09/exercises/02-minimal-failed
make
sudo ./opensnoop_mf
# 另一终端分别触发成功与失败：
cat /etc/hosts                              # 成功行：minimal 三列
cat /not-exist-file                         # 失败行：行尾带 ret=-2 ENOENT
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

成功行为 05 同形的 minimal 三列，失败行行尾追加诊断（预期输出为教学示例）：

```text
PID      COMM             FILE
1001     cat              /etc/hosts
1002     cat              /not-exist (ret=-2 ENOENT)
1003     cat              /root/.bashrc (ret=-13 EACCES)
```

- 列宽与 `opensnoop_mf.c` 中 `handle_event` 的 printf 格式串逐列排布：成功行 `%-8u %-16s %s`，失败行同宽、行尾 `(ret=%ld %s)`。
- 与 07 的输出形态区分：07 是四列表格（`PID COMM RET FILE`，成功 `fd=3`、失败 `ERR=-2`），且仅对 `-2` 特判了 ENOENT；本练习成功行不带任何返回值列（minimal），失败行的 errno 名覆盖常见错误码全集、表外回退 `errno`。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`opensnoop_mf_common.h`/`opensnoop_mf.bpf.c`/`opensnoop_mf.c`），建议自行实现后再对照：

1. **事件结构（`opensnoop_mf_common.h`）**：`__u32 pid` + `__u32 failed` 相邻，随后 `char comm[16]`、`char filename[128]`、`long ret`——布局自然对齐：4+4=8、comm 到 24、filename 到 152、`long ret` 恰落在 152（8 的倍数），总大小 160 字节，**不需要显式 padding**。若把 `failed` 放到 `ret` 之后（`__u8`），结构体尾部会补 7 字节隐式填充，虽也能对齐，但字段顺序「小的在前、长的在后」更省事也更可读。
2. **`failed` 的冗余与保留**：`failed = ret < 0` 与 `ret` 的符号位信息重复——保留它的理由是**模式自描述**：消费方按「两级事件」的模式分支（`if (e->failed)`），不必各自再解释一次「ret 是什么含义、负数算什么」；事件结构本身就是 schema 文档。这正是本练习与 07 的口径差：07 的消费方必须理解 `ret` 列的语义（fd/ERR 二义），本练习把「成功/失败」提升为事件的一等字段。
3. **enter/exit 关联（`opensnoop_mf.bpf.c`）**：机制与 07 同源、逐段同构——enter 把 `struct inflight_value { comm, filename }` 写入 HASH map（key = 完整 64 位 `pid_tgid`，多线程安全），exit 查回、补 `ret`/`failed` 后输出，最后 `bpf_map_delete_elem` 清理。清理在 reserve 失败分支也执行（先删再返回）：否则旧 filename 会被后续 exit 错误关联（事件串位）、map 持续增长（泄漏）——正文 §10 检查点 3 的两个后果。练习价值不在机制（07 已教）而在事件结构与输出语义的演化，README 不重复讲关联机制的推导。
4. **errno 名映射（`opensnoop_mf.c` 的 `errno_name`）**：switch 的 case 直接用 `<errno.h>` 枚举常量（`ENOENT`、`EACCES`、`EPERM`、`ENOTDIR`、`EISDIR`、`ELOOP`、`ENAMETOOLONG`、`EMFILE`、`ENFILE`、`ENOSPC`、`EROFS`），覆盖 opensnoop 实际会遇到的主要错误码；表外回退 `"errno"`。转换在用户态做——内核态 switch 一个个比错误码也能做，但那是在每个失败事件上白花指令，用户态一次映射即可；正文 §10 的「完整工具」指的就是这个位置。进阶：接 `strerror_r()` 输出错误描述串。
5. **两级输出（`handle_event`）**：同一回调、同一 printf 列宽（`%-8u %-16s`），成功行尾部无额外内容、失败行追加 `(ret=%ld %s)`——两级共享前缀列，肉眼扫一列即可分辨成败，这是「minimal + 失败增强」形态相对「全量 RET 列」的排版优势。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- ebpf.top：BPF 入门系列实践教程五/七（opensnoop 与返回值篇）；本练习为五的事件结构 + 七的返回值语义的组合改造。
- 本章正文 §9/§10：返回值语义（fd / -errno）、enter/exit 关联与临时 map 清理、errno 映射的「完整工具」口径。
- 内核源码走读树 v6.6.78：`fs/open.c`（`do_sys_openat2` 原型）。
- libbpf 文档：`bpf_ringbuf_reserve`/`bpf_ringbuf_submit`、HASH map 与 `bpf_map_delete_elem`。
- 基线示例 `book/examples/chapter-09/05-open-file-kprobe/`（事件结构基座）与 `book/examples/chapter-09/07-open-file-return/`（关联机制同源参照，口径区分见上文）。

# 05-open-file-kprobe：用 kprobe 捕获进程打开的文件

对应 ebpf.top BPF 入门系列实践教程五（捕获进程打开文件信息，系列第 8 篇），以及本书第 9 章第 8 节。

## 演示目标

- 学会用 kprobe 动态追踪任意内核函数（不依赖内核预埋事件点）。
- 掌握 kprobe 参数读取：`BPF_KPROBE` 宏按位置绑定参数，等价于 `PT_REGS_PARMn(ctx)`。
- 掌握用户指针与内核指针的区别：openat 的 filename 是用户态地址，
  必须用 `bpf_probe_read_user_str()` 复制，不能直接解引用。

## 文件说明

| 文件 | 说明 |
|---|---|
| `opensnoop_kprobe_common.h` | 共享事件结构 `{pid, comm, filename}` |
| `opensnoop_kprobe.bpf.c` | 内核态：kprobe `do_sys_openat2` + ring buffer |
| `opensnoop_kprobe.c` | 用户态：poll + 表格输出 |

## 环境前提

- 同 `../01-hello-world/`；`do_sys_openat2` 需要 5.6+。
- kprobe 需要内核符号可见（`sudo cat /proc/kallsyms | grep do_sys_openat2` 确认）。

## 编译与运行

```bash
cd book/examples/chapter-09/05-open-file-kprobe
make
sudo ./opensnoop_kprobe
# 另一终端触发：
cat /etc/hosts
ls /tmp
```

## 预期输出

```text
PID       COMM             FILE
1001      cat              /etc/hosts
1002      ls               /tmp
1003      nginx            /var/log/nginx/access.log
```

## 代码讲解要点

- **挂载点选择**：`do_sys_openat2` 是 `openat`/`openat2` 系统调用的共同内核入口，
  x86_64 上 5.6+ 存在。更早内核可用 `do_sys_open`。也可改挂
  `__x64_sys_openat`（参数需要从 `struct pt_regs` 转换，见内核 `SYSCALL_DEFINE` 宏）。
- **BPF_KPROBE 宏**（`bpf_tracing.h`）：把 `opensnoop_entry` 展开为
  `int opensnoop_entry(struct pt_regs *ctx, int dfd, const char *filename, ...)`，
  参数按原函数位置绑定。不用宏时的等价写法是 `PT_REGS_PARM2(ctx)`。
- **用户指针读取**：`bpf_probe_read_user_str(&dst, sizeof(dst), (const void *)filename)`
  从当前进程地址空间复制字符串，自带失败与截断处理；读内核地址则用
  `bpf_probe_read_kernel_str()`。两者混用是 verifier 报错的高频原因。
- **为什么 enter 阶段读文件名**：系统调用刚进入时参数指针仍然有效；
  是否成功、fd 是多少要等返回（这正是 `../07-open-file-return/` 的主题）。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| `kprobe not found in kallsyms` | 内核没有该符号（版本过老），换 `do_sys_open` 或 tracepoint 版本 |
| verifier 报 invalid mem access | 把用户指针当内核指针解引用了；改用 `bpf_probe_read_user_str` |
| 文件名乱码或为空 | 读取失败（指针无效/进程已退出）；生产工具应检查 helper 返回值 |
| 输出刷屏 | 系统所有 openat 都被捕获；可叠加 02 的 PID 过滤 map |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程五（捕获进程打开文件信息）。
- libbpf 文档：`BPF_KPROBE` 宏（bpf_tracing.h）、`bpf_probe_read_user_str`。
- 内核源码 `fs/open.c`：`do_sys_openat2` 原型。

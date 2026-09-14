# 07-open-file-return：关联 enter/exit 捕获打开文件的返回值

对应 ebpf.top BPF 入门系列实践教程七（捕获进程打开文件信息返回值，系列第 10 篇），以及本书第 9 章第 10 节。

## 演示目标

- 把“两个事件拼成一次完整操作”：enter 记录 filename，exit 补上返回值。
- 掌握 kretprobe：`BPF_KRETPROBE(name, long ret)` 第一个参数即 `PT_REGS_RC(ctx)`。
- 掌握临时状态的生命周期纪律：enter 更新 map -> exit 查找 -> 输出 -> **删除**。
- 学会解读 open 的返回值：非负是 fd，负值是 `-errno`（如 `-2` = `ENOENT`）。

## 事件流

```text
openat(dfd, filename, flags)
        |
        v  kprobe/do_sys_openat2 (enter)
读取 filename/comm -> 写入 inflight[key=pid_tgid]
        |
        v  （内核执行真正的打开操作）
        |
        v  kretprobe/do_sys_openat2 (exit)
读取 ret -> 查回 inflight -> ringbuf 输出完整事件 -> 删除 inflight 项
```

## 文件说明

| 文件 | 说明 |
|---|---|
| `openret_common.h` | 共享事件结构 `{pid, comm, filename, ret}` |
| `openret.bpf.c` | 内核态：kprobe + kretprobe 成对挂载，inflight HASH 关联 |
| `openret.c` | 用户态：按返回值语义格式化输出 |

## 环境前提

同 `../05-open-file-kprobe/`（`do_sys_openat2` 需 5.6+）。

## 编译与运行

```bash
cd book/examples/chapter-09/07-open-file-return
make
sudo ./openret
# 另一终端触发成功与失败两种情况：
cat /etc/hosts          # 成功：RET 为 fd
cat /not-exist-file     # 失败：RET 为 -2 (ENOENT)
```

## 预期输出

```text
PID       COMM             RET        FILE
1001      cat              fd=3       /etc/hosts
1002      cat              ERR=-2(ENOENT)  /not-exist-file
```

## 代码讲解要点

- **key 用完整 64 位 `pid_tgid`**：只取 tgid（进程）会在多线程程序中冲突——
  同一进程多个线程可能同时 open；`tgid<<32 | tid` 才能区分线程。
- **`bpf_map_delete_elem()` 不是可选项**：不清理会导致 map 无限增长
  （长时间运行的工具内存泄漏），且旧 filename 残留会被后续 exit 错误关联。
  即使 ringbuf 预留失败，示例也保证先删 map 再返回。
- **返回值语义**：内核返回 `-errno`。常见值：`-2` ENOENT（文件不存在）、
  `-13` EACCES（权限不足）、`-21` EISDIR（是目录）。生产工具在用户态把
  `-ret` 交给 `strerror()` 输出可读错误。
- **为什么 exit 阶段不重读 filename**：kretprobe 上下文里拿不到函数入参
  （需要 `bpf_get_func_arg` 等较新 helper），经典做法就是 map 关联。

## 常见失败点

| 现象 | 原因与处理 |
|---|---|
| 事件缺 filename 或串位 | inflight 没删干净，或 key 只用了 tgid（多线程冲突） |
| map 数量持续增长 | exit 路径漏了 delete（包括错误分支） |
| kretprobe 挂载失败 | 内核配置或符号问题；确认 `/proc/kallsyms` 中符号存在 |
| 事件对不上 | 深度嵌套/异步场景需要额外时间戳或调用栈信息做关联键 |

## 验证状态

- 编译验证：本仓库编写环境（x86_64、内核 7.0、clang 18、libbpf 1.3）`make` 通过。
- 待运行验证：编写环境为非 root，未执行加载与运行；“预期输出”为教学说明。

## 参考来源

- ebpf.top: BPF 入门系列实践教程七（捕获进程打开文件信息返回值）。
- libbpf 文档：`BPF_KRETPROBE` 宏、HASH map 生命周期。
- BCC opensnoop 上游实现（工业级 return-value 关联参考）。

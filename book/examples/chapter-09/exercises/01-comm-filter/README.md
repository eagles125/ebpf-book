# 练习 1：把 PID 过滤改成进程名过滤

第 9 章《eBPF 可观测性入门：基于 ebpf.top 入门系列》课后扩展任务 1（改写类）：以 02-pid-filter（`../../02-pid-filter/`）为基线，把过滤维度从 PID 换成进程名（comm），挂载点与输出行保持与基线同形。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在 02-pid-filter 的基础上完成过滤维度替换：

1. 内核态：`target_pid`（ARRAY map，value `__u32`）换为 `target_comm`（ARRAY map，value 16 字节进程名）；每次触发读取当前进程 comm，与目标名比较后再输出。输出行保持基线同形（`execve: pid=%u comm=%s`），仍然走 `bpf_printk` → `trace_pipe`。
2. 用户态：`-p <pid>` 参数换为 `-c <comm>`，把目标进程名写入 map；不带 `-c` 时不过滤——「不过滤」在字符串版下的对应约定由你设计，并在代码注释里说清。
3. 保持基线其余纪律：map 查找先判空再解引用、启动时一次写入、运行中更新立即生效、Ctrl-C 退出即卸载。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] `sudo ./comm_filter -c bash` 后，`trace_pipe` 只出现 comm 为 bash 的 `execve` 事件；不带 `-c` 时所有进程的事件都出现。
- [ ] 能说明为什么「补零约定」必须内核态与用户态两侧一致：任一侧第 16 字节或 NUL 之后残留非零字节，逐字节比较会发生什么。

## 提示

只给「会用到什么」，不给写法：

- comm 的长度约定是 16 字节（`TASK_COMM_LEN`）。`bpf_get_current_comm()` 在内核里走 `strscpy_pad`（v6.6.78 走读树 `kernel/bpf/helpers.c:264`）：NUL 之后补零——map 里那份「目标名」要用同样的补零约定，逐字节比较才可靠。
- ARRAY map 的 value 放 16 字节字符串，04/05 的事件结构都是用命名结构体包一层——照着做，并想想为什么「类型要有名字」对本练习格外重要（补零约定往哪儿写）。
- BPF 里没有现成的 strcmp 能比 map 里的值：`bpf_strncmp` 的第二串只收编译期常量（走读树 `kernel/bpf/helpers.c:586`，`ARG_PTR_TO_CONST_STR`）。一个 16 次的有界循环是 verifier 友好的写法。
- 「不过滤」怎么表示：02 用「值写 0」；字符串版的对应约定是什么、内核态怎么判？
- 用户态 `strncpy` 只在源串较短时才补零：手动补齐 NUL，别让第 16 字节是栈上垃圾。

## 环境前提与构建

同 `../../02-pid-filter/`：Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`；`clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `bash ../../00-env/check-env.sh` 自检）。运行 loader 需要 root 或 `CAP_BPF`/`CAP_PERFMON`；非 root 且 `unprivileged_bpf_disabled=2` 时 `open_and_load` 失败属预期。

```bash
cd book/examples/chapter-09/exercises/01-comm-filter
make

# 终端 1：只观察 bash 的 execve
sudo ./comm_filter -c bash

# 终端 2：查看输出并触发
sudo cat /sys/kernel/tracing/trace_pipe
/bin/true   # 若 /bin/true 由 bash 直接 exec，触发的是 bash 的事件
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

指定 `-c bash` 后，`trace_pipe` 只出现该进程名的事件（预期输出为教学示例）：

```text
            bash-12345   [002] .....  2345.6789: bpf_trace_printk: execve: pid=12345 comm=bash
```

其他进程（systemd、监控 agent 等）的 `execve` 不会再出现——过滤发生在内核态，与基线 02 的口径一致；差别只在「按什么比」：基线比较 4 字节整数，本练习比较 16 字节补零字符串。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`comm_filter.bpf.c`/`comm_filter.c`），建议自行实现后再对照：

1. **map value 换形态（`comm_filter.bpf.c`）**：`struct comm_value { char comm[16]; }` 包一层再 `__type(value, struct comm_value)`——`__type` 宏编码的是指针目标类型，BTF 实测为 PTR→ARRAY(char,16)（`bpftool btf dump` 可复现），value_size 解析为 16。裸数组 `__type(value, char[16])` 经实测同样编码出 16 字节 value（不会退化成 `char *`），但类型匿名、补零约定无处落笔；命名结构体与 04–07 的事件结构同一纪律，本练习照此办理。map 定义其余部分（ARRAY、`max_entries 1`、key `__u32`）与基线逐字一致。
2. **比较函数（`comm_eq`）**：16 次有界循环逐字节比较，`a[i] != b[i]` 即不等，`a[i] == 0` 即两串同时到尾（尾部都是补零）判等；16 字节都相等且无 NUL 也判等（覆盖恰好 15 字节 + NUL 的名字）。有界循环 verifier 可以完全展开，不会触发复杂度问题。不用 `bpf_strncmp` 的原因见提示第三条：它的第二串参数是 `ARG_PTR_TO_CONST_STR`，只收编译期常量，比不了 map 值。
3. **「不过滤」约定**：沿用基线「默认全放行」的语义，字符串版落为「map 里是空串（首字节 0）就不过滤」——`target->comm[0] != '\0'` 才进入过滤分支。用户态对应：不带 `-c` 时写全零 buffer（`char target[16] = {}`）。
4. **补零约定两侧成对**：内核态 `bpf_get_current_comm()` 是 `strscpy_pad`（NUL 之后补零），用户态写 map 用「全零初始化 + `strncpy` 到 15 字节 + 手动补末位 NUL」——`strncpy(dst, src, 15)` 只在源串不足 15 字节时补零，源串恰好 15 字节时不写终止符，所以末位 `\0` 必须手动补。任一侧尾部残留非零字节，比较就会在补零区失败，过滤「莫名失效」，这是本练习最常见的错法。
5. **与基线的差异点对照**：挂载点、输出行、生命周期（Ctrl-C 卸载）、运行中改过滤立即生效——全部不变；变的只有 map value 类型、比较逻辑与命令行参数。正因控制面/数据面通过 map 解耦，换过滤维度不需要动事件路径。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- ebpf.top：BPF 入门系列实践教程二（通过 PID 过滤，本练习基线）；`bpf_get_current_comm` helper 语义。
- 内核源码走读树 v6.6.78：`kernel/bpf/helpers.c:264`（`strscpy_pad` 补零语义）、`:586`（`bpf_strncmp` 的 `ARG_PTR_TO_CONST_STR` 参数约束）。
- libbpf 文档：ARRAY map 与 `bpf_map_update_elem`（BPF_ANY 语义）。
- 基线示例 `book/examples/chapter-09/02-pid-filter/`：本练习的改造对象，控制面/数据面雏形与其保持一致。

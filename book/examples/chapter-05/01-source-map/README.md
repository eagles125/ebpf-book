# 01-source-map:eBPF 子系统源码树地图

eBPF 的内核侧实现集中在 `kernel/bpf/` 一个目录里。本章正文《一、源码地图》一节按本 README 的分区展开;`citations.txt` 则是全章引用锚点的清单数据,`scripts/verify-citations.sh`(机检脚本)以它为唯一数据源,逐行到走读树里核对符号。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。本目录只做两件事:`wc -l` 统计行数、`grep -F` 核对符号——**只读源码树,不构建、不加载**。
- 术语约定:`map` 译作「映射」,`helper` 译作「辅助函数」,`program type` 译作「程序类型」,`attachment point` 译作「挂载点」。

## 一、kernel/bpf/ 分区导览(六区,45 个 .c 文件)

下表覆盖 `kernel/bpf/` 全部 45 个 .c 文件,共 65955 行。行数列在 v6.6.78 走读树上以 `wc -l` 统计。分区按「一条 bpf 命令从进门到出门」的路径划分:先过入口分发,再进验证器,拿到执行形态后按映射/对象落内存,最后由各类挂载点驱动执行。

| 分区 | 文件 | 行数 | 一句话职责 |
|---|---|---|---|
| 入口分发 | syscall.c | 5823 | bpf(2) 系统调用的唯一入口:命令分发、fd 生命周期、id 注册表与权限检查 |
| 验证 | verifier.c | 20459 | 验证器(verifier)主体:静态模拟每条指令,证明内存安全与可终止后才放行加载 |
| 验证 | tnum.c | 214 | 三值数字(tristate number)算术:跟踪寄存器里哪些位未知,支撑指针检查 |
| 验证 | log.c | 327 | 验证日志输出通道:拒绝原因与指令回溯统一从这里打印 |
| 验证 | disasm.c | 350 | 内核内建 eBPF 反汇编器:把指令按可读格式写进验证日志 |
| 执行与 JIT | core.c | 2975 | 解释器与通用 JIT:所有程序从这里获得可执行形态(`bpf_int_jit_compile` 的弱定义在此) |
| 执行与 JIT | dispatcher.c | 173 | BPF dispatcher:把同类程序聚合成一张间接跳转表,降低间接调用的开销 |
| 执行与 JIT | trampoline.c | 1051 | BPF trampoline:为 fentry/fexit 等「内核调程序」场景动态生成蹦床 |
| 执行与 JIT | mprog.c | 452 | bpf_mprog 多程序挂载框架:同一挂载点上排布多个程序的通用层(tcx 使用) |
| 执行与 JIT | offload.c | 865 | 网卡硬件卸载基础设施:验证后的程序交给驱动与网卡编译执行 |
| 映射实现 | arraymap.c | 1383 | 数组映射(BPF_MAP_TYPE_ARRAY 系):按索引访问,也是 per-CPU 数组与嵌套映射的底座 |
| 映射实现 | hashtab.c | 2630 | 哈希映射(BPF_MAP_TYPE_HASH 系)及 LRU 变体:最常用的映射实现 |
| 映射实现 | ringbuf.c | 809 | 环形缓冲区映射:共享内存上的多生产者无锁记录式输出 |
| 映射实现 | lpm_trie.c | 766 | 最长前缀匹配映射(LPM Trie):典型场景是路由表 |
| 映射实现 | bloom_filter.c | 219 | 布隆过滤器映射:用少量内存换概率性成员判断 |
| 映射实现 | queue_stack_maps.c | 299 | 队列(FIFO)与栈(LIFO)映射 |
| 映射实现 | stackmap.c | 691 | 栈轨迹映射:配合 `bpf_get_stackid` 保存调用栈(可选 build_id) |
| 映射实现 | map_in_map.c | 150 | 映射嵌套映射(ARRAY/HASH_OF_MAPS)的通用查找层 |
| 映射实现 | cpumap.c | 779 | CPU 引流映射:把报文重定向到指定 CPU 的后端处理 |
| 映射实现 | devmap.c | 1164 | 网卡重定向映射:`bpf_redirect` 的虚拟出口表 |
| 映射实现 | reuseport_array.c | 353 | reuseport 重定向映射:套接字组内分流用的旁路表 |
| 映射实现 | percpu_freelist.c | 200 | per-CPU 空闲链表:哈希与布隆映射的节点内存回收 |
| 映射实现 | bpf_lru_list.c | 700 | LRU 逐出链表:哈希映射的 LRU 变体靠它决定「谁被逐出」 |
| 映射实现 | memalloc.c | 932 | BPF 专用内存分配器(bpf_mem_alloc):为哈希节点等对象提供免锁快速分配 |
| BTF 与本地存储 | btf.c | 8601 | BTF(BPF Type Format)核心:解析、校验与查找内核或用户提供的类型信息 |
| BTF 与本地存储 | sysfs_btf.c | 45 | 把 vmlinux 的 BTF 暴露为 `/sys/kernel/btf/vmlinux`,供工具侧读取 |
| BTF 与本地存储 | bpf_local_storage.c | 921 | 本地存储共享核心:按对象指针挂取数据的缓存与 RCU 生命周期 |
| BTF 与本地存储 | bpf_task_storage.c | 374 | task 本地存储映射(BPF_MAP_TYPE_TASK_STORAGE) |
| BTF 与本地存储 | bpf_inode_storage.c | 237 | inode 本地存储映射(BPF_MAP_TYPE_INODE_STORAGE) |
| BTF 与本地存储 | bpf_cgrp_storage.c | 240 | cgroup 本地存储映射(BPF_MAP_TYPE_CGRP_STORAGE,按 cgroup 挂点指针取) |
| BTF 与本地存储 | local_storage.c | 614 | cgroup 本地存储的旧式形态(BPF_MAP_TYPE_CGROUP_STORAGE,键=cgroup+挂载点) |
| 其他 | cgroup.c | 2602 | cgroup 挂载点:程序按 cgroup 层级挂载、随报文路径执行 |
| 其他 | bpf_iter.c | 848 | bpf_iter 迭代器框架:目标(target)注册,把内核对象序列化给用户态 |
| 其他 | cgroup_iter.c | 296 | cgroup 迭代器目标:遍历 cgroup 层级 |
| 其他 | task_iter.c | 864 | task 迭代器目标:遍历进程与线程 |
| 其他 | prog_iter.c | 107 | 程序迭代器目标:遍历已加载的 BPF 程序 |
| 其他 | link_iter.c | 107 | link 迭代器目标:遍历已创建的 bpf_link |
| 其他 | map_iter.c | 231 | 映射迭代器目标:遍历系统中的映射 |
| 其他 | bpf_lsm.c | 376 | 把程序类型暴露为 LSM 钩子:挂载点在安全钩子处自动生成 |
| 其他 | bpf_struct_ops.c | 910 | struct_ops 机制:让 BPF 程序实现内核结构体的操作表(如 TCP 拥塞控制算法) |
| 其他 | tcx.c | 346 | TCX(TC eXpress)挂载层:ingress/egress 的多程序链管理 |
| 其他 | net_namespace.c | 567 | 网络命名空间侧的 link 生命周期与清理钩子 |
| 其他 | cpumask.c | 466 | cpumask 的 BTF kfuncs(`bpf_cpumask_*` 辅助函数集),不是映射 |
| 其他 | helpers.c | 2622 | 跨程序类型共享的基础辅助函数实现与原型(`bpf_base_func_proto` 等) |
| 其他 | inode.c | 817 | bpffs 文件系统:pin(钉住)映射与程序用的最小文件系统后端 |

六个分区的记忆法:入口分发是「门」,验证是「安检」,执行与 JIT 是「发动机」,映射实现是「集装箱」,BTF 与本地存储是「说明书与贴纸」,其余是「各个码头」。

## 二、三层 include 边界:谁允许包含谁

**第一层:`include/uapi/linux/bpf.h`(UAPI 契约)。** 定义用户态与内核之间的全部约定:命令字枚举(`BPF_PROG_LOAD` 等)、参数联合 `union bpf_attr`、映射类型枚举与辅助函数编号。用户态(libbpf、bpftool)与内核态都允许包含它——它是内核对用户态唯一承诺稳定的部分。边界:它只被包含、不包含任何内核私有头;内核私有头可以包含它,方向不可逆。用户态工具从不包含内核私有头,它们使用工具自带的 uapi 副本。

**第二层:`include/linux/bpf.h`(内核对象)。** 定义映射与程序的核心内核对象:`struct bpf_map`(映射对象)、`struct bpf_map_ops`(映射操作回调表)、`struct bpf_prog`(程序对象)、`struct bpf_prog_aux` 等,以及加载链、验证器导出的函数原型。只有内核态代码允许包含它;它自己包含 `include/uapi/linux/bpf.h`——「契约在内层、实现在外层」的单向依赖,就是从这一行 `#include` 看到的现场。

**第三层:`include/linux/bpf_types.h`(注册表 X-macro)。** 文件头自注「internal file - do not include directly」:全文件没有任何结构体或函数,只有一列 `BPF_PROG_TYPE` / `BPF_MAP_TYPE` / `BPF_LINK_TYPE` 宏调用(v6.6.78 实测 32 行程序类型、44 行映射/link 类型)。因此它不允许被「正常包含」——只有先 `#define` 好配套宏、再 `#include` 它的翻译单元才能让它展开出内容;被包含几次,就按当次的宏定义展开出几张不同的表(见下一节)。

三层合起来的包含方向:`uapi/linux/bpf.h`(契约)← `linux/bpf.h`(对象)← `syscall.c` 等使用方;`bpf_types.h` 则游离在包含语义之外,它是被「两次定义宏、两次展开」的清单,不参与头文件依赖链。

## 三、双展开现场:同一张清单,两种展开

`include/linux/bpf_types.h` 的行都是宏调用,宏的含义由包含它的文件当场定义,这就是 X-macro(同一数据多次展开)惯用法。`kernel/bpf/syscall.c` 里有三处现场:

1. **第一次展开(映射类型注册表)**:`syscall.c:63` 定义 `static const struct bpf_map_ops * const bpf_map_types[] = {`,先把 `BPF_MAP_TYPE(_id, _ops)` 定义成 `[_id] = &_ops`(另外两个宏定义为空),再在 `:68` `#include <linux/bpf_types.h>`——一张「映射类型 → ops 表」的注册表就此展开。
2. **第二次展开(程序类型注册表)**:`syscall.c:2039` 定义 `bpf_prog_types[]`,这次把 `BPF_PROG_TYPE(_id, _name, ...)` 定义成 `[_id] = & _name ## _prog_ops`,在 `:2044` 再次 include 同一个头文件——同一份清单,展开出「程序类型 → prog_ops」的注册表。
3. **旁证(第三处)**:`syscall.c:2966` 的 `bpf_link_type_strs[]`(在 `CONFIG_PROC_FS` 之下,include 于 `:2968`)把 `BPF_LINK_TYPE` 行展开成类型名字符串,供 /proc 的 fdinfo 打印。三处展开、三种结果。

效果:新增一种程序类型或映射类型时,只需在 `bpf_types.h` 加一行宏调用,两张注册表同时「长」出来,入口分发无需改动——这就是本章标题所说的「一行宏展开成源码地图」。正文《一、源码地图》一节将逐段贴出上述两处源码与 `bpf_types.h` 的一行实例;对应的机检锚点见 `citations.txt` 中的 `bpf_map_types[]`、`bpf_prog_types[]`、`BPF_PROG_TYPE(BPF_PROG_TYPE_SOCKET_FILTER`、`BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY` 四行。

## 复算命令

以下两条命令可随时在走读树上重跑(只读):

```bash
# 六区分区表的行数列:kernel/bpf/ 全部 .c 文件行数
wc -l "$WALK_TREE"/kernel/bpf/*.c

# citations.txt 的锚点行数(跳过注释与空行)
grep -vc "^#\|^$" book/examples/chapter-05/01-source-map/citations.txt
```

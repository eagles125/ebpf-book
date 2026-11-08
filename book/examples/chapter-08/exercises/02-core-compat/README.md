# 练习② 跨版本兼容分支:仿 03 写一个「目标内核有啥读啥」的探针

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己写完 `compat.bpf.c` 并编过,再看 [`check-compat.sh`](check-compat.sh) 的断言口径与后部「参考思路」(参考实现 [`compat.bpf.c`](compat.bpf.c) 也在本目录,完成前勿看)。

## 任务

仿 [`../../03-core-read/core.bpf.c`](../../03-core-read/core.bpf.c) 写一份跨版本兼容分支探针 `compat.bpf.c`,要求:

1. 用 `bpf_core_field_exists` 构造一个**字段级**兼容分支:字段在本机 BTF 里存在才可编译;目标内核没有就走哨兵分支(0 值),不崩、不退加载;
2. 用 `bpf_core_type_exists` 构造一个**类型级**兼容分支:守卫式——类型存在,才敢向它要尺寸;
3. 探测对象自己选:必须是「**本机 BTF 在场、v6.6.78 走读树不在场**」的字段/类型,选好附两边 grep 对照(选择方法见提示);
4. 事件结构 + ring buffer 上报,挂 `tracepoint/sched/sched_process_exec`(骨架仿 03;本练习止步编译级,不配用户态加载器)。

## 提示

- 宏锚点(走读树 `tools/lib/bpf/bpf_core_read.h`,v6.6.78):`bpf_core_field_exists` 在 :132(支持 `p->f` 与 `struct t, f` 两种形式),`bpf_core_type_exists` 在 :185;
- 探测对象怎么选:先在本机 BTF 里挑——`bpftool btf dump file /sys/kernel/btf/vmlinux | grep -E "^\[[0-9]+\] STRUCT '候选名'"` 或直接翻 vmlinux.h;再到 v6.6.78 树 `grep -rwl 候选名 include/ kernel/` 确认**无命中**。两边都「在场」演示不出分支差;两边都「不在场」本机就编不过(字段必须在本机 vmlinux.h 里存在才可编译——第 2 章排错表处方的前半句「先看本机 BTF 再写代码」);
- **字段存在≠语义一致**(本练习教案点,正文总结同款措辞):CO-RE 保证的是**布局修正**(字段偏移/大小按目标 BTF 重算),不保证**语义不变**——同名成员跨版本可能改单位、改极性、改取值集合;存在性分支只挡「没有」,挡不住「有但变了」,后者要靠版本判据或字段命名隔离。别把「存在性判定过了」当成「语义兼容」;
- `-g` 别丢:两个判定宏都要靠 `.BTF.ext` 重定位记录在加载期常量化(正文 3.3 的命门);
- make 产物(compat.bpf.o/vmlinux.h)不入库(`.gitignore` 全局规则);加载运行为预期档(需 root),预期输出示例见参考思路。

## 验收

```bash
bash check-compat.sh
# 预期:TOTAL=6 PASS=6 FAIL=0,exit 0
# 两级断言:①make clean all 编译级通过(强制重编,clang 必须真跑);
#           ②锚点在场:两个判定宏 + 教案点注释「字段存在≠语义一致」
# 负路径(实测):COMPAT_SRC=/nonexistent/compat.bpf.c → FAIL=6 → exit 1;
#               CLANG=/nonexistent/clang → 编译级 FAIL → exit 1
```

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己写完 compat.bpf.c 并 make 编过</summary>

- **结构对照**:与 03 的 core.bpf.c 同骨架——vmlinux.h(include 处局部静音 vmlinux.h 的 FWD 噪音告警)+ bpf_helpers/bpf_core_read、license、事件结构+ringbuf、tracepoint 入口、`bpf_get_current_task_btf()` 取任务可信指针;
- **两个兼容分支**(compat.bpf.c:72-101):
  - 字段级:`if (bpf_core_field_exists(task->scx)) { 存在侧读 task->scx.weight } else { 哨兵 0 }`——编成 kind 2(BPF_CORE_FIELD_EXISTS)判定 + 存在分支内的 kind 0 取数;目标内核缺 `scx` 时,取数指令被 poison、死分支被验证器淘汰(正文 4.2 失败语义),程序照样加载;
  - 类型级:`if (bpf_core_type_exists(struct sched_ext_entity)) { 记 bpf_core_type_size(...) } else { 哨兵 0 }`——守卫式:存在性判定是后续访问的闸门,不是装饰;
- **探测对象的对照实证**(clang 18 + 本机 7.0 BTF 实测,其他内核数值不同):task_struct 成员 `scx`(成员序 23、位偏移 5952=字节 744)与类型 `sched_ext_entity`([149960] size=208 vlen=23),在 v6.6.78 走读树 `grep -rwl scx` / `grep -rwl sched_ext_entity`(include/ kernel/)均无命中——字段与类型双双随版本漂移;
- **本机切片实测**(04 切片器对 compat.bpf.o,5 条记录):`0:94` pid(kind 0)、`0:23` scx 存在判定(kind 2)、`0:23:6` weight 两段链取数(kind 0)、`sched_ext_entity` 类型存在(kind 8)与类型大小(kind 9,访问串恒 `"0"`);type_id 12=task_struct、67=sched_ext_entity(对象自己 BTF 里的坐标,正文 2.4「blob 内坐标」);
- **预期输出示例 / 运行级待验证**(需 root,本机 `unprivileged_bpf_disabled=2`;本机目标=编译机,两分支均走「存在」侧):

  ```text
  [core-compat] pid=2314  scx_exists=1 scx_weight=15  entity_exists=1 entity_size=208
  ```

  同一份 .bpf.o 拿到无 scx 的内核(如 6.6):预期 `scx_exists=0 scx_weight=0 entity_exists=0 entity_size=0`——两分支各走各的哨兵侧,同一份对象两种内核各判各的(正文 5.5 的「诚实补一句」同款:本机演示修正量不显形,跨内核才显形);
- **自查三件事**:make 零告警;`bash check-compat.sh` 全绿(TOTAL=6 PASS=6);探测对象的两边 grep 对照自己复现过一遍。

</details>

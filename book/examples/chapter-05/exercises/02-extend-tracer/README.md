# 02-extend-tracer:给 trace-map-ops.sh 增加 BPF_BTF_LOAD 站

《第 5 章 eBPF 子系统内核代码结构》「课后扩展任务」练习 2 的任务目录。
切片器是可扩展的——每个站只是「锚点模式 + sed 区间 + 职责注」三件套;
本练习把 4.1 的手法用在一个真实扩展上。`trace-btf-load.sh` 是参考实现
(剧透——先做后看):BPF_BTF_LOAD 命令链的三站切片器,与
[`../../03-map-ops/trace-map-ops.sh`](../../03-map-ops/trace-map-ops.sh)
同构;`check-trace.sh` 是它的自检脚本。本目录全程只读源码树:不构建、
不加载,对源码树零写入。

## 任务

在映射链切片器(`trace-map-ops.sh`,六站)的基础上新增 `BPF_BTF_LOAD`
站——`BPF_MAP_CREATE` 同一条 switch 的平级命令。要求:

1. 沿切片器两步法(4.1)定位分发支路 `case BPF_BTF_LOAD`;
2. **命令实体函数名自己 grep**——在 case 分支看调用句,回溯函数定义
   (这正是排错①练过的检索),切出其签名与关键段(syscall.c:4808 附近);
3. **标注跨文件边界**:命令实体把解析工作整包交给 `kernel/bpf/btf.c`
   (1.1 区 5)——切出 btf.c 一侧的接门函数与解析器入口;
4. 输出三层(定位 → 代码段 → 职责注)与汇总行和既有六站一致,每段
   ≤25 行;
5. 在本 README 失败模式表补一条**换树实测行为**(手法见「验证方式」,
   参考实现的实测记录见该表补条)。

验收标准:

- 站输出含三层,汇总行 `TREE_VERSION=v6.6.78 STATIONS=3`(独立三站;
  若整站并入原切片器则为 `STATIONS=7`);
- `bash check-trace.sh` 全绿(语法断言 + 实跑断言 + 三锚点断言),退出码 0;
- 失败模式表补齐换树实测一条,实测命令与结论可复现。

## 前置知识

- 4.1 切片器两步法(锚点 → 区间)——动手手法;排错①的符号检索策略与
  排错②的 grep 工具箱——找命令实体名的手艺;
- [`03-map-ops/README.md`](../../03-map-ops/README.md) 的站结构与失败
  模式表——你的新站要与既有六站同构,失败模式表同口径补条;
- 1.1 区 5:`kernel/bpf/btf.c` 的分区职责(说明书与贴纸)——跨文件边界
  站的落点。

## 提示

- 三站结构(与既有六站同构):①分发 `case BPF_BTF_LOAD`(在
  `__sys_bpf` 的 switch 里);②命令实体 `bpf_btf_load` 的签名与关键段;
  ③btf.c 一侧的 `btf_parse` 调用点——边界站。
- `case BPF_BTF_LOAD` 在 syscall.c 出现**两次**——`grep -n` 数命中,取
  正身;第二处的归属同练习①(见
  [`../01-trace-freeze/`](../01-trace-freeze/) 调用链图末段)。
- 命令实体名不背书:从 case 分支的调用句读出来,再 grep 它的定义行。
  v6.6.78 上它落在 :4808 附近——但先定位再抄行号,顺序不能反。
- btf.c 里 `btf_parse` 是**一族前缀同名函数**(`btf_parse_kptr`/
  `btf_parse_str_sec`/`btf_parse_hdr`/……)——锚点要写完整形态(带返回
  类型的定义行、或整句调用),别让前缀撞车。
- 偏移量以你的树实测为准:先 `sed -n` 看全函数,再定区间;切片器的
  「锚点行 + 偏移」两步法保证树小幅漂移时区间随锚点自动平移。
- 验证口径:`bash check-trace.sh` 全绿 = 被测脚本在场 + `bash -n` 语法
  断言 + 实跑退出码 0 + 三个锚点(`case BPF_BTF_LOAD`/`bpf_btf_load`/
  `btf_parse`)在实跑输出中全命中。

## 验证方式

- 自检:`bash check-trace.sh` 输出六条断言与
  `TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78`,
  `FAIL=0`(退出码 0)即全绿。
- 换树实测(任务要求的补条手法):
  `WALK_TREE=<另一棵树> bash check-trace.sh` 与
  `WALK_TREE=<另一棵树> bash trace-btf-load.sh`,记录哪几站 NOT FOUND、
  哪几站照常切出、退出码几何——行为随树而异,以你的实测为准。
- 失败模式表(与 03-map-ops README 同口径;#4 为任务要求的补条):

| # | 失败模式 | 实测行为 |
|---|---|---|
| 1 | 树不在(`WALK_TREE` 指错目录/源码未解压) | 启动即报「走读树目录不存在」并退出(码 1),一站不产(check-trace.sh 侧:实跑断言与三锚点断言 FAIL,汇总 `TOTAL=6 PASS=2 FAIL=4`,退出码 1——本机实测) |
| 2 | 锚点漂移(树在,锚点未命中) | 对应站打 `STATION n: NOT FOUND` 后继续其余站,退出码 1 |
| 3 | 版本不符 | stderr 打 WARN 后继续,行号仅供参考 |
| 4 | 换树实测(补条,v6.6.78 → 4.19.325) | `WALK_TREE=/work/linux/linux-4.19.325 bash trace-btf-load.sh`:stderr 打 `WARN: 树版本 4.19.325 …`,**站 1 照常切出**(`case BPF_BTF_LOAD` 命中 :2466)、**站 2/3 NOT FOUND**(命令实体签名无 `bpfptr_t uattr` 参数、btf_parse 调用语句形态不同——这两个锚点是 5.x+ 形态),退出码 1。教训:站 1 的锚点(命令字 `case BPF_BTF_LOAD`)自 4.18 起稳定存活,跨版本可用;越靠实现细节(完整签名、调用语句)的锚点,存活期越短——选锚点时「命令字优先于签名,签名优先于行号」 |

---

## 参考实现思路

**三站设计**(`trace-btf-load.sh`;行号 v6.6.78 实测,`grep -n` 可复现):

| 站 | 锚点(grep -F,取第 1 命中) | 实测行 | 区间(锚点+偏移) | 职责 |
|---|---|---|---|---|
| 1 | `case BPF_BTF_LOAD:` | syscall.c:5509(第二处 :5593) | :5508-5512(-1..+3) | switch(cmd) 分发支路,与映射命令平级 |
| 2 | `static int bpf_btf_load(const union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)` | syscall.c:4808 | :4808-4817(+0..+9) | 命令实体全函数:CHECK_ATTR → bpf_capable → 交棒 btf.c |
| 3 | `int btf_new_fd(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)` / `btf = btf_parse(attr, uattr, uattr_size);` / `static struct btf *btf_parse(const union bpf_attr *attr, bpfptr_t uattr, u32 uattr_size)` | btf.c:7183 / :7188 / :5474 | :7183-7192(+0..+9)、:5474-5485(+0..+11) | 跨文件边界:btf_new_fd 接门、btf_parse 解析 |

**逐站要点**:

- 站 1 只锚命令字、不锚签名——`locate` 取第 1 命中(:5509)即正身;
  `show_locate` 展示全部命中,两处对比本身就是教学点(同练习①)。4.19
  树上此站照常存活(失败模式表 #4),印证「命令字锚点最长寿」。
- 站 2 锚完整定义行:v6.6.78 的 `bpf_btf_load` 带 `(attr, uattr,
  uattr_size)` 三参(bpfptr_t 形态);整函数只有 10 行——**没有三层
  分工**,边界检查、特权闸(`bpf_capable`,`-EPERM`)之后一步交棒
  `btf_new_fd`。区间 +0..+9 恰好罩住全函数,偏移即「读到右括号为止」。
- 站 3 是边界站,三锚点各司其职:`btf_new_fd` 定义行(btf.c 对
  syscall.c 的接门人)、调用语句 `btf = btf_parse(attr, uattr,
  uattr_size);`(**整句**作锚点——若只锚 `btf_parse(` 会撞上
  `btf_parse_kptr`/`btf_parse_hdr` 等一族前缀同名函数)、`btf_parse`
  完整定义行(解析器入口,头部即 `BTF_MAX_SIZE` 尺寸闸与验证器环境
  分配)。第二段区间只切头部 12 行——解析主体(头/串/类型三节,
  :5520/:5526/:5530)留给你顺藤摸瓜。
- 与站 2 模板的关系:trace-map-ops.sh 站 2(`map_create` 与类型查找)
  的「定义锚点 + 头部区间 + 职责注」写法可直接照搬——所谓扩展,就是把
  三件套对新锚点再填一遍;`STATIONS=3` 的汇总行与降级逻辑照抄即可。

## 调用链图

```text
SYSCALL_DEFINE3(bpf, ...)              syscall.c:5567   ← bpf(2) 唯一真身
  └─ __sys_bpf(cmd, USER_BPFPTR(uattr), size)      :5426(三道前置 :5431/:5438/:5441)
       └─ switch (cmd)                              :5445
            └─ case BPF_BTF_LOAD:                   :5509   ← 正身(第二处 :5593 在 bpf_sys_bpf 白名单)
                 └─ bpf_btf_load(&attr, uattr, size)   :5510 → :4808   【站 1 → 站 2】
                      ├─ CHECK_ATTR(BPF_BTF_LOAD)       :4810-4811  -EINVAL
                      ├─ bpf_capable()                   :4813-4814  -EPERM(特权闸,1.3 开关)
                      └─ btf_new_fd(attr, uattr, uattr_size)  :4816 → btf.c:7183   【跨文件边界 → 站 3】
                           ├─ btf_parse(attr, uattr, uattr_size)  :7188 → btf.c:5474
                           │    ├─ attr->btf_size > BTF_MAX_SIZE → -E2BIG   :5484-5485
                           │    └─ 解析三节:头 :5520 / 串 :5526 / 类型 :5530
                           └─ btf_alloc_id(btf)               :7192  发编号(同 prog_idr 惯用法)
```

与映射链(create 查 `bpf_map_types[]` 取 ops 后走三层)对照:本链在
:4816 一站式跨文件——BTF 对象的「实现」不散布在 ops 回调表里,而是
整装在 btf.c 一个文件中;两种组织形态都是 1.1 分区的活例子。

## 验证状态

- 机检级:`trace-btf-load.sh` 与 `check-trace.sh` 已在本机走读树
  (v6.6.78)实跑:切片器三站全出、末行 `TREE_VERSION=v6.6.78
  STATIONS=3`;`check-trace.sh` 输出 `TOTAL=6 PASS=6 FAIL=0`,退出码 0。
- 负路径:`WALK_TREE=/nonexistent bash check-trace.sh` 实测——语法断言
  仍 PASS、实跑断言 FAIL(被测脚本启动即退 1)、三锚点断言 FAIL,汇总
  `TOTAL=6 PASS=2 FAIL=4`,退出码 1(与失败模式表 #1 一致)。
- 换树:`WALK_TREE=/work/linux/linux-4.19.325` 实测记录见失败模式表 #4
  补条(站 1 存活、站 2/3 NOT FOUND、退出码 1)。
- 运行级:本练习不执行任何 BPF 命令、不加载 BTF 对象;`BPF_BTF_LOAD`
  的运行级效果(真实加载一份 BTF 并领编号)不在切片与机检范围,本目录
  无任何运行级表述——切片器与机检均为只读源码树动作。
- 行号:全部树上实测(`grep -n`),随版本漂移;换树以实测为准(失败
  模式 #4 即活例)。

## 参考来源

- Linux 内核源码树(v6.6.78):`kernel/bpf/syscall.c`(分发与命令实体)、
  `kernel/bpf/btf.c`(解析与编号)。
- 正文对位:1.1 区 5(btf.c 分区)、4.1(切片器两步法)、排错①②
  (检索)、排错④(NOT FOUND 处置)、3.1(三层分工对照)。
- 模板同构:[`../../03-map-ops/trace-map-ops.sh`](../../03-map-ops/trace-map-ops.sh)
  (六站切片器,站 2 的「定义锚点 + 头部区间」写法)。
- 姊妹练习:[`../01-trace-freeze/`](../01-trace-freeze/)(入口分发的
  双命中辨析同款)。

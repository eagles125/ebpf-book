# 01-trace-freeze:追踪 BPF_MAP_FREEZE 完整调用链——自制「freeze 走读卡」

《第 5 章 eBPF 子系统内核代码结构》「课后扩展任务」练习 1 的任务目录。
正文 3.3 只展开了 freeze 的通用层收口,完整链要从入口分发走到底。本目录
没有可运行的产物:交付物是一张带行号标注的「freeze 走读卡」加一份仿
[`../../01-source-map/citations.txt`](../../01-source-map/citations.txt)
「文件|符号|说明」格式的锚点清单;`check-answer.sh` 是内嵌参考答案的机检
脚本(剧透——先自行走读再跑它对照)。本目录全程只读源码树:不构建、
不加载,对源码树零写入。

## 任务

沿 4.1 的切片器两步法独立走通 `BPF_MAP_FREEZE` 的完整调用链
(`SYSCALL_DEFINE3(bpf, → __sys_bpf → case BPF_MAP_FREEZE → map_freeze`),
产出带行号标注的「freeze 走读卡」——至少覆盖:

1. **入口分发行**:两个 `case BPF_MAP_FREEZE` 各在哪个 switch 里、哪个才是
   bpf(2) 命令的正身;
2. **`map_freeze` 全函数**:从签名到收尾,逐段标注行号(v6.6.78)与职责;
3. **三道并发闸的顺序**:互斥、写活动、幂等——先后次序及其理由;
4. **写命令撞 `-EPERM` 的拦截点**:冻结的生效机制在哪几行,update/delete/
   freeze 各撞在哪;
5. **失败出口总表**:按 2.6 errno 总表的格式整理本链全部失败出口;
6. **「少了什么」**:freeze 与 3.1 动作①(create)相比少了什么、为什么
   ——回调表与善后链两个角度。

教学目标:方法迁移——把切片器两步法从「跟着书走」变成「自己开链」;
顺带体会「不是所有命令都经 ops 回调」(3.3 的空白行对照面)。

验收标准:

- 走读卡每条行号都能用一条 `grep -n` 在走读树上复现(标注 v6.6.78);
- 锚点清单仿 citations.txt「文件|符号|说明」格式,机检 `FAIL=0` 收口——
  用本目录 `check-answer.sh` 对照参考答案,或把自建清单喂给
  [`../../scripts/verify-citations.sh`](../../scripts/verify-citations.sh);
- 「少了什么」的说明成文,回调表与善后链两个角度各自成立。

## 前置知识

- 2.6 errno 总表:`| errno | 拦截点 | 触发条件 |` 三列格式——本练习的
  失败出口表照此整理;
- 3.1 动作① create:对照组——一条「走全三层」的命令长什么样
  (查表 → alloc_check → alloc → 发 fd → 善后链);
- 3.2 写计数:update/delete 入口与出口的 inc/dec——「冻结为什么拦得住
  写」的另一半;
- 3.3 / 3.5:freeze 的通用层收口与 release 善后链——走读卡的底稿;
- 4.1 机检两步法与切片器两步法:动手手法;排错①②③:检索策略与 FAIL
  处置链。

## 提示

- 起点在一节区 1:`SYSCALL_DEFINE3(bpf,`(:5567)只是包一层
  `USER_BPFPTR` 转交;实际分发在 `__sys_bpf`(:5426)——三道前置工序
  (:5431/:5438/:5441)后 `switch (cmd)`(:5445)。
- `case BPF_MAP_FREEZE` 在 syscall.c 出现**两次**——用 `grep -n` 数命中,
  认准带 `err = map_freeze(&attr);` 的那处;第二处的归属与含义见分界线后。
- `map_freeze` 的闸有先后:fd 校验(空 fd、非映射对象)在权限闸之前,
  权限闸(`-EPERM`)在互斥闸之前,三道并发闸都在 `freeze_mutex` 内部——
  先到先拦,次序就是失败出口的优先级。
- 「不是所有命令都经 ops 回调」要数出来才算数:把 `map_freeze` 全函数里
  `map->ops->` 出现次数数一遍,再对照 3.1 的 `map_create` 数一遍——
  两个数字就是 3.3「空白行」的实证。
- 冻结的**生效点不在 `map_freeze` 本函数**:`map_get_sys_perms`
  (:784)发现 `map->frozen` 就剥 `FMODE_CAN_WRITE`(:791-792)——之后
  每一次写命令的权限检查才是拦截现场。

## 验证方式

- 机检:`bash check-answer.sh` 对内嵌参考答案逐条 `grep -F`,输出
  `TOTAL=<n> PASS=<n> FAIL=<n> WALK_TREE=<路径> TREE_VERSION=v6.6.78`,
  `FAIL=0`(退出码 0)即全绿;`WALK_TREE=<路径>` 换树。
- FAIL 处置:两类——文件不在树(查 `WALK_TREE` 指向与树版本)、符号未命中
  (回树 `grep -n` 重定位,修自己的锚点清单)——修清单不放水,处置链同
  排错③。
- 运行级(可选,需特权环境,本目录编写环境未执行):冻结的实际拦写效果
  要真机才能看到——冻结后 `bpftool map update` 预期报
  `Operation not permitted`(`-EPERM`)、有并发写时冻结预期 `-EBUSY`;
  本目录不执行任何 BPF 命令,此类输出一律按预期输出示例 / 运行级待验证
  对待(章级 README「验证状态约定」第三档)。

---

## 参考实现思路

**参考答案锚点清单**(`check-answer.sh` 内嵌符号表;行号为 v6.6.78 树
实测,`grep -n` 可复现;全部限 syscall.c——全链不经第二个文件,这就是
「自包含」的机检证据):

| 锚点符号(grep -F) | 实测行 | 说明 |
|---|---|---|
| `SYSCALL_DEFINE3(bpf,` | :5567 | bpf(2) 唯一真身:包 `USER_BPFPTR` 转交(:5569) |
| `__sys_bpf` | :5426 | 三道前置(:5431/:5438/:5441)后 `switch (cmd)`(:5445) |
| `case BPF_MAP_FREEZE` | :5461(第二处 :5590) | 分发支路 → `map_freeze(&attr)`(:5462) |
| `static int map_freeze(` | :1998 | 命令实体,函数体 :1998-2037 |
| `__bpf_map_get` | :1322(def),用 :2008 | fd 校验:`-EBADF`/:1325、`-EINVAL`/:1328 |
| `map_get_sys_perms` | :784(def),用 :2017 | 权限位合成,冻结剥写位 :791-792 |
| `FMODE_CAN_WRITE` | 用 :2017 | 可写权限位;update :1528、delete :1582、批命令 :4989 同源 |
| `freeze_mutex` | init :1249,用 :2022/:2034 | 并发闸①;mmap 侧 :893-926 共用 |
| `bpf_map_write_active` | :124(def),用 :2023 | 并发闸②;inc/dec 定义 :114/:119 |
| `frozen` | 读 :2027,写 :2032 | 并发闸③与最终置位;全局读点 :791 |

**三道并发闸的顺序**(为什么是这个次序):

1. 闸① `mutex_lock(&map->freeze_mutex)`(:2022):先串行化——冻结与
   mmap 冻结判定(:893-926)、以及并发 freeze 之间互斥,闸内判定才有
   「查了之后没人插队」的意义;
2. 闸② `bpf_map_write_active(map)`(:2023):再查写活动——`writecnt`
   原子计数非零即有写在进行(update/delete :1527/:1581 入口递增、
   :1558/:1612 出口递减),有写先拿 `-EBUSY`(:2024),不撕裂进行中的写;
3. 闸③ `READ_ONCE(map->frozen)`(:2027):幂等检查——已冻结再冻也是
   `-EBUSY`(:2028);`READ_ONCE`/`WRITE_ONCE`(:2032)配对,保证并发读
   `map_get_sys_perms`(:791)看到的是完整翻转。

**写命令撞 `-EPERM` 的拦截点**:冻结的全部「效果」就是 :2032 那一次
`WRITE_ONCE(map->frozen, true)`;生效靠 `map_get_sys_perms`(:784)——
`READ_ONCE(map->frozen)`(:791)为真就 `mode &= ~FMODE_CAN_WRITE`
(:792)。此后 update(:1528)、delete(:1582)、再 freeze(:2017)、
批命令(:4989)都撞 `-EPERM`;读(:1455 的 `FMODE_CAN_READ`)不受影响。
拦的不是「此刻的写」,是「之后的一切写」。

**失败出口总表**(按 2.6 格式;行号 v6.6.78 实测):

| errno | 拦截点 | 触发条件 |
|---|---|---|
| `-EFAULT` | :5438-5439 | 属性块拷贝失败——三道前置之一,全命令共享 |
| `-EINVAL` | :5559-5560;:2004-2005;:1326-1328 | 未知命令(`default`);`CHECK_ATTR(BPF_MAP_FREEZE)` 填充位非零;`map_fd` 指向非 bpf_map 对象(`f_op` 不符) |
| `-EBADF` | :1324-1325 | `map_fd` 为空 fd |
| `-ENOTSUPP` | :2012-2014 | `BPF_MAP_TYPE_STRUCT_OPS` 或带 btf_record 的映射不支持冻结 |
| `-EPERM` | :5441-5443(LSM);:2017-2019 | `security_bpf` 钩子拒绝;fd 无 `FMODE_CAN_WRITE`——冻结后 update(:1528)/delete(:1582)/再 freeze(:2017)均撞此 |
| `-EBUSY` | :2023-2025 | 写活动(`writecnt≠0`),不撕裂进行中的写 |
| `-EBUSY` | :2027-2029 | 已冻结再冻 |

**freeze 与 3.1 动作①相比少了什么**(两个角度):

- **回调表角度**:`map_create` 一路查表——`array_index_nospec`(:1165)
  防越界后 `bpf_map_types[]`(:1166)取 ops,再 `ops->map_alloc_check`
  (:1170)、`ops->map_alloc`(:1236);`map_freeze` 全函数
  :1998-2037 **零次 `map->ops->` 调用**——冻结是「从可写改只读」的共享
  通用属性,不触及任何随实现变化的存储算法,回调表只装后者(3.3/3.4:
  「对象私有语义走 ops,通用属性走通用层」)。
- **善后链角度**:`map_create` 的 fd 发放失败是唯一要善后的出口
  (:1305 `bpf_map_put_with_uref` 走释放链),因为它先建了对象、置了
  计数、领了编号;`map_freeze` 没有创建任何东西——每个失败出口的清理
  就是 `fdput(f)`(:2013/:2018/:2035),成功也只是翻一个标志位:没有
  编号要还、没有内存要放、没有 RCU 宽限期要等。**不建东西,就无善后**。

**第二处 `case BPF_MAP_FREEZE` 的归属**::5590 在
`BPF_CALL_3(bpf_sys_bpf)`(:5584)的命令白名单里——那是 BPF 程序经
`bpf_sys_bpf` 辅助函数发起调用的**准入清单**,不是 bpf(2) 的正身;放行
后同样汇入 `__sys_bpf`(:5600),只是指针来源换成内核态
(`KERNEL_BPFPTR`)。同一符号、两个 switch,`grep -n` 数命中是走读第一课。

## 调用链图

```text
SYSCALL_DEFINE3(bpf, ...)              syscall.c:5567   ← bpf(2) 唯一真身
  └─ __sys_bpf(cmd, USER_BPFPTR(uattr), size)      :5569 → :5426
       ├─ bpf_check_uarg_tail_zero                  :5431   多传字段须为零
       ├─ copy_from_bpfptr(&attr, uattr, size)      :5438   拷贝属性 → -EFAULT
       ├─ security_bpf(cmd, &attr, size)            :5441   LSM 留闸 → -EPERM
       └─ switch (cmd)                              :5445
            ├─ default: err = -EINVAL               :5559-5560
            └─ case BPF_MAP_FREEZE:                 :5461   ← 正身
                 └─ map_freeze(&attr)               :5462 → :1998
                      ├─ CHECK_ATTR(BPF_MAP_FREEZE)      :2004-2005  -EINVAL
                      ├─ fdget(ufd) → __bpf_map_get(f)   :2007-2008  -EBADF/:1325
                      │                                  ·           -EINVAL/:1328
                      ├─ STRUCT_OPS / 带 record 拒绝      :2012-2014  -ENOTSUPP
                      ├─ map_get_sys_perms & FMODE_CAN_WRITE  :2017-2019  -EPERM ← 权限闸
                      └─ mutex_lock(&map->freeze_mutex)  :2022       ← 并发闸①
                           ├─ bpf_map_write_active(map)  :2023-2025  -EBUSY ← 并发闸②
                           ├─ READ_ONCE(map->frozen)     :2027-2029  -EBUSY ← 并发闸③
                           └─ WRITE_ONCE(map->frozen, true)  :2032   唯一动作
                      (err_put: mutex_unlock :2034、fdput :2035、return :2036)
```

第二条支线(同一符号的第二命中):`BPF_CALL_3(bpf_sys_bpf)`(:5584)
白名单 `case BPF_MAP_FREEZE`(:5590)→ `__sys_bpf(cmd,
KERNEL_BPFPTR(attr), attr_size)`(:5600)——BPF 程序侧入口,殊途同归。

## 验证状态

- 机检级:`check-answer.sh` 已在本机走读树(v6.6.78)实跑:`TOTAL=10
  PASS=10 FAIL=0`,退出码 0;负路径 `WALK_TREE=/nonexistent` 实跑:十条
  锚点逐行 `FAIL 文件不在树`,`TOTAL=10 PASS=0 FAIL=10`,退出码 1(与
  头部注释的降级行为一致)。
- 运行级:本练习不执行任何 BPF 命令、不加载任何对象;冻结的实际效果
  (冻结后写命令 `-EPERM`、并发写中冻结 `-EBUSY`、已冻结再冻 `-EBUSY`)
  为源码推导的**预期输出示例 / 运行级待验证**,待特权环境以 `bpftool`
  实跑确认——本目录不出现任何「已验证通过」式运行级表述。
- 行号:全部树上实测(`grep -n`),随版本漂移;机检按符号不按行号
  (4.1),换树后行号以实测为准。

## 参考来源

- Linux 内核源码树(v6.6.78):`kernel/bpf/syscall.c`(本练习唯一涉及
  文件);`include/uapi/linux/bpf.h`(`BPF_MAP_FREEZE` 命令字与
  `union bpf_attr` 契约)。
- 正文对位:2.6(errno 总表格式)、3.1(create 对照)、3.2(写计数)、
  3.3(freeze 收口与「空白行」)、3.5(善后链)、4.1(两步法)、
  排错③(FAIL 处置)。
- 本目录机检口径同源:[`../../scripts/verify-citations.sh`](../../scripts/verify-citations.sh)。

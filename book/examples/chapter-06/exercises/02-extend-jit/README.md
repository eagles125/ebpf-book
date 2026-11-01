# 练习② 扩展切片器:给 x86 JIT 链加「致盲细节」站

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己完成再看 `trace-blind-station.sh` 与 `check-trace.sh`。

## 任务

仿照 [`../../02-jit-x86/trace-jit-x86.sh`](../../02-jit-x86/trace-jit-x86.sh) 的接口与结构,写一个**独立**小脚本 `trace-blind-station.sh`,对走读树 `kernel/bpf/core.c` 切出常量致盲的两站:

- **站 1**:`bpf_jit_blind_insn`(定义 `core.c:1250`,v6.6.78)——签名头 + 改写示例区间(AX 保护、零值特例、ALU 立即数「随机异或对」);
- **站 2**:`bpf_jit_blind_constants`(定义 `core.c:1428`)——遍历入口:请求门、克隆、循环改写。

接口同款:`bash trace-blind-station.sh [WALK_TREE]`,每站「定位(grep -n 实测)+ 代码段(sed 区间,≤25 行)+ 职责注」,末尾汇总 `TREE_VERSION=v6.6.78 STATIONS=2`;锚点漂移时打 `STATION n: NOT FOUND` 并 exit 1。只读源码树,不构建、不加载。

## 提示

- 这两个锚点是正文二节站六**已公开**的内容(正文二节站六贴的就是 :1279-1300 改写示例)——先把切片器骨架抄过来,再改锚点与区间,是合法且推荐的做法;
- 从定义行(1250)到改写示例(1279 起)中间隔着 AX 约束注释块——锚点行 + 偏移的两步法在这里正好用上(偏移量自己数);
- 每站职责注一句话讲清「这一段在致盲流程里干什么」,与正文二节站六呼应。

## 验收

```bash
bash check-trace.sh
# 预期:TOTAL=8 PASS=8 FAIL=0,exit 0
# (存在/语法/实跑退出码 + 两站标题 + 两锚点 + 汇总行,共 8 项断言)
```

`check-trace.sh` 先 `bash -n` 检查语法,再实跑 `trace-blind-station.sh`,断言输出含两站标题、两个锚点符号与 `STATIONS=2` 汇总。

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己写完脚本</summary>

- 骨架照抄 `02-jit-x86` 切片器:入参/依赖/版本软校验/`locate`/`show_locate`/`seg`/`station_fail` 全部复用,只留两站;
- 站 1 用两个代码段:定义头(锚点 `static int bpf_jit_blind_insn(const struct bpf_insn *from,` +0..+9)与改写示例(同锚点 +29..+50,即 :1279-1300——AX 保护、`imm == 0` 特例转 `xor`、ALU 族 `mov AX, imm_rnd ^ imm; xor AX, imm_rnd; op dst, AX`);
- 站 2 一个代码段:锚点 `struct bpf_prog *bpf_jit_blind_constants(struct bpf_prog *prog)` +0..+24(:1428-1452,门/克隆/循环头);
- 职责注口径:站 1「单条改写:AX 用户不可见,零值走 xor,其余立即数拆随机异或对」;站 2「遍历入口:未请求/已致盲原样返回,克隆后逐条改写,失败 ERR_PTR 回退」;
- 自查三件事:`bash -n`、`WALK_TREE=/nonexistent` 负路径 exit 1、与正文二节站六的贴码逐字一致。

</details>

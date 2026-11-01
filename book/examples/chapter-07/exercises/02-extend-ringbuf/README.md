# 练习② ringbuf 切片扩展:亲手切出两段式提交的现场

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己完成再看 `trace-ringbuf.sh` 与 `check-ringbuf.sh`。

## 任务

仿照 [`../../05-map-internals/trace-map-internals.sh`](../../05-map-internals/trace-map-internals.sh) 的接口与结构,写一个**独立**小脚本 `trace-ringbuf.sh`,对走读树的 `kernel/bpf/ringbuf.c` 切出环形缓冲区两段式提交的两站:

- **站 1**:`__bpf_ringbuf_reserve`(定义 `:408`,v6.6.78)——预留路径:单条上限、NMI trylock、pending 扫描;
- **站 2**:`bpf_ringbuf_commit`(`:489`)与就绪唤醒 `bpf_ringbuf_notify`(irq_work,`:150`)——提交/丢弃与「消费者被叫醒」的机制。

接口同款:`bash trace-ringbuf.sh [WALK_TREE]`,每站「定位 + 代码段(≤25 行)+ 职责注」,汇总 `TREE_VERSION=v6.6.78 STATIONS=2`;锚点漂移打 `STATION n: NOT FOUND` 并 exit 1。只读源码树,不构建、不加载。

## 提示

- 正文四节 4.2 贴过 :408-432(预留)——站 1 的区间直接对齐正文;站 2 的 commit(:489)很短,配 notify(:150)凑一站,正好回答「submit 之后消费者怎么知道」;
- 站 2 职责注要点:commit 只推进 producer_pos(原子释放)、discard 走同一路径带 discard 标记、notify 经 irq_work 在安全上下文唤醒轮询者;
- `check-ringbuf.sh` 的断言口径照抄 `../../exercises/01-map-op-chain/check-chain.sh` 的骨架(存在/语法/实跑退出码/锚点/汇总),把断言对象换成两站锚点。

## 验收

```bash
bash check-ringbuf.sh
# 预期:TOTAL=7 PASS=7 FAIL=0,exit 0
# (存在/语法/实跑退出码 + 两站标题 + 两锚点 + 汇总行)
```

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己写完脚本</summary>

- 骨架照抄 05-map-internals 切片器:入参/依赖/版本软校验/locate/show_locate/seg/station_fail 全复用,只留两站、关键文件换成 kernel/bpf/ringbuf.c;
- 站 1:锚点 `static void *__bpf_ringbuf_reserve(struct bpf_ringbuf *rb, u64 size)`,区间 +0..+24(:408-432,与正文四节 4.2 的贴码一致);
- 站 2:两个代码段——commit 锚点 `static void bpf_ringbuf_commit(void *sample, u64 flags, bool discard)` +0..+8(签名与 hdr→len 分支),notify 锚点 `static void bpf_ringbuf_notify(struct irq_work *work)` +0..+7(irq_work 回调体);
- 职责注:站 1「预留:占位不发布,pending 扫描跳过未提交记录,NMI 用 trylock 宁丢不等」;站 2「提交/丢弃:推进 producer_pos 原子发布;notify 经 irq_work 把轮询者叫醒」;
- 自查三件事:`bash -n`、`WALK_TREE=/nonexistent` 负路径 exit 1、与正文四节 4.2 贴码逐字一致。

</details>

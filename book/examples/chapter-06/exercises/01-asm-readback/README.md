# 练习① 字节码回读:亲手产出 source↔bytecode 对照

> 防剧透结构:本 README 前半只给**任务与提示**,请先自己完成再看 [`answer.md`](answer.md) 与 `check-answer.sh` 的断言口径。

## 任务

对 [`../../01-bpf-asm/`](../../01-bpf-asm/) 的锚点程序 `xdp_count.bpf.c`,产出**你自己的**逐行 source↔bytecode 对照表:

1. `make` 编译出 `xdp_count.bpf.o`(与第 6 章 4.2 节同一命令);
2. `./disasm.sh` 反汇编,得到 14 条指令;
3. 把每条指令对回 `.c` 源码行:哪条对应哪个语句?为什么有些语句「没有指令」?为什么返回值是 0/1/2?
4. 把你的表与 `answer.md` 交叉核对——**先对条数,再对顺序,最后逐字对反汇编文本**。

## 提示

- 从 `.c` 行号与 llvm-objdump -d 输出对照入手:源码 20/21 行是两次 ctx 读,24 行是「复制指针→加 14→比较跳转」三条;
- `enum xdp_action` 里 XDP_ABORTED=0、XDP_DROP=1、XDP_PASS=2(include/uapi/linux/bpf.h,v6.6.78)——三个返回值来源即此;
- 机器码列不要抄答案:自己从反汇编输出逐条摘录,再与 `answer.md` 比对逐字一致才算过。

## 验收

```bash
bash check-answer.sh
# 预期:TOTAL=5 PASS=5 FAIL=0,exit 0
```

脚本内部先自动 `make`(自足性:仓库里没有 .bpf.o 也能跑),再对反汇编输出断言五个特征序列(逐条 PASS/FAIL,汇总行同机检口径)。

---

## 参考思路(防剧透线以下,完成前勿看)

<details>
<summary>点开前请先自己画完表格</summary>

思路要点(完整表见 [`answer.md`](answer.md)):

- 指令 0(`r0 = 0x0`)是编译器**预置**的默认返回值 XDP_ABORTED——异常路径直接带着它走向出口,这是「出错快路径」的写法;
- 指令 1/2 与源码 21/20 行**逆序对应**(data_end 先装),寄存器分配的顺序不承诺跟源码一致;
- 指令 3/4/5 是源码 24 行一行的展开:`eth + 1` 编译成「复制指针 + 加 14(sizeof(struct ethhdr))+ 无符号比较跳转」;
- 指令 6–9 是 `h_proto` 的两个字节按小端拼装;`__builtin_bswap16(0x86dd)` 在**编译期**折算成常量 `0xdd86`,运行期没有 bswap 指令;
- 指令 10(`r0 = 0x1`)先备好 XDP_DROP,指令 11 的比较决定是否带着它去出口;非 IPv6 落到指令 12 覆盖为 XDP_PASS;
- `LBB0_3` 处的 `exit` 是三条返回路径的**共享出口块**——clang 的 CFG 单一出口优化;
- `LICENSE` 在独立 license 段,`llvm-objdump -d` 只反汇编可执行段,所以不见它。

</details>

# 03-jit-arm64:arm64 JIT 四站切片器

沿「编译入口 → build_body 指令分派 → bpf2a64 寄存器映射 → 与 x86 差异点」的路径,把走读树里 arm64 JIT 的四段关键代码按「站」切出来。正文《三、arm64:另一套后端的同构与差异》逐站引用本输出的行号与代码段;每站职责注都带「与 x86 对照」,方便把两套后端并排读。

- 走读树:`/work/linux/linux-6.6.78`(v6.6.78)。**只读源码树,不构建、不加载**——脚本只做 `grep -n` 定位与 `sed -n 'A,Bp'` 区间提取,对源码树零写入。
- 站点地图:站 1 编译入口(与 x86 同构);站 2 build_body 逐条分派(相当于 x86 的 do_jit);站 3 bpf2a64 映射表(与 x86 reg2hex 对照);站 4 差异点(MOVZ/MOVK 立即数序列与显式 icache 刷新)。

## 用法

```bash
# 默认走读树
bash trace-jit-arm64.sh
# 指定走读树(位置参数优先;也兼容环境变量 WALK_TREE)
bash trace-jit-arm64.sh /path/to/linux-6.6.78
```

## 预期输出示例(本机实跑摘录,首尾各 10 行)

```
# trace-jit-arm64.sh —— arm64 JIT 四站切片(第 6 章源码走读)
# 走读树:WALK_TREE=/work/linux/linux-6.6.78
# 声明:只读源码树(bpf_jit_comp.c/bpf_jit.h),不构建、不加载,对源码树零写入。
# 版本:行号按 v6.6.78 校准;TREE_VERSION 为书稿锁定常量,非自动探测。

## 站 1:编译入口 bpf_int_jit_compile
### 定位(grep -n 实测;v6.6.78 校准)
1518:struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
### 代码段:sed -n '1518,1540p' arch/arm64/net/bpf_jit_comp.c —— 编译入口:jit_requested 门 + 常量致盲预处理(与 x86 站 1③ 同构)
struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
```

```
		bpf_jit_dump(prog->len, prog_size, 2, ctx.image);

	bpf_flush_icache(header, ctx.image + ctx.idx);
职责:与 x86 对照——x86 的 64 位立即数靠 emit_mov_imm64(单条 movabs imm32 拼 32/64 位),arm64 必须用 MOVZ/MOVK 序列按 16 位分块;arm64 写完映像必须显式 flush icache,x86 是一致的缓存体系无此步。

== 汇总 ==
WALK_TREE=/work/linux/linux-6.6.78
TREE_VERSION=v6.6.78 STATIONS=4
```

完整实跑为四站、exit 0、无 `STATION n: NOT FOUND`。行号用「锚点 + 偏移」两步法:树小幅漂移时区间随锚点自动平移。输出结构与失败模式与 `../02-jit-x86/`(同款接口,六站)完全一致,此处不再重复——树不在/锚点漂移/版本不符三类情况的处理请看 `../02-jit-x86/README.md` 的失败模式表。

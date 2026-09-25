# 练习 04：按端口限速（令牌桶）

第 17 章《实战：构建高性能 eBPF 防火墙》课后扩展任务 4（数据面）：以阶段 2 的解析骨架为基线，用令牌桶算法实现按目的端口的报文速率限制。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

复用阶段 2 的解析与统计骨架（省略 ACL 映射与判定——本练习的策略只有限速），实现按目的端口限速：

1. 新增两类映射：**限速配置**（loader 用 `--limit` 下发）与**令牌桶状态**（数据面运行时维护）；key 都取目的端口。
2. 数据面在解析成功后按目的端口做令牌桶判定：令牌充足则放行并扣减，**超额报文 `XDP_DROP`** 并计入统计。
3. loader 新增 `--limit <port>,<pps>[,<burst>]`（可重复；`burst` 缺省 = `pps`，即 1 秒突发额度）。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] `--limit 5201,2000` 下发后，目的端口 5201 的报文速率被压到约 2000 pps（短暂突发不超桶容量 2000）。
- [ ] 未配置的端口不受影响：报文全部放行（`pass`）。
- [ ] 超额报文被丢弃并计入 `drop_rl` 展示项；`total` = `pass` + `drop_rl`（解析成功的报文中两者互斥）。
- [ ] 非法输入在参数阶段被拒绝：端口 >65535、字段数不足、pps/burst 超大量级等均报错退出。

## 提示

只给「会用到什么」，不给写法：

- 两类映射各选什么类型？配置由 loader 一次性下发（回想阶段 2 的 `acl_rules`）；状态每端口一份、由数据面持续更新，条目数有上界、旧条目可自动回收——回想阶段 3 的 `conntrack` 用了什么类型。
- key 只要一个目的端口就够了：报文里的端口是什么字节序？loader 下发时怎么对齐这个字节序？
- 令牌桶状态至少需要两个量：剩余额度与上次补充时间；BPF 里**不能用浮点**，那「不到 1 个报文」的零头怎么表示？
- 时间源用 `bpf_ktime_get_ns`；补令牌要算 `elapsed * rate / 1e9` 这样的乘除——verifier 对除法有什么约束？乘除换成乘移位行不行？
- 状态条目第一次遇到时还不存在：怎么初始化、首包放不放行？
- loader 侧结构体镜像、`bpf_map_update_elem` 下发、attach XDP 的写法与阶段 2 loader 同款。

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`gcc`、`bpftool`、`libbpf-dev`、`make`（可用 `../../00-env/check-env.sh` 自检）。
- 运行 loader 需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`；实验拓扑由 `../../scripts/setup-veth.sh` 创建，XDP 挂载点为拓扑中的 `veth-fw-c`；压测另需 `iperf3`（编译不依赖）。
- 安全警告：限速会直接丢包，**只挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**，否则可能立即失联。

```bash
cd book/examples/chapter-17/exercises/04-port-rate-limit
make
```

运行演示（需要 root；先建立拓扑）：

```bash
sudo ../../scripts/setup-veth.sh

# 终端 A：ns-fw 里起 iperf3 UDP 服务端
sudo ip netns exec ns-fw iperf3 -s -u -p 5201

# 终端 B：下发限速（5201 端口 2000 pps，突发容量缺省 2000）并挂载
sudo ./ratelimit -i veth-fw-c --limit 5201,2000

# 终端 C：客户端不限带宽 UDP 打流（-b 0 即尽力发送），观察限速效果
sudo ip netns exec ns-client iperf3 -u -c 10.0.0.1 -p 5201 -b 0 -t 30
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

下发 `--limit 5201,2000` 后，客户端 `iperf3 -u -c 10.0.0.1 -p 5201 -b 0` 尽力打流：

```text
已下发限速配置 1 条（pps=0 表示不限速）。
XDP 程序已挂载到 veth-fw-c（ifindex=5），Ctrl-C 退出。
total=0 pass=0 drop_rl=0 drop_parse=0
total=147520 pass=4020 drop_rl=143500 drop_parse=0
total=295180 pass=8025 drop_rl=287155 drop_parse=0
```

- `pass` 每 2 秒约增长 4000——即限速阈值 2000 pps 的 2 秒份额：令牌桶把 5201 端口的
  放行速率压在阈值附近；`drop_rl` 承接其余超额报文，`total` ≈ `pass` + `drop_rl`。
- iperf3 客户端侧会报告大量 UDP 丢包（`dropped` 计数），与服务端收到的速率对上即闭环。
- 对照实验：去掉 `--limit`（只写 `-i`）时 `total` 与 `pass` 同步增长、`drop_rl` 恒 0——
  未配置端口不限速；下发 `--limit 5201,0`（pps=0）效果相同，即配置里「0 = 不限速」的语义。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`fw_common.h`/`ratelimit.bpf.c`/`ratelimit.c`），建议自行实现后再对照：

1. **两类映射的选型**：`rate_cfg`（hash，key `__u16` 目的端口，value `rl_cfg{rate_pps, burst}`）由 loader 下发，语义与阶段 2 的 `acl_rules` 同类；`rate_state`（**LRU** hash，value `rl_value{tokens, last_ns}`）由数据面运行时维护。状态条目按端口惰性创建，条目数有上界（与 `RL_MAX_PORTS = 256` 同上限），选 LRU 是兜底淘汰语义，与阶段 3 `conntrack` 的选型理由一致。
2. **令牌桶定点数**：BPF 里不能用浮点，令牌余量用 Q8.10 定点整数表示——**1 个报文额度 = 1 << 10 = 1024**。初始化/封顶用 `burst << 10`，扣减用 `tokens -= (1 << 10)`，`tokens < (1 << 10)` 即不足一个额度。定点化让「半打满的桶」可以被精确记账，只靠整数也能平滑限速。
3. **elapsed 折算（移位代替除法）**：每次报文到达按经过时间补令牌，精确式是
   `tokens(Q8.10) += elapsed_ns * rate_pps / 1e9 * 1024`。参考实现写成 `(elapsed * rate) >> 20`——
   `2^20 = 1048576 ≈ 1e9 / 1024 = 976562.5`，用乘移位代替乘除。两点代价与理由：
   - 除法会引入「除数非零」的 verifier 约束与除零路径，移位没有；
   - 该近似系统性**略少补**（约 7%，限得更紧一点，方向保守），教学可接受。
   量级上 `elapsed ≤ 1e11`（约 100s）、`rate ≤ 1e7` 时乘积 `≤ 1e18` 小于 u64 上限，不会溢出；
   超长空闲后首包的少量少补由 `burst` 封顶掩盖。补完即封顶（`tokens > burst << 10` 拉回），
   再更新 `last_ns`。
4. **判定与懒初始化**：`rate_cfg` 查不到该端口或 `rate_pps == 0` 直接放行（未配置不限速）；
   `rate_state` 查不到则写入初始状态（满桶 `burst << 10`、`last_ns = now`）并放行首包；
   之后每次到达先补令牌再扣减，扣不动就 `XDP_DROP`。
5. **多 CPU 写竞争近似**：`rate_state` 是普通（非 per-CPU）LRU hash，多 CPU 同时处理同端口的
   报文时，对 `tokens`/`last_ns` 的读-改-写没有加锁，可能互相覆盖、少量多放行——与阶段 3
   conntrack 计量的「多 CPU 写竞争近似」同类，教学可接受；生产化可用 per-CPU 预聚合或自旋锁。
6. **key 字节序**：限速 key 只有端口一个字段，仍遵守「原样存原样查」契约——数据面从报文里取的
   `dst_port` 是网络字节序，loader 侧必须 `htons` 后再写 `rate_cfg`，否则规则永远查不中。
7. **统计口径**：省略 ACL 映射与判定，骨架其余（解析、per-CPU 计数）与阶段 2 逐字一致；限速超额
   复用 C1 的 `STAT_DROP_ACL` 下标（顺序契约不动、不新增下标），loader 展示名按本练习语义记为
   `drop_rl`。已知边界：解析骨架是 IPv4/TCP/UDP（与阶段 2 相同），IPv6 报文按 PASS 处理；
   限速粒度是**报文数（pps）**而非字节数，按带宽限速需让令牌按报文长度加权折算，留作进阶方向。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3）`make` 通过，并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- `tc-tbf(8)`：Linux 内核令牌桶限速器（令牌桶、速率与突发容量的经典表述，本练习算法的教学参照）。
- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，LRU hash 的容量与淘汰语义）。
- Linux kernel documentation：BPF helper `bpf_ktime_get_ns`（`CLOCK_MONOTONIC` 时间基准）。
- libbpf 文档：`bpf_map_lookup_elem`/`bpf_map_update_elem` 与 skeleton 加载模式（与阶段 2 同源）。
- iperf3 用户文档：UDP 压测与 `-b` 带宽参数（压测方法学与第 17 章正文口径一致）。
- 本章阶段 2 示例 `book/examples/chapter-17/02-parse-acl/`：解析与统计骨架的同源基线。

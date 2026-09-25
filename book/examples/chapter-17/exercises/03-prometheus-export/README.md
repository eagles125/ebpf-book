# 练习 03：Prometheus 指标导出

第 17 章《实战：构建高性能 eBPF 防火墙》课后扩展任务 3（控制面）：以阶段 5（`../../05-firewall/`）的 Go 控制面 `fwctl` 为基线，把 per-CPU 统计聚合暴露为 Prometheus 抓取端点——标准库 `net/http` 实现，不引入第三方依赖。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在阶段 5 控制面（`user/` 四文件：子命令分发、map 镜像与 pin、规则下发、统计聚合）的基础上新增指标导出：

1. 新增 `export --listen <地址>` 子命令（缺省 `:9100`，**阻塞运行**）：监听 HTTP，`GET /metrics` 输出 Prometheus 文本格式。
2. 指标 `fw17_packets_total{reason="..."}`：8 个计数器与 `fwctl stats` 完全同口径——`reason` label 取值固定为 `statNames` 的 8 项（`total/pass/drop_acl/drop_parse/ct_new/ct_established/ct_full/nat`，C1 契约顺序）。
3. 约束：只依赖标准库与既有的 cilium/ebpf（不引第三方 client 库）；`stats.go` 不改动（复用其 `statNames` 与 `SumPerCPU`）。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] `export` 运行期间 `curl :9100/metrics` 返回文本：`# HELP`/`# TYPE` 两行注释 + 8 行 `fw17_packets_total{reason="..."} <值>`。
- [ ] 同一时刻 `fwctl stats` 与 `/metrics` 的 8 个值一致（同一张 map、同一套聚合）。
- [ ] 导出运行期间数据面计数不受影响（导出只读 map，iperf3 打流时计数照常增长）。
- [ ] `go.mod` 中除 `cilium/ebpf` 及其间接依赖外无新增依赖。

## 提示

只给「会用到什么」，不给写法：

- Prometheus 文本格式的最小合法形态：两行注释（`# HELP <名> <说明>`、`# TYPE <名> counter`）+ 每个样本一行 `<名>{<label>="<值>"} <数值>`。`Content-Type` 用 `text/plain; version=0.0.4`。
- `stats` 是 per-CPU 数组：一次 Lookup 返回几份副本、怎么合并？`stats.go` 里现成的 `SumPerCPU` 就是答案，别重写。
- `reason` 的 8 个取值直接来自 `statNames`（C1 契约顺序）：为什么 label 取值集合必须固定，而不能按五元组/接口名之类动态生成？
- HTTP 服务端最小骨架是标准库 `net/http` 的哪两个调用？哪个是阻塞的？handler 拿到的是不是每次请求都新执行一遍？
- 「导出频率低（抓取间隔秒级）vs 数据面纳秒级自增」——值得为它起一个后台 goroutine 定时预聚合吗？想清楚再决定聚合发生在哪一侧。
- HTTP handler 里 map 读取失败怎么办？一个 HTTP 请求的错误该用什么状态码回给抓取端？

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`bpftool`、`make`、Go 1.22+（本仓库编写环境为 Go 1.27.1）；Go 依赖已提交 `go.sum`，首次构建如提示缺失执行 `cd user && go mod tidy`（离线环境按「依赖待拉取，编译级待验证」处理）。
- `tc`（iproute2，运行期挂 TC filter 用）；运行需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`。
- 实验拓扑由 `../../scripts/setup-veth.sh` 创建；pin 目录与阶段 5 相同（`/sys/fs/bpf/fw17`）；`:9100` 端口需在容器/主机放行。
- 安全警告：与阶段 5 相同——本程序会真实丢包并改写 NAT 字段，**只应挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**；`:9100` 是纯文本无认证端点，只应在实验网络内暴露。

```bash
cd book/examples/chapter-17/exercises/03-prometheus-export
make    # 首次如提示缺依赖：cd user && go mod tidy 后重试
```

运行演示（需要 root；attach 后起导出，另一终端 curl 观测）：

```bash
sudo ../../scripts/setup-veth.sh

# 1) 加载并挂载（同阶段 5 的 attach 口径）
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s

# 2) 阻塞启动指标导出（Ctrl-C 退出，attach 的挂载不受影响）
sudo ./fwctl export --listen :9100

# 3) 建流（另开终端）：ns-server 起 iperf3 服务端、ns-client 打流
sudo ip netns exec ns-server iperf3 -s -p 5201 &
sudo ip netns exec ns-client iperf3 -c 10.0.2.2 -p 5201 -t 30

# 4) 抓取与核对（再开一个终端）
curl -s http://localhost:9100/metrics
sudo ./fwctl stats    # 与 /metrics 的 8 个值核对
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

`curl -s http://localhost:9100/metrics`（iperf3 打流片刻后）：

```text
# HELP fw17_packets_total firewall packet counters.
# TYPE fw17_packets_total counter
fw17_packets_total{reason="total"} 18734
fw17_packets_total{reason="pass"} 18734
fw17_packets_total{reason="drop_acl"} 0
fw17_packets_total{reason="drop_parse"} 0
fw17_packets_total{reason="ct_new"} 1
fw17_packets_total{reason="ct_established"} 0
fw17_packets_total{reason="ct_full"} 0
fw17_packets_total{reason="nat"} 0
```

- 演示未下发任何规则与 NAT：报文走「ACL 未命中 → 放行」的慢路径，故 `total` = `pass`、
  `drop_acl` 恒 0；`nat` 只计实际改写成功的报文，未配 NAT 即为 0。若按阶段 5 的演示
  叠加 DNAT/SNAT 规则再打流，`nat` 项会按每报文改写次数递增（口径见阶段 5 README）。

- 8 行样本与 `fwctl stats` 输出逐项一致（同一张 pin 的 `stats` map、同一套 `SumPerCPU` 聚合）；`total`/`pass`/`nat` 的口径含义见阶段 5 README 的「预期输出」一节。
- 计数器是**自 map 创建以来的累计值**（counter 语义）：Prometheus 侧用 `rate(fw17_packets_total[1m])` 之类函数算增量速率，而不是直接看绝对值。
- 导出期间在另一终端反复 curl，iperf3 吞吐不受影响——导出只读 map，不参与数据面路径。

---

## 参考实现思路

以下思路对应本目录内的参考实现（`user/exporter.go` + `user/main.go` 的 `export` 子命令，其余文件与阶段 5 一致，`stats.go` 未改动），建议自行实现后再对照：

1. **最小文本格式（`metricsHandler`）**：每个请求现场拼接文本——`# HELP`/`# TYPE` 两行头 + 循环 `statNames` 的 8 个下标逐个 `SumPerCPU` 聚合、`fmt.Fprintf(..., "fw17_packets_total{reason=%q} %d\n", ...)` 输出一行样本。`Content-Type: text/plain; version=0.0.4` 是 Prometheus 文本格式的约定类型。任一下标读取失败直接 `http.Error` 回 500——map 读取是唯一可能出错的环节（如 pin 被外部删除），此时宁可让本次抓取失败，也不能输出缺项的半份指标（监控系统会区分「抓取失败」与「计数为零」，后者是危险的假信号）。
2. **per-CPU 聚合放请求时做**：`stats` 是 per-CPU 数组，每 CPU 一份独立 `__u64`；`SumPerCPU` 一次 Lookup 返回 `nr_cpus` 份并求和。聚合发生在每次 `/metrics` 请求里而不是后台 goroutine——理由是频率不对称：抓取间隔是秒级（Prometheus 默认 15s–1m），数据面自增是纳秒级，预聚合只能把「秒级的陈旧值」提前算好，既不更准也不更省（读 8 个下标的开销对秒级抓取可忽略）。教学实现选择「请求时聚合」这个最简单的正确形态。
3. **label 取值固定为 C1 契约（`statNames`）**：`reason` 只有 8 个固定取值，与 `fwctl stats` 的展示顺序逐字一致。刻意的反面是动态 label——若按五元组/连接条目生成 label，样本基数随流量无上界增长（时序数据库的「高基数」问题：每个新五元组都是一个新时序），监控端反而被拖垮。固定 8 项意味着 Prometheus 的时间线数量恒定，`fw17_packets_total` 永远只有 8 条。
4. **`ServeMetrics` 与 CLI 装配**：`http.HandleFunc("/metrics", metricsHandler(stats))` 注册 handler，`http.ListenAndServe(listen, nil)` 阻塞运行（默认 `:9100`）。`export` 子命令先经 `openMap` 打开 pin 的 `stats` map 再传入——与 `stats` 子命令完全同源，只是把「打印到终端」换成「暴露给 HTTP」；进程被 Ctrl-C 退出不影响 attach 的挂载与 pin（数据面照常工作，重新运行 `export` 即恢复导出）。
5. **已知边界**：单端点、无 TLS/认证（实验网络内使用；生产应放网关/采集器后面）；导出的 `conntrack` 相关计数只有 4 个汇总下标，连接明细仍走 `conntrack list`（明细不适合塞进指标 label，理由同第 3 点）；计数器从 attach 创建 map 起累计，detach 后归零重建——监控侧把它视为 counter 重置即可。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3 / Go 1.27.1）`make` 通过（两个 BPF 对象 + `go build` + `go vet`），并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- Prometheus documentation：Exposition Formats（text format 0.0.4 的 `# HELP`/`# TYPE` 语法与 `text/plain; version=0.0.4`）、Writing client libraries（counter 语义、`rate()` 与 label 命名）。
- Prometheus documentation：Instrumentation 一节对 cardinality（label 基数）爆炸的告诫（固定 label 取值集合的依据）。
- `net/http` 文档：`HandleFunc`/`ListenAndServe`（标准库即可满足的最小抓取端点）。
- Linux kernel documentation：BPF 映射（`Documentation/bpf/maps.rst`，per-CPU 数组映射的用户态语义：一次 lookup 返回每 CPU 一份副本）。
- cilium/ebpf 文档与源码（v0.16.0）：per-CPU map 的 Lookup 聚合形态（`SumPerCPU` 的依据）。
- 本章阶段 5 示例 `book/examples/chapter-17/05-firewall/`：本练习的基线代码，`stats` 计数口径（`total/pass/drop_acl/...`）的出处。

# 练习 02：规则持久化与重启恢复

第 17 章《实战：构建高性能 eBPF 防火墙》课后扩展任务 2（控制面）：以阶段 5（`../../05-firewall/`）的 Go 控制面 `fwctl` 为基线，为精确五元组规则（`acl_rules`）增加 JSON 持久化——保存、加载与原子替换。

> 防剧透结构：任务与提示在前；`---` 分界线之后才是参考实现思路。建议先只读前半部分、自行实现，再对照后半部分。

## 任务

在阶段 5 控制面（`user/` 四文件：子命令分发、map 镜像与 pin、规则下发、统计聚合）的基础上新增规则持久化：

1. 新增 `rules save --file <路径>` 与 `rules load --file <路径>` 两个子命令：`save` 把 `acl_rules` 全量导出为 JSON 文件；`load` 从 JSON 文件逐条恢复规则。
2. **原子替换**两点口径：(a) `save` 先写临时文件再 `rename`，规则文件要么是完整旧版、要么是完整新版，绝不出现半截文件；(b) `load` 逐条 Put、**不清空重建**——恢复过程中不存在「先清空后下发」的瞬时全放行窗口，已生效的规则保持生效。
3. 提供 `rules.example.json` 示例文件（一 allow 一 drop 两条），字段与 `rule add` 的 CLI 参数一一对应（`proto/src/sport/dst/dport/action`）。

验收标准：

- [ ] `make` 编译通过（本练习计入 `../../scripts/verify-build.sh` 的练习编译项）。
- [ ] 下发若干规则后 `rules save`，`detach` 后重新 `attach`（规则表已空），再 `rules load`：`rule list` 输出与保存前一致。
- [ ] `rules load` 可重复执行（同 key 覆盖，恢复以文件为准），不报冲突错误。
- [ ] 非法输入在加载阶段被拒绝并带序号定位：JSON 语法错、第 N 条字段非法（协议/地址/端口/动作）均报「第 N 条」错误。
- [ ] `rules save` 中途被 `kill -9` 不产生半截规则文件（最多残留一个 `.tmp` 临时文件，旧文件完好）。

## 提示

只给「会用到什么」，不给写法：

- 导出要遍历整张 hash map：回想 `rule list` 是怎么遍历 `acl_rules` 的（cilium/ebpf v0.16.0 的迭代 API 形态，循环结束后还要检查什么）。
- map 里的地址/端口是网络字节序原样存储，落成 JSON 前要转回点分十进制与主机字节序：`rules.go` 末尾的共享小工具（`int2ip`/`be16`/`protoName`）已经够用，不必重写。
- 「先写临时文件再 rename」为什么能避免半截文件？想一想 `rename(2)` 对已存在目标路径的替换是不是原子的；临时文件的权限位该给多少。
- 恢复条目的合法性校验不必另写一套：`rule add` 用 `ParseRule` 做了协议/地址/端口/动作的全部检查，参数类型怎么对上（JSON 里端口是数字，`ParseRule` 收字符串）？
- JSON 编解码用标准库 `encoding/json` 即可：struct tag 控制字段名，`MarshalIndent` 让文件可读。
- `rules` 子命令有两层参数（子子命令 save/load + `--file`）：`flag.Parse` 在第一个非 flag 参数处停止——回想 `rule add` 是怎么用「两趟 Parse」解决这个问题的。

## 环境前提与构建

- Linux kernel 5.10+，开启 `CONFIG_DEBUG_INFO_BTF`。
- `clang`、`bpftool`、`make`、Go 1.22+（本仓库编写环境为 Go 1.27.1）；Go 依赖已提交 `go.sum`，首次构建如提示缺失执行 `cd user && go mod tidy`（离线环境按「依赖待拉取，编译级待验证」处理）。
- `tc`（iproute2，运行期挂 TC filter 用）；运行需要 root 或 `CAP_BPF` + `CAP_NET_ADMIN`。
- 实验拓扑由 `../../scripts/setup-veth.sh` 创建；pin 目录与阶段 5 相同（`/sys/fs/bpf/fw17`）。
- 安全警告：与阶段 5 相同——本程序会真实丢包并改写 NAT 字段，**只应挂载到实验拓扑创建的 veth 接口，绝不要挂到物理网卡或 SSH 正在使用的接口**。

```bash
cd book/examples/chapter-17/exercises/02-rule-persistence
make    # 首次如提示缺依赖：cd user && go mod tidy 后重试
```

运行演示（需要 root；持久化闭环：下发 → 保存 → 销毁 → 恢复）：

```bash
sudo ../../scripts/setup-veth.sh

# 1) 加载并挂载（同阶段 5 的 attach 口径）
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s

# 2) 下发规则并保存
sudo ./fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
sudo ./fwctl rules save --file rules.json

# 3) detach 销毁 pin（规则随之消失），再重新 attach（规则表为空）
sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s

# 4) 从文件恢复，核对与保存前一致（也可直接 load 提交的 rules.example.json）
sudo ./fwctl rules load --file rules.json
sudo ./fwctl rule list

# 5) 清理
sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
```

## 预期输出示例

（预期输出示例 / 运行级待验证）

持久化闭环各步：

```text
已下发精确规则。
已保存规则到 rules.json（临时文件 + rename 原子替换）。
已解除挂载并清理 /sys/fs/bpf/fw17（规则/连接/NAT 表随之删除）。
已从 rules.json 恢复规则（逐条下发，未清空既有规则）。
10.0.0.2:5201 -> 10.0.2.2:5201 proto=6 action=drop
```

`rules save` 生成的 `rules.json`（与 `rules.example.json` 同构）：

```json
[
  {
    "proto": "tcp",
    "src": "10.0.0.2",
    "sport": 5201,
    "dst": "10.0.2.2",
    "dport": 5201,
    "action": "drop"
  }
]
```

- 恢复后 `rule list` 与保存前逐条一致；数据面对恢复规则的判定与手工 `rule add` 无差别（同一张 `acl_rules`、同一个 key）。
- 加载非法文件时的报错带序号（如把示例里 `"action": "drop"` 改成 `"block"`）：

```text
fwctl: 恢复规则失败: 第 1 条规则非法: action 仅支持 allow/drop
```

---

## 参考实现思路

以下思路对应本目录内的参考实现（`user/persist.go` + `user/main.go` 的 `rules` 子命令，其余文件与阶段 5 一致），建议自行实现后再对照：

1. **可读的中间格式（`PersistedRule`）**：规则文件不存二进制镜像，而存与 CLI 参数同构的六字段 JSON——`proto` 写 `tcp/udp` 字样、地址写点分十进制、端口写主机字节序数字。map 里的 `FlowKey`/`RuleValue` 是「网络字节序原样存原样查」的机器形态，两种形态的互转只发生在 `rules save/load` 的边界上：导出用 `rules.go` 现成的 `int2ip`/`be16`/`protoName`，恢复复用 `ParseRule`（与 `rule add` 同一套校验：协议仅 tcp/udp、IPv4 合法、端口 0–65535、动作仅 allow/drop）。人可读的中间格式意味着规则文件可入库、可 review、可手工编辑。
2. **`SaveRules` 的遍历与原子写**：遍历用 `MapIterator`（cilium/ebpf v0.16.0 的 `Next(keyOut, valueOut)` 一次取出一对、循环后检查 `Err()`，与 `ListRules` 同款）。编码用 `MarshalIndent` 缩进两格。落盘是两步：先写 `path + ".tmp"`（权限 `0o600`——规则文件含策略语义，不对其他用户开放），再 `os.Rename` 到目标路径。`rename` 在同一文件系统内对已存在目标的替换是原子的，观察者要么看到完整旧版、要么看到完整新版；`save` 中途被杀最多留下 `.tmp`，旧文件不受影响。
3. **`LoadRules` 的「原子替换」口径**：恢复是**逐条 Put、不清空重建**。hash map 的单 key `Put` 在内核侧本就是原子的，「清空重建」才会制造「表空但新规则还没下发完」的瞬时全放行窗口；逐条恢复让已有规则全程保持生效。`Map.Put` 是 `UpdateAny` 语义（同 key 覆盖），所以 `load` 可重复执行、恢复以文件为准——这与 `rule add` 的「已存在即拒绝冲突检查」是刻意不同的两种写入口径：交互式单条下发宁可拒绝也不静默改写，而批量恢复的权威来源是整个规则文件。每条失败（校验或下发）都带「第 N 条」序号返回，配合逐条语义可定位到具体条目修正后重跑。
4. **CLI 的两趟解析**：`rules` 子命令与 `rule add/del` 同款——`flag.NewFlagSet` 第一趟 `Parse` 在子子命令名（save/load）处停下，跳过它再 `Parse` 一趟 `--file` 才就位；`--file` 缺省 `rules.json`。分发入口在 `main.go` 的 switch 里加一个 `case "rules"`，保存/加载逻辑全部收敛在 `persist.go`。
5. **已知边界**：本练习只持久化精确五元组规则（`acl_rules`）——网段规则（`acl_lpm`）的 CIDR 形态与 NAT 规则（`nat_map`）的通配 key 形态各需自己的中间表示，留作进阶扩展；连接表（`conntrack`）不持久化（重启后连接自然重建，持久化陈旧状态反而有害）；`rules.json` 若与 map 均为空则两个方向都是空操作（`save` 写出 `[]`、`load` 什么都不做）。

## 验证状态

- 编译验证：已在编写环境（x86_64、内核 7.0、clang 18、bpftool 7.7、libbpf 1.3 / Go 1.27.1）`make` 通过（两个 BPF 对象 + `go build` + `go vet`），并计入 `../../scripts/verify-build.sh` 的「练习编译」项。编译输出中 `vmlinux.h` 自身会带 5 条 `-Wmissing-declarations` 告警（bpftool 7.7 导出的前向声明在 clang 18 下的已知现象），与练习源码无关。
- 待运行验证：编写环境为非 root（`unprivileged_bpf_disabled=2`），未执行加载、挂载与运行；上方「预期输出示例」为教学示例。

## 参考来源

- `rename(2)`：同一文件系统内对已存在目标的原子替换（「临时文件 + rename」写法的依据）。
- `encoding/json` 文档：`struct tag`、`MarshalIndent`/`Unmarshal`（标准库即可满足的持久化格式）。
- cilium/ebpf 文档与源码（v0.16.0）：`MapIterator.Next` 的弱遍历语义与 `Err` 检查、`Map.Put` 的 `UpdateAny` 语义（同 key 覆盖）。
- POSIX.1-2017 `rename`：跨文件系统 rename 的 `EXDEV` 限制（本练习临时文件与目标同目录，不受影响）。
- 本章阶段 5 示例 `book/examples/chapter-17/05-firewall/`：本练习的基线代码（`user/` 四文件与 `bpf/` 三源逐一对应），规则下发与「逐 key 原子」口径的出处。

// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
//
// fwctl：第 17 章阶段 5 完整防火墙的 Go 控制面。
//
// 数据面/控制面分离：加载、挂载、规则下发、统计读取全部在本进程完成；
// 报文解析、ACL 判定、conntrack、NAT 改写全部在 BPF 数据面完成。
// attach 把 map/prog/link pin 到 /sys/fs/bpf/fw17 之后即可退出，
// 其余子命令通过 LoadPinnedMap 独立操作同一批 pin 实例（detach 才清理）。
//
// 子命令总览（与 C3 map 名 acl_rules/acl_lpm/conntrack/nat_map/stats 对应）：
//
//	sudo ./fwctl attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
//	     --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
//	sudo ./fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
//	sudo ./fwctl rule list
//	sudo ./fwctl rule del --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201
//	sudo ./fwctl cidr add --action drop --cidr 10.0.0.0/24
//	sudo ./fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
//	sudo ./fwctl nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1
//	sudo ./fwctl conntrack list
//	sudo ./fwctl stats
//	sudo ./fwctl detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
//
// 本文件只做子命令分发与参数解析；类型镜像/加载/pin 在 maps.go，
// 规则构造与下发在 rules.go，统计聚合与展示在 stats.go。
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/cilium/ebpf/rlimit"
)

// C3 契约的 map 名：与 SEC(".maps") 定义逐字一致（同名才共享同一 pin 实例）
const (
	mapAclRules  = "acl_rules"
	mapAclLpm    = "acl_lpm"
	mapConntrack = "conntrack"
	mapNat       = "nat_map"
	mapStats     = "stats"
)

func usage() {
	fmt.Fprintf(os.Stderr, `fwctl：第 17 章阶段 5 完整防火墙控制面（需要 root/CAP_BPF + CAP_NET_ADMIN）

用法（在 05-firewall 目录下执行；对象路径相对当前目录）：
  sudo %s attach --xdp-obj bpf/fw_xdp.bpf.o --tc-obj bpf/fw_tc.bpf.o \
       --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
      加载两个 .o（同名 map 合并为同一实例）、挂 XDP 与 TC ingress/egress，
      并把 map/prog/link pin 到 /sys/fs/bpf/fw17
  sudo %s rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop
  sudo %s rule del --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201
  sudo %s rule list
  sudo %s cidr add --action drop --cidr 10.0.0.0/24
  sudo %s nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
  sudo %s nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1
  sudo %s conntrack list
  sudo %s stats
  sudo %s detach --xdp-iface veth-fw-c --tc-ingress veth-fw-c --tc-egress veth-fw-s
      解除 XDP link 与 TC filter，并清理 /sys/fs/bpf/fw17（规则随之消失）

说明：attach 之后规则持续生效、可重复执行 rule/nat 子命令；除 attach/detach 外的
子命令只读写 pin 的 map，不需要重新加载程序。
`, os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0], os.Args[0])
	os.Exit(1)
}

func fatal(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "fwctl: "+format+"\n", args...)
	os.Exit(1)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}

	/* 5.11 之前的内核以 memlock 限制 BPF 内存占用，先解除（新内核调用无害，
	 * 与第 9 章 09-netfilter-go 同一口径）；只在 attach 路径需要 */
	if os.Args[1] == "attach" {
		if err := rlimit.RemoveMemlock(); err != nil {
			fatal("移除 memlock 限制失败: %v", err)
		}
	}

	switch os.Args[1] {
	case "attach":
		cmdAttach()
	case "detach":
		cmdDetach()
	case "rule":
		cmdRule()
	case "cidr":
		cmdCidr()
	case "nat":
		cmdNat()
	case "conntrack":
		cmdConntrack()
	case "stats":
		cmdStats()
	default:
		usage()
	}
}

/* attach：加载两个 .o 并挂载。pin 生命周期（三类对象）：
 *   map  —— MapOptions.PinPath 按名 pin/复用，attach 退出后规则仍在；
 *   prog —— 逐个 pin 成 <名>.prog，供 tc filter replace ... pinned 引用；
 *   link —— XDP 的 bpf_link pin 成 xdp_link，detach 时加载并关闭即解除挂载。 */
func cmdAttach() {
	fs := flag.NewFlagSet("attach", flag.ExitOnError)
	xdpObj := fs.String("xdp-obj", "bpf/fw_xdp.bpf.o", "XDP 数据面对象路径")
	tcObj := fs.String("tc-obj", "bpf/fw_tc.bpf.o", "TC 数据面对象路径")
	xdpIface := fs.String("xdp-iface", "", "XDP 挂载接口（实验拓扑为 veth-fw-c）")
	tcIngress := fs.String("tc-ingress", "", "TC ingress 挂载接口（DNAT，实验拓扑为 veth-fw-c）")
	tcEgress := fs.String("tc-egress", "", "TC egress 挂载接口（SNAT，实验拓扑为 veth-fw-s）")
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if *xdpIface == "" || *tcIngress == "" || *tcEgress == "" {
		fatal("attach 需要 --xdp-iface、--tc-ingress、--tc-egress 三个接口参数")
	}
	if err := loadAndAttach(*xdpObj, *tcObj, *xdpIface, *tcIngress, *tcEgress); err != nil {
		fatal("attach 失败: %v", err)
	}
	fmt.Printf("已挂载：XDP@%s、TC ingress@%s（DNAT）、TC egress@%s（SNAT）。\n",
		*xdpIface, *tcIngress, *tcEgress)
	fmt.Printf("map/prog/link 已 pin 到 %s；规则将持续生效，detach 时清理。\n", pinDir)
}

/* detach：解除挂载并清理 pin 目录。必须显式执行——TC filter 与 pinned 对象
 * 不随进程退出自动消失，残留会让下次 attach 失败（见 README 常见失败点） */
func cmdDetach() {
	fs := flag.NewFlagSet("detach", flag.ExitOnError)
	xdpIface := fs.String("xdp-iface", "", "XDP 挂载接口")
	tcIngress := fs.String("tc-ingress", "", "TC ingress 挂载接口")
	tcEgress := fs.String("tc-egress", "", "TC egress 挂载接口")
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if *xdpIface == "" || *tcIngress == "" || *tcEgress == "" {
		fatal("detach 需要 --xdp-iface、--tc-ingress、--tc-egress 三个接口参数")
	}
	if err := detach(*xdpIface, *tcIngress, *tcEgress); err != nil {
		fatal("detach 失败: %v", err)
	}
	fmt.Printf("已解除挂载并清理 %s（规则/连接/NAT 表随之删除）。\n", pinDir)
}

func cmdRule() {
	fs := flag.NewFlagSet("rule", flag.ExitOnError)
	proto := fs.String("proto", "", "协议：tcp/udp")
	src := fs.String("src", "", "源 IPv4 地址")
	sport := fs.String("sport", "", "源端口")
	dst := fs.String("dst", "", "目的 IPv4 地址")
	dport := fs.String("dport", "", "目的端口")
	action := fs.String("action", "", "动作：allow/drop（仅 add 需要）")
	/* flag.Parse 在第一个非 flag 参数处停止：第一趟解析后 Args() 以子命令名
	 * 开头，各 flag 值要等跳过子命令名的第二趟 Parse 才真正就位 */
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if len(fs.Args()) < 1 {
		usage()
	}
	switch fs.Args()[0] {
	case "add":
		if err := fs.Parse(fs.Args()[1:]); err != nil {
			fatal("%v", err)
		}
		k, v, err := ParseRule(*proto, *src, *sport, *dst, *dport, *action)
		if err != nil {
			fatal("%v", err)
		}
		m, err := openMap(mapAclRules)
		if err != nil {
			fatal("打开 %s: %v", mapAclRules, err)
		}
		defer m.Close()
		if err := AddRule(m, k, v); err != nil {
			fatal("下发规则失败: %v", err)
		}
		fmt.Println("已下发精确规则。")
	case "del":
		if err := fs.Parse(fs.Args()[1:]); err != nil {
			fatal("%v", err)
		}
		// del 只用五元组 key：action 传占位值 allow，仅为复用 ParseRule 的参数校验
		k, _, err := ParseRule(*proto, *src, *sport, *dst, *dport, "allow")
		if err != nil {
			fatal("%v", err)
		}
		m, err := openMap(mapAclRules)
		if err != nil {
			fatal("打开 %s: %v", mapAclRules, err)
		}
		defer m.Close()
		if err := DelRule(m, k); err != nil {
			fatal("删除规则失败（不存在？）: %v", err)
		}
		fmt.Println("已删除规则。")
	case "list":
		m, err := openMap(mapAclRules)
		if err != nil {
			fatal("打开 %s: %v", mapAclRules, err)
		}
		defer m.Close()
		if err := ListRules(m); err != nil {
			fatal("遍历规则失败: %v", err)
		}
	default:
		usage()
	}
}

func cmdCidr() {
	fs := flag.NewFlagSet("cidr", flag.ExitOnError)
	action := fs.String("action", "", "动作：allow/drop")
	cidr := fs.String("cidr", "", "CIDR 网段，如 10.0.0.0/24")
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if len(fs.Args()) < 1 || fs.Args()[0] != "add" {
		usage()
	}
	// 跳过子命令名再解析一次，--action/--cidr 才就位（Parse 停在首个非 flag 参数）
	if err := fs.Parse(fs.Args()[1:]); err != nil {
		fatal("%v", err)
	}
	m, err := openMap(mapAclLpm)
	if err != nil {
		fatal("打开 %s: %v", mapAclLpm, err)
	}
	defer m.Close()
	if err := AddLpmRule(m, *action, *cidr); err != nil {
		fatal("下发网段规则失败: %v", err)
	}
	fmt.Println("已下发网段规则。")
}

func cmdNat() {
	fs := flag.NewFlagSet("nat", flag.ExitOnError)
	typ := fs.String("type", "", "类型：snat/dnat")
	proto := fs.String("proto", "", "协议：tcp/udp")
	src := fs.String("src", "", "原源 IPv4（snat 用）")
	dst := fs.String("dst", "", "原目的 IPv4（dnat 用）")
	dport := fs.String("dport", "", "原目的端口（仅 dnat 需要）")
	to := fs.String("to", "", "新地址；dnat 为 IP:端口，snat 为裸 IP（仅改地址）")
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if len(fs.Args()) < 1 {
		usage()
	}
	switch fs.Args()[0] {
	case "add":
		if err := fs.Parse(fs.Args()[1:]); err != nil {
			fatal("%v", err)
		}
		/* AddNat 的 addr 形参按 type 取值：dnat 用 --dst（原目的地址），
		 * snat 用 --src（原源地址，函数形参名 dst 仅是签名位置）——
		 * 与 rules.go 顶部两条固定 CLI 示例一致 */
		addr := *dst
		if *typ == "snat" {
			addr = *src
		}
		m, err := openMap(mapNat)
		if err != nil {
			fatal("打开 %s: %v", mapNat, err)
		}
		defer m.Close()
		if err := AddNat(m, *typ, *proto, addr, *dport, *to); err != nil {
			fatal("下发 NAT 规则失败: %v", err)
		}
		fmt.Println("已下发 NAT 规则。")
	default:
		usage()
	}
}

func cmdConntrack() {
	fs := flag.NewFlagSet("conntrack", flag.ExitOnError)
	if err := fs.Parse(os.Args[2:]); err != nil {
		fatal("%v", err)
	}
	if len(fs.Args()) < 1 || fs.Args()[0] != "list" {
		usage()
	}
	m, err := openMap(mapConntrack)
	if err != nil {
		fatal("打开 %s: %v", mapConntrack, err)
	}
	defer m.Close()
	if err := PrintConntrack(m); err != nil {
		fatal("遍历连接表失败: %v", err)
	}
}

func cmdStats() {
	m, err := openMap(mapStats)
	if err != nil {
		fatal("打开 %s: %v", mapStats, err)
	}
	defer m.Close()
	if err := PrintStats(m); err != nil {
		fatal("读取统计失败: %v", err)
	}
}

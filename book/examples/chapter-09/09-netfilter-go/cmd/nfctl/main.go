// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
//
// nfctl：第 9 章示例 09-netfilter-go 的 Go 控制面
// （对应 ebpf.top BPF 入门系列实践教程九）。
//
// 子命令：
//   run              加载 BPF、挂到 NF_INET_LOCAL_OUT、循环打印统计（Ctrl-C 卸载）
//   block <port>     把目标端口（TCP/UDP）加入 drop 黑名单
//   unblock <port>   从黑名单移除端口
//   stats            打印当前 pass/drop 统计
//
// 演示“数据面/控制面分离”：Go 只负责加载程序、下发规则、读取统计；
// 报文解析与转发决策全部在 BPF 数据面完成，规则变更不需要重新编译 BPF。
//
// map 通过 LIBBPF_PIN_BY_NAME 固定在 bpffs，run 退出后规则仍保留，
// block/unblock/stats 可在另一个进程/终端独立操作。

package main

import (
	"fmt"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
	"golang.org/x/sys/unix"
)

const objPath = "bpf/nf_guard.bpf.o"

// pin 路径 = bpffs 挂载点 + map 名（LIBBPF_PIN_BY_NAME 语义）
const (
	rulesPin = "/sys/fs/bpf/nf_guard_rules"
	statsPin = "/sys/fs/bpf/nf_guard_stats"
)

// 与 bpf/nf_guard.bpf.c 保持一致的常量。
const (
	actionDrop = 1
	statsPass  = 0
	statsDrop  = 1
)

func usage() {
	fmt.Fprintf(os.Stderr, `用法（需要 root）：
  sudo %s run              加载并挂载，每 2 秒打印统计，Ctrl-C 卸载
  sudo %s block <port>     拉黑目标端口（TCP/UDP）
  sudo %s unblock <port>   解除拉黑
  sudo %s stats            打印 pass/drop 统计
`, os.Args[0], os.Args[0], os.Args[0], os.Args[0])
	os.Exit(1)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}

	// 5.11 之前的内核需要提高 memlock；新内核调用无害。
	if err := rlimit.RemoveMemlock(); err != nil {
		fatal("移除 memlock 限制失败: %v", err)
	}

	switch os.Args[1] {
	case "run":
		if err := run(); err != nil {
			fatal("run 失败: %v", err)
		}
	case "block":
		if len(os.Args) != 3 {
			usage()
		}
		port, err := parsePort(os.Args[2])
		if err != nil {
			fatal("%v", err)
		}
		if err := setRule(port, actionDrop); err != nil {
			fatal("block 失败: %v", err)
		}
	case "unblock":
		if len(os.Args) != 3 {
			usage()
		}
		port, err := parsePort(os.Args[2])
		if err != nil {
			fatal("%v", err)
		}
		if err := delRule(port); err != nil {
			fatal("unblock 失败: %v", err)
		}
	case "stats":
		if err := printStats(); err != nil {
			fatal("读取统计失败: %v", err)
		}
	default:
		usage()
	}
}

func run() error {
	spec, err := ebpf.LoadCollectionSpec(objPath)
	if err != nil {
		return fmt.Errorf("打开 BPF 对象 %s: %w", objPath, err)
	}

	// PinByName：map 已 pin 则复用（规则保留），否则 pin 到 bpffs。
	coll, err := ebpf.NewCollectionWithOptions(spec, ebpf.CollectionOptions{
		Maps: ebpf.MapOptions{Pin: ebpf.PinByName},
	})
	if err != nil {
		return fmt.Errorf("加载 BPF 程序（内核是否 6.6+？）: %w", err)
	}
	defer coll.Close()

	prog := coll.Programs["nf_guard"]
	if prog == nil {
		return fmt.Errorf("BPF 对象中找不到程序 nf_guard")
	}

	// 挂到当前网络命名空间的 IPv4 LOCAL_OUT 钩子，优先级 0。
	// 在 netns 中运行（见 scripts/netns-lab.sh），只影响该 netns。
	nfLink, err := link.AttachNetfilter(link.Netfilter{
		Protocol: unix.NFPROTO_IPV4,
		Hooknum:  uint32(unix.NF_INET_LOCAL_OUT),
		Priority: 0,
	}, prog)
	if err != nil {
		return fmt.Errorf("挂载 netfilter 钩子: %w", err)
	}
	defer nfLink.Close()

	fmt.Printf("nf_guard 已挂到 NF_INET_LOCAL_OUT（优先级 0）\n")
	fmt.Printf("map 已 pin：%s / %s\n", rulesPin, statsPin)
	fmt.Println("按 Ctrl-C 卸载程序；map 与规则将保留。")

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()

	for {
		select {
		case <-sig:
			fmt.Println("\n收到退出信号，卸载 BPF 程序。")
			return nil
		case <-tick.C:
			if err := printStats(); err != nil {
				return err
			}
		}
	}
}

func parsePort(s string) (uint16, error) {
	port, err := strconv.ParseUint(s, 10, 16)
	if err != nil || port == 0 {
		return 0, fmt.Errorf("端口必须是 1-65535 的整数: %s", s)
	}
	return uint16(port), nil
}

func setRule(port uint16, action uint8) error {
	rules, err := ebpf.LoadPinnedMap(rulesPin, nil)
	if err != nil {
		return fmt.Errorf("打开 rules map（是否先运行过 run？）: %w", err)
	}
	defer rules.Close()

	if err := rules.Update(&port, &action, ebpf.UpdateAny); err != nil {
		return fmt.Errorf("写入规则: %w", err)
	}
	fmt.Printf("已拉黑端口 %d\n", port)
	return nil
}

func delRule(port uint16) error {
	rules, err := ebpf.LoadPinnedMap(rulesPin, nil)
	if err != nil {
		return fmt.Errorf("打开 rules map（是否先运行过 run？）: %w", err)
	}
	defer rules.Close()

	if err := rules.Delete(&port); err != nil {
		return fmt.Errorf("删除规则: %w", err)
	}
	fmt.Printf("已解除端口 %d\n", port)
	return nil
}

func printStats() error {
	stats, err := ebpf.LoadPinnedMap(statsPin, nil)
	if err != nil {
		return fmt.Errorf("打开 stats map（是否先运行过 run？）: %w", err)
	}
	defer stats.Close()

	// per-CPU map 的 Lookup 返回每个 CPU 的计数切片，求和得到总数。
	var passPerCPU, dropPerCPU []uint64
	passKey := uint32(statsPass)
	dropKey := uint32(statsDrop)
	if err := stats.Lookup(&passKey, &passPerCPU); err != nil {
		return fmt.Errorf("读取 pass 计数: %w", err)
	}
	if err := stats.Lookup(&dropKey, &dropPerCPU); err != nil {
		return fmt.Errorf("读取 drop 计数: %w", err)
	}

	fmt.Printf("pass=%-12d drop=%d\n", sum(passPerCPU), sum(dropPerCPU))
	return nil
}

func sum(xs []uint64) uint64 {
	var t uint64
	for _, x := range xs {
		t += x
	}
	return t
}

func fatal(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}

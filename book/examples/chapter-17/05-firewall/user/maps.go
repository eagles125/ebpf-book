package main

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
)

const pinDir = "/sys/fs/bpf/fw17"

/* 与 bpf/fw_common.h 逐字节对应（网络字节序手工填充，见 rules.go）。
 * Go 的数组字段自带确定布局、对齐为 1，pad 字段逐字节对应 C 侧 __u8 pad[n]；
 * CtValue 里的 uint64 对齐 8，与 C 侧 __u64 布局一致——改任一侧必须同步另一侧 */
type FlowKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type RuleValue struct {
	Action uint8
	Pad    [7]byte
}

type LpmKey struct {
	PrefixLen uint32
	Addr      [4]byte
}

type LpmValue struct {
	Action uint8
	Pad    [7]byte
}

type CtKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type CtValue struct {
	LastSeenNS uint64
	Packets    uint64
	Bytes      uint64
	State      uint8
	Pad        [7]byte
}

type NatKey struct {
	SrcIP   [4]byte
	DstIP   [4]byte
	SrcPort [2]byte
	DstPort [2]byte
	Proto   uint8
	Pad     [3]byte
}

type NatValue struct {
	NewAddr [4]byte
	NewPort [2]byte
	IsSnat  uint8
	Pad     uint8
}

func ensurePinDir() error {
	return os.MkdirAll(pinDir, 0o700) // bpffs 子目录，detach 时清理
}

/* 两个 .o 的同名 map 必须是同一实例（XDP 与 TC 要看到同一份规则/统计）。
 * cilium/ebpf 不允许一个 Collection 里出现同名 map——把 TC spec 的程序与缺失的
 * map 并入 XDP spec 后一次性加载，同名 map 自然共享。 */
func mergeSpecs(xdp, tc *ebpf.CollectionSpec) *ebpf.CollectionSpec {
	for name, prog := range tc.Programs {
		xdp.Programs[name] = prog
	}
	for name, m := range tc.Maps {
		if _, ok := xdp.Maps[name]; !ok {
			xdp.Maps[name] = m
		}
	}
	return xdp
}

func loadAndAttach(xdpObj, tcObj, xdpIface, tcIngress, tcEgress string) error {
	if err := ensurePinDir(); err != nil {
		return err
	}
	specX, err := ebpf.LoadCollectionSpec(xdpObj)
	if err != nil {
		return fmt.Errorf("解析 %s: %w", xdpObj, err)
	}
	specT, err := ebpf.LoadCollectionSpec(tcObj)
	if err != nil {
		return fmt.Errorf("解析 %s: %w", tcObj, err)
	}
	coll, err := ebpf.NewCollectionWithOptions(mergeSpecs(specX, specT),
		ebpf.CollectionOptions{Maps: ebpf.MapOptions{PinPath: pinDir}})
	if err != nil {
		return fmt.Errorf("加载 BPF（需 root/CAP_BPF）: %w", err)
	}
	defer coll.Close()

	/* pin 两个数据面程序，供 tc filter 以 pinned 方式挂载 */
	for name, prog := range coll.Programs {
		p := filepath.Join(pinDir, name+".prog")
		if err := prog.Pin(p); err != nil {
			return err
		}
	}

	// 接口名 -> ifindex 用标准库解析：cilium/ebpf v0.16.0 的 link 包没有
	// InterfaceByName（那是更高版本的 API），固定版本即以 net.InterfaceByName 为准
	ifi, err := net.InterfaceByName(xdpIface) // 只允许 veth，勿挂物理网卡
	if err != nil {
		return err
	}
	xdpLink, err := link.AttachXDP(link.XDPOptions{
		Interface: ifi.Index, Program: coll.Programs["fw_xdp"],
	})
	if err != nil {
		return fmt.Errorf("attach XDP: %w", err)
	}
	lp := filepath.Join(pinDir, "xdp_link")
	if err := xdpLink.Pin(lp); err != nil { // pin link 供 detach 复用
		return err
	}
	if err := tcAttach(tcIngress, "ingress", filepath.Join(pinDir, "fw_tc_ingress.prog")); err != nil {
		return err
	}
	return tcAttach(tcEgress, "egress", filepath.Join(pinDir, "fw_tc_egress.prog"))
}

/* TC 挂载用 tc 命令 + pinned prog：兼容 5.10+（cilium/ebpf 的 TCX 路径需 6.6+） */
func tcAttach(iface, direction, progPin string) error {
	for _, args := range [][]string{
		{"tc", "qdisc", "replace", "dev", iface, "clsact"},
		{"tc", "filter", "replace", "dev", iface, direction, "bpf", "direct-action", "pinned", progPin},
	} {
		if out, err := exec.Command(args[0], args[1:]...).CombinedOutput(); err != nil {
			return fmt.Errorf("tc attach: %v: %s", err, out)
		}
	}
	return nil
}

func detach(xdpIface, tcIngress, tcEgress string) error {
	if l, err := link.LoadPinnedLink(filepath.Join(pinDir, "xdp_link"), nil); err == nil {
		_ = l.Close() // 关闭 pinned XDP link 即解除挂载
	}
	for _, d := range []string{tcIngress, tcEgress} {
		_ = exec.Command("tc", "filter", "del", "dev", d, "bpf").Run()
	}
	_ = exec.Command("tc", "qdisc", "del", "dev", tcEgress, "clsact").Run()
	return os.RemoveAll(pinDir) // map/prog/link pin 一并清理，避免残留导致二次 attach 失败
}

func openMap(name string) (*ebpf.Map, error) {
	/* pin 目录不存在说明还没 attach 过——给出可操作的提示而非裸 ENOENT */
	if _, err := os.Stat(pinDir); os.IsNotExist(err) {
		return nil, fmt.Errorf("未找到 %s：请先执行 fwctl attach 创建并 pin 映射", pinDir)
	}
	return ebpf.LoadPinnedMap(filepath.Join(pinDir, name), nil)
}

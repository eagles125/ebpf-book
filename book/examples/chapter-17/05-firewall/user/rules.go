package main

import (
	"encoding/binary"
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/cilium/ebpf"
)

/* inet.IPString -> [4]byte（网络字节序） */
func ipBytes(s string) ([4]byte, error) {
	ip := net.ParseIP(s)
	if ip == nil || ip.To4() == nil {
		return [4]byte{}, fmt.Errorf("非法 IPv4: %s", s)
	}
	var b [4]byte
	copy(b[:], ip.To4())
	return b, nil
}

func portBytes(p string) ([2]byte, error) {
	n, err := strconv.Atoi(p)
	if err != nil || n < 0 || n > 65535 {
		return [2]byte{}, fmt.Errorf("非法端口: %s", p)
	}
	var b [2]byte
	binary.BigEndian.PutUint16(b[:], uint16(n))
	return b, nil
}

/* fwctl rule add --proto tcp --src 10.0.0.2 --sport 5201 --dst 10.0.2.2 --dport 5201 --action drop */
func ParseRule(proto, src, sport, dst, dport, action string) (FlowKey, RuleValue, error) {
	var k FlowKey
	var v RuleValue
	if proto != "tcp" && proto != "udp" {
		return k, v, fmt.Errorf("proto 仅支持 tcp/udp")
	}
	var err error
	if k.SrcIP, err = ipBytes(src); err != nil {
		return k, v, err
	}
	if k.DstIP, err = ipBytes(dst); err != nil {
		return k, v, err
	}
	if k.SrcPort, err = portBytes(sport); err != nil {
		return k, v, err
	}
	if k.DstPort, err = portBytes(dport); err != nil {
		return k, v, err
	}
	if proto == "tcp" {
		k.Proto = 6
	} else {
		k.Proto = 17
	}
	if action == "drop" {
		v.Action = 1
	} else if action != "allow" {
		return k, v, fmt.Errorf("action 仅支持 allow/drop")
	}
	return k, v, nil
}

/* 冲突检查：key 已存在即拒绝——hash map 的逐 key 更新本身就是原子的，
 * 「原子更新」指不做清空重建，避免瞬时全放行/全阻断 */
func AddRule(m *ebpf.Map, k FlowKey, v RuleValue) error {
	var existing RuleValue
	if err := m.Lookup(k, &existing); err == nil {
		return fmt.Errorf("规则已存在（冲突），如需覆盖请先 rule del")
	}
	return m.Put(k, v)
}

func DelRule(m *ebpf.Map, k FlowKey) error { return m.Delete(k) }

func ListRules(m *ebpf.Map) error {
	var k FlowKey
	var v RuleValue
	it := m.Iterate()
	// cilium/ebpf v0.16.0 的迭代 API 是 Next(keyOut, valueOut) 一次取出一对、
	// 返回 bool（没有 Decode 方法，那是更高版本的 API）；Err 必须在循环结束后检查
	for it.Next(&k, &v) {
		act := "allow"
		if v.Action == 1 {
			act = "drop"
		}
		fmt.Printf("%s:%d -> %s:%d proto=%d action=%s\n",
			int2ip(k.SrcIP), be16(k.SrcPort), int2ip(k.DstIP), be16(k.DstPort), k.Proto, act)
	}
	return it.Err()
}

/* fwctl cidr add --action drop --cidr 10.0.0.0/24：LPM key 由前缀长度+网络地址构成 */
func AddLpmRule(m *ebpf.Map, action, cidr string) error {
	_, ipnet, err := net.ParseCIDR(cidr)
	if err != nil {
		return err
	}
	ones, _ := ipnet.Mask.Size()
	var k LpmKey
	k.PrefixLen = uint32(ones)
	copy(k.Addr[:], ipnet.IP.To4())
	var v LpmValue
	if strings.EqualFold(action, "drop") {
		v.Action = 1
	}
	return m.Put(k, v)
}

/* NAT：fwctl nat add --type dnat --proto tcp --dst 10.0.1.1 --dport 8080 --to 10.0.2.2:8080
 *      fwctl nat add --type snat --proto tcp --src 10.0.0.2 --to 10.0.2.1   （仅改地址）
 * 与 do_nat 通配 key 契约一致：DNAT 写 {src=0,sport=0}，SNAT 写 {dst=0,dport=0}；
 * snat 的 --to 为裸 IP 时 NewPort 保持 0，数据面按「仅改地址」处理。 */
func AddNat(m *ebpf.Map, typ, proto, dst, dport, to string) error {
	if typ != "snat" && typ != "dnat" {
		return fmt.Errorf("type 仅支持 snat/dnat")
	}
	if proto != "tcp" && proto != "udp" {
		return fmt.Errorf("proto 仅支持 tcp/udp")
	}
	var k NatKey
	var v NatValue
	var err error
	if typ == "dnat" {
		if k.DstIP, err = ipBytes(dst); err != nil {
			return err
		}
		if k.DstPort, err = portBytes(dport); err != nil {
			return err
		}
		host, portStr, err2 := net.SplitHostPort(to)
		if err2 != nil {
			return fmt.Errorf("--to 需为 IP:端口: %w", err2)
		}
		if v.NewAddr, err = ipBytes(host); err != nil {
			return err
		}
		if v.NewPort, err = portBytes(portStr); err != nil {
			return err
		}
	} else {
		if k.SrcIP, err = ipBytes(dst); err != nil {
			return err
		}
		if v.NewAddr, err = ipBytes(to); err != nil {
			return err
		}
	}
	if proto == "tcp" {
		k.Proto = 6
	} else {
		k.Proto = 17
	}
	if typ == "snat" {
		v.IsSnat = 1
	}
	return m.Put(k, v)
}

/* 共享小工具（rules.go 内定义，stats.go / 练习 02 persist.go 复用） */
func int2ip(b [4]byte) string { return net.IP(b[:]).String() }
func be16(b [2]byte) uint16   { return binary.BigEndian.Uint16(b[:]) }
func protoName(p uint8) string {
	if p == 6 {
		return "tcp"
	}
	return "udp"
}

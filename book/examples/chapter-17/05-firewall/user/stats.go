package main

import (
	"encoding/binary"
	"fmt"

	"github.com/cilium/ebpf"
)

var statNames = [...]string{
	"total", "pass", "drop_acl", "drop_parse",
	"ct_new", "ct_established", "ct_full", "nat",
} // 顺序即 C1 契约，不得改动

/* per-CPU 聚合：lookup 返回 nr_cpus 份 __u64，逐 CPU 求和（漏 CPU 即漏计数） */
func SumPerCPU(m *ebpf.Map, key uint32) (uint64, error) {
	var all []uint64
	if err := m.Lookup(key, &all); err != nil {
		return 0, err
	}
	var sum uint64
	for _, v := range all {
		sum += v
	}
	return sum, nil
}

func PrintStats(m *ebpf.Map) error {
	for i, name := range statNames {
		v, err := SumPerCPU(m, uint32(i))
		if err != nil {
			return err
		}
		fmt.Printf("%-16s %d\n", name, v)
	}
	return nil
}

func PrintConntrack(m *ebpf.Map) error {
	var k CtKey
	var v CtValue
	it := m.Iterate()
	// cilium/ebpf v0.16.0 的迭代 API：Next(keyOut, valueOut) 一次取出一对、
	// 返回 bool；遍历期间被 LRU 淘汰的表项会被自动跳过，结束后必须检查 Err
	for it.Next(&k, &v) {
		states := map[uint8]string{1: "NEW", 2: "ESTABLISHED", 3: "FIN_WAIT", 4: "CLOSED"}
		fmt.Printf("%s:%d -> %s:%d proto=%d state=%s\n",
			int2ip(k.SrcIP), binary.BigEndian.Uint16(k.SrcPort[:]),
			int2ip(k.DstIP), binary.BigEndian.Uint16(k.DstPort[:]),
			k.Proto, states[v.State])
	}
	return it.Err()
}

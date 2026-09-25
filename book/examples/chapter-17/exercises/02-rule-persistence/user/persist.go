package main

// 规则持久化：保存为 JSON，重启后 fwctl rules load 恢复（CLI 见 main.go 的 rules 子命令）。
// 「原子替换」口径：恢复时逐条 Put（hash map 逐 key 原子），不清空重建，
// 避免「先清空后下发」的瞬时全放行窗口。

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/cilium/ebpf"
)

// PersistedRule 是规则文件里的一条规则：字段与 rule add 的 CLI 参数一一对应，
// 地址/端口/协议写成点分十进制、主机字节序与 tcp/udp 字样（人可读、可手工编辑），
// 与 map 里网络字节序原样存储的 FlowKey/RuleValue 相互转换
type PersistedRule struct {
	Proto  string `json:"proto"` // tcp/udp
	Src    string `json:"src"`   // 点分 IPv4
	Sport  int    `json:"sport"`
	Dst    string `json:"dst"`
	Dport  int    `json:"dport"`
	Action string `json:"action"` // allow/drop
}

/* SaveRules 把 acl_rules 全量导出为 JSON：遍历 map 转回可读形式，
 * 先写临时文件再 rename（见函数内注释） */
func SaveRules(path string, m *ebpf.Map) error {
	var k FlowKey
	var v RuleValue
	var out []PersistedRule
	it := m.Iterate()
	// cilium/ebpf v0.16.0 的迭代 API 是 Next(keyOut, valueOut) 一次取出一对、
	// 返回 bool（与 rules.go 的 ListRules 同款）；循环结束后必须检查 Err
	for it.Next(&k, &v) {
		out = append(out, PersistedRule{
			Proto: protoName(k.Proto), Src: int2ip(k.SrcIP),
			Sport: int(be16(k.SrcPort)), Dst: int2ip(k.DstIP),
			Dport:  int(be16(k.DstPort)),
			Action: map[uint8]string{0: "allow", 1: "drop"}[v.Action],
		})
	}
	if err := it.Err(); err != nil {
		return err
	}
	data, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	// 先写临时文件再 rename：rename 原子，避免半截规则文件
	// ——保存中途被 kill 也只留下 .tmp，旧规则文件要么完整旧版要么完整新版
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

/* LoadRules 从 JSON 逐条恢复规则：ParseRule 与 rule add 走同一套校验，
 * 非法条目带序号定位；逐条 Put、不清空重建（见文件头「原子替换」口径） */
func LoadRules(path string, m *ebpf.Map) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var rules []PersistedRule
	if err := json.Unmarshal(data, &rules); err != nil {
		return err
	}
	for i, r := range rules {
		k, v, err := ParseRule(r.Proto, r.Src, fmt.Sprint(r.Sport),
			r.Dst, fmt.Sprint(r.Dport), r.Action)
		if err != nil {
			return fmt.Errorf("第 %d 条规则非法: %w", i+1, err)
		}
		// 逐条下发，失败带序号定位；Put 是 UpdateAny 语义，同 key 覆盖
		// （与 rule add 的冲突检查不同：恢复路径以规则文件为准，可重复执行）
		if err := m.Put(k, v); err != nil {
			return fmt.Errorf("第 %d 条下发失败: %w", i+1, err)
		}
	}
	return nil
}

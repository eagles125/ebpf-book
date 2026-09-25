package main

// Prometheus 指标导出：GET /metrics 输出 fw17_packets_total{reason="..."} 计数器。
// 设计要点：per-CPU 聚合放请求时做（导出频率低，不值得后台聚合）；
// reason label 取值固定为 statNames（C1 契约），不暴露动态 label。

import (
	"fmt"
	"net/http"
	"strings"

	"github.com/cilium/ebpf"
)

/* metricsHandler 每次请求实时遍历 8 个统计下标：计数器值是自 map 创建以来的
 * 累计值（counter 语义，增量交给 Prometheus 的 rate()），与 fwctl stats 完全同口径
 * ——同一份 statNames 顺序（C1）与同一个 SumPerCPU 聚合 */
func metricsHandler(stats *ebpf.Map) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var b strings.Builder
		b.WriteString("# HELP fw17_packets_total firewall packet counters.\n")
		b.WriteString("# TYPE fw17_packets_total counter\n")
		for i, name := range statNames {
			v, err := SumPerCPU(stats, uint32(i))
			if err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			fmt.Fprintf(&b, "fw17_packets_total{reason=%q} %d\n", name, v)
		}
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		_, _ = w.Write([]byte(b.String()))
	}
}

/* ServeMetrics 注册 /metrics 并阻塞运行（CLI 的 export 子命令调到这里即不再返回） */
func ServeMetrics(listen string, stats *ebpf.Map) error {
	http.HandleFunc("/metrics", metricsHandler(stats))
	return http.ListenAndServe(listen, nil) // 默认 :9100，需在容器/主机放行端口
}

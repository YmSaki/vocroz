// SPDX-License-Identifier: AGPL-3.0-only
//
// compensate_latency() と analyze_latency() の実装。

#include "latency_planner.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "vocroz/audio_core/constants.h"

#include "graph_validator.h"

namespace vocroz::audio_core {

using common::make_unexpected;

common::Result<LatencyReport, GraphError> compensate_latency(
    const GraphDescription& description, std::span<const std::size_t> order,
    const std::vector<std::vector<std::size_t>>& inputs_of, std::span<const std::size_t> from_of) {
    LatencyReport report;
    report.node_latency_samples.assign(description.nodes.size(), 0);
    report.compensation_samples.assign(description.connections.size(), 0);
    for (const std::size_t node : order) {
        const NodeSpec& spec = description.nodes[node];
        // 遅延ノードの入力は前のブロックまでに書いた信号（ループで戻る信号を含む）で、処理順では
        // 後ろのノードから来る。演出として意図した遅延なので、合流をそろえる対象にしない。
        if (spec.kind == NodeKind::kDelay) {
            continue;
        }
        std::size_t arrival = 0;
        for (const std::size_t c : inputs_of[node]) {
            arrival = std::max(arrival, report.node_latency_samples[from_of[c]]);
        }
        for (const std::size_t c : inputs_of[node]) {
            report.compensation_samples[c] = arrival - report.node_latency_samples[from_of[c]];
        }
        const std::size_t own = spec.kind == NodeKind::kPlugin ? spec.latency_samples : 0;
        report.node_latency_samples[node] = arrival + own;
        // 経路の遅延を上限で抑えると、補正（経路の遅延の差）も上限以下に収まる。
        if (report.node_latency_samples[node] > kMaxLatencyCompensationSamples) {
            return make_unexpected(GraphError::kLatencyOutOfRange);
        }
    }
    return report;
}

common::Result<LatencyReport, GraphError> analyze_latency(const GraphDescription& description) {
    constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();
    auto plan = plan_graph(description, kUnlimited, kUnlimited);
    if (!plan) {
        return make_unexpected(plan.error());
    }
    return std::move(plan).value().latency;
}

}  // namespace vocroz::audio_core

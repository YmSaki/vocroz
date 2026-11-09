// SPDX-License-Identifier: AGPL-3.0-only
//
// add_minus_me_returns() の実装。

#include "vocroz/audio_core/minus_me.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// from から結線をたどって届くノード（from 自身を含む）を集める。
// センドもプリフェーダーの結線もたどる。どの取り出し位置でも、その子機の声が乗っているため。
std::set<NodeId> reachable_from(const std::vector<NodeId>& from,
                                const std::multimap<NodeId, NodeId>& edges) {
    std::set<NodeId> reached(from.begin(), from.end());
    std::vector<NodeId> pending(from.begin(), from.end());
    while (!pending.empty()) {
        const NodeId current = pending.back();
        pending.pop_back();
        const auto [first, last] = edges.equal_range(current);
        for (auto it = first; it != last; ++it) {
            if (reached.insert(it->second).second) {
                pending.push_back(it->second);
            }
        }
    }
    return reached;
}

}  // namespace

common::Result<MinusMeReport, GraphError> add_minus_me_returns(
    GraphDescription& graph, std::span<const NodeId> mix_inputs,
    std::span<const MinusMeReturn> returns) {
    std::set<NodeId> known;
    std::uint32_t next_id = 0;
    for (const NodeSpec& node : graph.nodes) {
        known.insert(node.id);
        next_id = std::max(next_id, node.id.value + 1);
    }
    const auto is_known = [&known](NodeId id) { return known.contains(id); };
    if (!std::all_of(mix_inputs.begin(), mix_inputs.end(), is_known)) {
        return make_unexpected(GraphError::kUnknownNode);
    }
    for (const MinusMeReturn& ret : returns) {
        if (!std::all_of(ret.own_inputs.begin(), ret.own_inputs.end(), is_known)) {
            return make_unexpected(GraphError::kUnknownNode);
        }
        if (!ret.sink) {
            return make_unexpected(GraphError::kMissingSink);
        }
    }

    std::multimap<NodeId, NodeId> edges;
    for (const Connection& connection : graph.connections) {
        edges.emplace(connection.from, connection.to);
    }

    MinusMeReport report;
    report.taps.reserve(returns.size());
    for (const MinusMeReturn& ret : returns) {
        const std::set<NodeId> own_voice = reachable_from(ret.own_inputs, edges);
        const NodeId limiter{next_id++};
        const NodeId tap{next_id++};
        graph.nodes.push_back(NodeSpec::make_limiter(limiter));
        graph.nodes.push_back(NodeSpec::make_return_tap(tap, ret.sink));
        std::set<NodeId> connected;
        for (const NodeId input : mix_inputs) {
            // 同じ候補が 2 回あっても 1 本だけつなぐ。重なった結線は 2 倍（+6 dB）で加算される。
            if (!own_voice.contains(input) && connected.insert(input).second) {
                // ポストフェーダーで取り、オペレーターが作ったメインのバランスのまま返す。
                graph.connections.emplace_back(input, limiter);
            }
        }
        graph.connections.emplace_back(limiter, tap);
        report.taps.push_back(tap);
    }
    return report;
}

}  // namespace vocroz::audio_core

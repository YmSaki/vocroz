// SPDX-License-Identifier: AGPL-3.0-only
//
// analyze_feedback() の実装。強連結成分（Tarjan のアルゴリズム）でループを見つけ、
// 遅延ノードへ入る結線を除いたグラフでもう一度強連結成分を求めて、遅延のないループを見つける。

#include "vocroz/audio_core/feedback_analysis.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <utility>

namespace vocroz::audio_core {
namespace {

using Adjacency = std::vector<std::vector<std::size_t>>;

// ループに含まれるノード（大きさ 2 以上の強連結成分か、自己ループのあるノード）を返す。
// 再帰を使うとノード数に比例してスタックを使うので、明示的なスタックで反復的に辿る。
std::vector<bool> nodes_in_cycles(const Adjacency& outputs, const std::vector<bool>& self_loop) {
    const std::size_t count = outputs.size();
    constexpr std::size_t kUnvisited = static_cast<std::size_t>(-1);
    std::vector<std::size_t> index(count, kUnvisited);
    std::vector<std::size_t> low_link(count, 0);
    std::vector<bool> on_stack(count, false);
    std::vector<std::size_t> stack;
    std::vector<bool> in_cycle(count, false);
    std::size_t next_index = 0;

    // (ノード, 次に見る出力の位置) の組で、再帰呼び出しの途中経過を表す。
    std::vector<std::pair<std::size_t, std::size_t>> frames;
    for (std::size_t root = 0; root < count; ++root) {
        if (index[root] != kUnvisited) {
            continue;
        }
        frames.emplace_back(root, 0);
        while (!frames.empty()) {
            auto& [node, edge] = frames.back();
            if (edge == 0 && index[node] == kUnvisited) {
                index[node] = low_link[node] = next_index++;
                stack.push_back(node);
                on_stack[node] = true;
            }
            if (edge < outputs[node].size()) {
                const std::size_t next = outputs[node][edge++];
                if (index[next] == kUnvisited) {
                    frames.emplace_back(next, 0);
                } else if (on_stack[next]) {
                    low_link[node] = std::min(low_link[node], index[next]);
                }
                continue;
            }
            // node の出力をすべて見終えた。node が成分の根なら、成分をスタックから取り出す。
            const std::size_t finished = node;
            if (low_link[finished] == index[finished]) {
                std::vector<std::size_t> component;
                std::size_t member = 0;
                do {
                    member = stack.back();
                    stack.pop_back();
                    on_stack[member] = false;
                    component.push_back(member);
                } while (member != finished);
                const bool looped = component.size() > 1 || self_loop[finished];
                for (const std::size_t m : component) {
                    in_cycle[m] = looped;
                }
            }
            frames.pop_back();
            if (!frames.empty()) {
                const std::size_t parent = frames.back().first;
                low_link[parent] = std::min(low_link[parent], low_link[finished]);
            }
        }
    }
    return in_cycle;
}

std::vector<NodeId> sorted_ids(const std::vector<NodeId>& ids, const std::vector<bool>& selected) {
    std::vector<NodeId> result;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (selected[i]) {
            result.push_back(ids[i]);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

FeedbackReport analyze_feedback(const GraphDescription& description) {
    // 同じ NodeId が重複していれば最初のノードを使う（重複は検証で別に拒否する）。
    std::map<NodeId, std::size_t> index_of;
    std::vector<NodeId> ids;
    std::vector<bool> is_delay;
    for (const NodeSpec& spec : description.nodes) {
        if (index_of.emplace(spec.id, ids.size()).second) {
            ids.push_back(spec.id);
            is_delay.push_back(spec.kind == NodeKind::kDelay);
        }
    }

    const std::size_t count = ids.size();
    Adjacency all_outputs(count);
    Adjacency undelayed_outputs(count);
    std::vector<bool> self_loop(count, false);
    std::vector<bool> undelayed_self_loop(count, false);
    for (const Connection& connection : description.connections) {
        const auto from = index_of.find(connection.from);
        const auto to = index_of.find(connection.to);
        if (from == index_of.end() || to == index_of.end()) {
            continue;
        }
        all_outputs[from->second].push_back(to->second);
        // 遅延ノードの出力は前のブロックまでの入力から作るので、遅延ノードへ入る結線は
        // 同じブロックの中の依存を作らない。この結線を除いても残るループが、遅延のないループ。
        const bool into_delay = is_delay[to->second];
        if (!into_delay) {
            undelayed_outputs[from->second].push_back(to->second);
        }
        if (from->second == to->second) {
            self_loop[from->second] = true;
            undelayed_self_loop[from->second] = !into_delay;
        }
    }

    FeedbackReport report;
    report.looped_nodes = sorted_ids(ids, nodes_in_cycles(all_outputs, self_loop));
    report.undelayed_nodes =
        sorted_ids(ids, nodes_in_cycles(undelayed_outputs, undelayed_self_loop));
    return report;
}

}  // namespace vocroz::audio_core

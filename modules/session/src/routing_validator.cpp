// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの構成の検証の実装。
// 「件数 → ID → 出力のチャンネル → 遅延 → センド → ループ」の順に確かめ、最初の問題を返す。
// 先に ID と参照を確かめておくと、ループの検出はノードがそろったグラフだけを相手にできる。

#include "routing_validator.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/feedback_analysis.h"
#include "vocroz/audio_core/graph_description.h"
#include "vocroz/session/routing_delay.h"

#include "session_constants.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
namespace ac = audio_core;
using cp::RoutingIssue;

/// バスと出力はステレオなので、出力 1 つがデバイスの 2 チャンネルを使う。
constexpr std::uint64_t kStereoChannels = 2;

/// メインの出力（Master の出力先）が使う先頭のチャンネル。追加の出力はここと重ねられない。
constexpr std::uint64_t kMainOutputChannel = 0;

/// グラフの中でのノードの役割。
enum class Role { kStrip, kMaster, kBus, kOutput, kDelay };

/// 見つかった問題。
struct Problem {
    RoutingIssue issue;
    std::optional<std::string> entity_id;
};

using Outcome = std::optional<Problem>;

Problem problem(RoutingIssue issue, const std::string& entity_id) {
    return Problem{issue, entity_id};
}

/// entity_id からノードの役割を引く表。
using NodeTable = std::map<std::string, Role, std::less<>>;

Outcome check_counts(const cp::RoutingConfig& config) {
    if (config.buses.size() > kMaxRoutingBuses || config.outputs.size() > kMaxRoutingOutputs ||
        config.sends.size() > kMaxRoutingSends || config.delays.size() > kMaxRoutingDelays) {
        return Problem{RoutingIssue::kTooManyItems, std::nullopt};
    }
    return std::nullopt;
}

/// 種類ごとの接頭辞と一意性を確かめて、ノードを表に登録する。
/// センドもノードではないが ID の一意性の対象にする（フェーダーの entity_id になるため）。
Outcome register_ids(const cp::RoutingConfig& config, std::span<const std::string> strip_ids,
                     NodeTable& nodes) {
    for (const auto& id : strip_ids) {
        nodes.emplace(id, Role::kStrip);
    }
    nodes.emplace(kMasterEntityId, Role::kMaster);
    std::set<std::string, std::less<>> send_ids;

    const auto add = [&](const std::string& id, std::string_view prefix,
                         std::optional<Role> role) -> Outcome {
        if (!id.starts_with(prefix) || id.size() == prefix.size()) {
            return problem(RoutingIssue::kInvalidId, id);
        }
        if (nodes.contains(id) || send_ids.contains(id)) {
            return problem(RoutingIssue::kDuplicateId, id);
        }
        if (role) {
            nodes.emplace(id, *role);
        } else {
            send_ids.insert(id);
        }
        return std::nullopt;
    };
    for (const auto& bus : config.buses) {
        if (auto p = add(bus.entity_id, kBusIdPrefix, Role::kBus)) {
            return p;
        }
    }
    for (const auto& output : config.outputs) {
        if (auto p = add(output.entity_id, kOutputIdPrefix, Role::kOutput)) {
            return p;
        }
    }
    for (const auto& delay : config.delays) {
        if (auto p = add(delay.entity_id, kDelayIdPrefix, Role::kDelay)) {
            return p;
        }
    }
    for (const auto& send : config.sends) {
        if (auto p = add(send.entity_id, kSendIdPrefix, std::nullopt)) {
            return p;
        }
    }
    return std::nullopt;
}

/// 追加の出力は 1 台の出力デバイスのチャンネルの組に書く。複数のデバイスへの出力は、
/// デバイスごとのクロックのずれを補正する仕組みがないので扱わない（docs/adr/0018-routing-graph.md）。
Outcome check_outputs(const cp::RoutingConfig& config) {
    std::vector<std::uint64_t> used;
    for (const auto& output : config.outputs) {
        if (output.device_channel > ac::kMaxOutputChannels - kStereoChannels) {
            return problem(RoutingIssue::kOutputChannelOutOfRange, output.entity_id);
        }
        const auto overlaps = [&output](std::uint64_t first) {
            const auto low = std::min(first, output.device_channel);
            const auto high = std::max(first, output.device_channel);
            return high - low < kStereoChannels;
        };
        if (overlaps(kMainOutputChannel) || std::any_of(used.begin(), used.end(), overlaps)) {
            return problem(RoutingIssue::kOverlappingOutputs, output.entity_id);
        }
        used.push_back(output.device_channel);
    }
    return std::nullopt;
}

Outcome check_delays(const cp::RoutingConfig& config, const NodeTable& nodes) {
    for (const auto& delay : config.delays) {
        // 遅延ノードはブロックの長さより短くできない。ループの戻りを次のブロックで読むため。
        const auto samples = delay_samples(delay.delay_ms);
        if (samples < ac::kMinFeedbackDelaySamples || samples > ac::kMaxFeedbackDelaySamples) {
            return problem(RoutingIssue::kDelayOutOfRange, delay.entity_id);
        }
    }
    std::set<std::string_view> delayed_strips;
    for (const auto& delay : config.input_delays) {
        const auto it = nodes.find(delay.strip_id);
        if (it == nodes.end() || it->second != Role::kStrip) {
            return problem(RoutingIssue::kUnknownNode, delay.strip_id);
        }
        if (!delayed_strips.insert(delay.strip_id).second) {
            return problem(RoutingIssue::kDuplicateId, delay.strip_id);
        }
        if (delay_samples(delay.delay_ms) > ac::kMaxInputDelaySamples) {
            return problem(RoutingIssue::kDelayOutOfRange, delay.strip_id);
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool can_send_from(Role role) noexcept {
    switch (role) {
        case Role::kStrip:
        case Role::kMaster:
        case Role::kBus:
        case Role::kDelay:
            return true;
        case Role::kOutput:
            return false;
    }
    return false;
}

[[nodiscard]] bool can_send_to(Role role) noexcept {
    switch (role) {
        case Role::kMaster:
        case Role::kBus:
        case Role::kOutput:
        case Role::kDelay:
            return true;
        case Role::kStrip:
            // ストリップは 1 人の参加者の入力だけを処理するモノラルの段なので、入力を受けない。
            return false;
    }
    return false;
}

Outcome check_sends(const cp::RoutingConfig& config, const NodeTable& nodes) {
    std::set<std::pair<std::string_view, std::string_view>> pairs;
    for (const auto& send : config.sends) {
        const auto from = nodes.find(send.from);
        const auto to = nodes.find(send.to);
        if (from == nodes.end() || to == nodes.end()) {
            return problem(RoutingIssue::kUnknownNode, send.entity_id);
        }
        // ストリップから Master への結線は暗黙にあるので、書くと重複になる。
        const bool implicit = from->second == Role::kStrip && to->second == Role::kMaster;
        if (!can_send_from(from->second) || !can_send_to(to->second) || send.from == send.to ||
            implicit || !pairs.emplace(send.from, send.to).second) {
            return problem(RoutingIssue::kInvalidSend, send.entity_id);
        }
        // プリフェーダーはフェーダーのあるストリップとバスからだけ取り出せる。
        // Master のプリフェーダーは用途がなく、Master のミュートを迂回する経路になるので認めない。
        if (send.tap == cp::SendTap::kPreFader && from->second != Role::kStrip &&
            from->second != Role::kBus) {
            return problem(RoutingIssue::kInvalidSendTap, send.entity_id);
        }
    }
    return std::nullopt;
}

/// ループの検出のためだけのグラフ。フェーダーやメーターの識別子はループの判定に使わないので 0
/// にする。
struct LoopGraph {
    ac::GraphDescription description;
    std::vector<std::string> names;
};

LoopGraph build_loop_graph(const cp::RoutingConfig& config, const NodeTable& nodes) {
    LoopGraph graph;
    std::map<std::string_view, ac::NodeId> ids;
    for (const auto& [name, role] : nodes) {
        const ac::NodeId id{static_cast<std::uint32_t>(graph.names.size())};
        graph.names.push_back(name);
        ids.emplace(name, id);
        switch (role) {
            case Role::kStrip:
                graph.description.nodes.push_back(ac::NodeSpec::make_strip(id, {}, std::nullopt));
                break;
            case Role::kMaster:
            case Role::kBus:
                graph.description.nodes.push_back(ac::NodeSpec::make_bus(id, {}, std::nullopt));
                break;
            case Role::kOutput:
                graph.description.nodes.push_back(ac::NodeSpec::make_output(id, std::nullopt));
                break;
            case Role::kDelay:
                graph.description.nodes.push_back(
                    ac::NodeSpec::make_delay(id, ac::kMinFeedbackDelaySamples));
                break;
        }
    }
    const auto master = ids.at(kMasterEntityId);
    for (const auto& [name, role] : nodes) {
        if (role == Role::kStrip) {
            graph.description.connections.emplace_back(ids.at(name), master);
        }
    }
    for (const auto& send : config.sends) {
        graph.description.connections.emplace_back(ids.at(send.from), ids.at(send.to));
    }
    return graph;
}

std::vector<std::string> names_of(const LoopGraph& graph, const std::vector<ac::NodeId>& ids) {
    std::vector<std::string> names;
    names.reserve(ids.size());
    for (const auto id : ids) {
        names.push_back(graph.names.at(id.value));
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

cp::RoutingCheck check_routing(const cp::RoutingConfig& config,
                               std::span<const std::string> strip_ids) {
    NodeTable nodes;
    Outcome outcome = check_counts(config);
    if (!outcome) {
        outcome = register_ids(config, strip_ids, nodes);
    }
    if (!outcome) {
        outcome = check_outputs(config);
    }
    if (!outcome) {
        outcome = check_delays(config, nodes);
    }
    if (!outcome) {
        outcome = check_sends(config, nodes);
    }
    cp::RoutingCheck check;
    if (outcome) {
        check.issue = outcome->issue;
        check.issue_entity_id = std::move(outcome->entity_id);
        return check;
    }

    const auto graph = build_loop_graph(config, nodes);
    const auto report = ac::analyze_feedback(graph.description);
    if (!report.all_loops_delayed()) {
        // 遅延のないループは承認しても適用できないので、そのループのノードだけを示す。
        check.looped_entity_ids = names_of(graph, report.undelayed_nodes);
        check.issue = RoutingIssue::kUndelayedLoop;
        check.issue_entity_id = check.looped_entity_ids.front();
        return check;
    }
    check.looped_entity_ids = names_of(graph, report.looped_nodes);
    check.needs_approval = report.has_loops();
    return check;
}

}  // namespace vocroz::session

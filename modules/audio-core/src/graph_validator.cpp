// SPDX-License-Identifier: AGPL-3.0-only
//
// plan_graph() の実装。ノード、結線、フィードバックループの順に検証し、処理順を決める。

#include "graph_validator.h"

#include <map>
#include <optional>
#include <set>
#include <utility>

#include "vocroz/audio_core/constants.h"
#include "vocroz/audio_core/feedback_analysis.h"

#include "audio_core_constants.h"
#include "latency_planner.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// ノードの種類ごとのパラメーター（ソース、フェーダー、遅延）を検証する。
std::optional<GraphError> validate_node(const NodeSpec& spec, std::size_t fader_capacity,
                                        std::size_t meter_capacity) {
    switch (spec.kind) {
        case NodeKind::kSource:
            if (!spec.source) {
                return GraphError::kMissingSource;
            }
            break;
        case NodeKind::kStrip:
            if (spec.delay_samples > kMaxInputDelaySamples) {
                return GraphError::kDelayOutOfRange;
            }
            [[fallthrough]];
        case NodeKind::kBus:
            if (!spec.fader) {
                return GraphError::kMissingFader;
            }
            break;
        case NodeKind::kDelay:
            if (spec.delay_samples < kMinFeedbackDelaySamples ||
                spec.delay_samples > kMaxFeedbackDelaySamples) {
                return GraphError::kDelayOutOfRange;
            }
            break;
        case NodeKind::kLimiter:
            break;
        case NodeKind::kOutput:
            // L と R の 2 チャンネルがデバイスの範囲に収まること。
            if (spec.device_channel + kStereoChannels > kMaxOutputChannels) {
                return GraphError::kOutputChannelOutOfRange;
            }
            break;
        case NodeKind::kPlugin:
            if (!spec.plugin) {
                return GraphError::kMissingPlugin;
            }
            if (spec.plugin_layout.input_channels > kStereoChannels ||
                spec.plugin_layout.output_channels == 0 ||
                spec.plugin_layout.output_channels > kStereoChannels) {
                return GraphError::kInvalidPluginLayout;
            }
            if (spec.latency_samples > kMaxLatencyCompensationSamples) {
                return GraphError::kLatencyOutOfRange;
            }
            break;
    }
    if (spec.fader && spec.fader->value >= fader_capacity) {
        return GraphError::kFaderOutOfRange;
    }
    if (spec.meter && spec.meter->value >= meter_capacity) {
        return GraphError::kMeterOutOfRange;
    }
    return std::nullopt;
}

// フェーダーの使用を記録する。同じフェーダーを 2 か所が使うと、1 ブロックでランプが 2 回進む。
std::optional<GraphError> claim_fader(std::optional<FaderId> fader, std::size_t fader_capacity,
                                      std::set<FaderId>& used) {
    if (!fader) {
        return std::nullopt;
    }
    if (fader->value >= fader_capacity) {
        return GraphError::kFaderOutOfRange;
    }
    if (!used.insert(*fader).second) {
        return GraphError::kDuplicateFader;
    }
    return std::nullopt;
}

// 結線 1 本を検証する。
std::optional<GraphError> validate_connection(const Connection& connection, const NodeSpec& from,
                                              const NodeSpec& to) {
    // 音声入力を持たないプラグイン（VSTi）への結線も、受け取る先がないので拒否する。
    if (from.kind == NodeKind::kOutput || to.kind == NodeKind::kSource ||
        input_channels_of(to) == 0) {
        return GraphError::kInvalidConnection;
    }
    // モノラルからステレオへは両チャンネルに複製するが、ステレオからモノラルへの
    // ダウンミックスの規則は決めていないので受け付けない。
    if (output_channels_of(from) > input_channels_of(to)) {
        return GraphError::kChannelMismatch;
    }
    if (connection.tap == SendTap::kPreFader) {
        if (from.kind != NodeKind::kStrip && from.kind != NodeKind::kBus) {
            return GraphError::kInvalidSendTap;
        }
        if (!connection.level) {
            return GraphError::kMissingSendLevel;
        }
    }
    return std::nullopt;
}

// MIDI ソースと MIDI の経路を検証し、ノードごとの MIDI の送り元を plan に書く。
std::optional<GraphError> plan_midi(const GraphDescription& description,
                                    const std::map<NodeId, std::size_t>& index_of,
                                    GraphPlan& plan) {
    std::map<MidiSourceId, std::size_t> source_index_of;
    for (std::size_t i = 0; i < description.midi_sources.size(); ++i) {
        const MidiSourceSpec& spec = description.midi_sources[i];
        if (!source_index_of.emplace(spec.id, i).second) {
            return GraphError::kDuplicateMidiSource;
        }
        if (!spec.source) {
            return GraphError::kMissingMidiSource;
        }
    }
    plan.midi_sources_of.resize(description.nodes.size());
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (const MidiConnection& connection : description.midi_connections) {
        const auto from = source_index_of.find(connection.from);
        const auto to = index_of.find(connection.to);
        // MIDI を受け取れるのはプラグインだけ。重複した経路は同じイベントを 2 回送り、
        // 1 回目の Note Off で止まった音を 2 回目の Note On で鳴らし直すことがある。
        if (from == source_index_of.end() || to == index_of.end() ||
            description.nodes[to->second].kind != NodeKind::kPlugin ||
            !seen.emplace(from->second, to->second).second) {
            return GraphError::kInvalidMidiConnection;
        }
        plan.midi_sources_of[to->second].push_back(from->second);
    }
    return std::nullopt;
}

}  // namespace

std::size_t output_channels_of(const NodeSpec& spec) noexcept {
    switch (spec.kind) {
        case NodeKind::kSource:
        case NodeKind::kStrip:
            return 1;
        case NodeKind::kBus:
        case NodeKind::kDelay:
        case NodeKind::kLimiter:
        case NodeKind::kOutput:
            return kStereoChannels;
        case NodeKind::kPlugin:
            return spec.plugin_layout.output_channels;
    }
    return 1;
}

std::size_t input_channels_of(const NodeSpec& spec) noexcept {
    if (spec.kind == NodeKind::kPlugin) {
        return spec.plugin_layout.input_channels;
    }
    return output_channels_of(spec);
}

common::Result<GraphPlan, GraphError> plan_graph(const GraphDescription& description,
                                                 std::size_t fader_capacity,
                                                 std::size_t meter_capacity) {
    const std::vector<NodeSpec>& specs = description.nodes;

    // ノードの検証と、NodeId から記述上の位置への対応づけ。
    std::map<NodeId, std::size_t> index_of;
    std::set<FaderId> used_faders;
    std::set<MeterId> used_meters;
    std::set<std::size_t> used_device_channels;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const NodeSpec& spec = specs[i];
        if (!index_of.emplace(spec.id, i).second) {
            return make_unexpected(GraphError::kDuplicateNodeId);
        }
        if (const auto error = validate_node(spec, fader_capacity, meter_capacity)) {
            return make_unexpected(*error);
        }
        if (const auto error = claim_fader(spec.fader, fader_capacity, used_faders)) {
            return make_unexpected(*error);
        }
        if (spec.meter && !used_meters.insert(*spec.meter).second) {
            return make_unexpected(GraphError::kDuplicateMeter);
        }
        // 2 つの出力ノードが同じチャンネルに書くと、後に書いた方だけが聞こえて結線と食い違う。
        if (spec.kind == NodeKind::kOutput) {
            for (std::size_t ch = 0; ch < kStereoChannels; ++ch) {
                if (!used_device_channels.insert(spec.device_channel + ch).second) {
                    return make_unexpected(GraphError::kOverlappingOutputs);
                }
            }
        }
    }

    GraphPlan plan;
    if (const auto error = plan_midi(description, index_of, plan)) {
        return make_unexpected(*error);
    }

    // 結線の検証。
    plan.inputs_of.resize(specs.size());
    plan.has_pre_fader_send.assign(specs.size(), false);
    plan.from_of.reserve(description.connections.size());
    std::vector<std::vector<std::size_t>> same_block_outputs(specs.size());
    std::vector<std::size_t> same_block_inputs(specs.size(), 0);
    std::set<std::pair<std::size_t, std::size_t>> seen_connections;
    for (std::size_t c = 0; c < description.connections.size(); ++c) {
        const Connection& connection = description.connections[c];
        const auto from = index_of.find(connection.from);
        const auto to = index_of.find(connection.to);
        if (from == index_of.end() || to == index_of.end()) {
            return make_unexpected(GraphError::kUnknownNode);
        }
        const std::size_t from_index = from->second;
        const std::size_t to_index = to->second;
        if (const auto error =
                validate_connection(connection, specs[from_index], specs[to_index])) {
            return make_unexpected(*error);
        }
        // 同じ結線が重なると、意図せず 2 倍（+6 dB）で加算される。
        if (!seen_connections.emplace(from_index, to_index).second) {
            return make_unexpected(GraphError::kInvalidConnection);
        }
        if (const auto error = claim_fader(connection.level, fader_capacity, used_faders)) {
            return make_unexpected(*error);
        }
        plan.inputs_of[to_index].push_back(c);
        plan.from_of.push_back(from_index);
        if (connection.tap == SendTap::kPreFader) {
            plan.has_pre_fader_send[from_index] = true;
        }
        // 遅延ノードは前のブロックまでの入力から出力を作るので、遅延ノードへ入る結線は
        // 同じブロックの処理順を縛らない。これによって遅延ノードを通るループも処理順が決まる。
        if (specs[to_index].kind != NodeKind::kDelay) {
            same_block_outputs[from_index].push_back(to_index);
            ++same_block_inputs[to_index];
        }
    }

    // ループの検証。遅延のないループは承認があっても拒否し、遅延ノードを通るループは
    // オペレーターの承認があるときだけ受け付ける。
    const FeedbackReport feedback = analyze_feedback(description);
    if (!feedback.all_loops_delayed()) {
        return make_unexpected(GraphError::kCycle);
    }
    if (feedback.has_loops() && !description.feedback_approved) {
        return make_unexpected(GraphError::kUnapprovedFeedback);
    }

    // Kahn のアルゴリズムで処理順を決める。遅延のないループは上で拒否したので、必ず全ノードが並ぶ。
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (same_block_inputs[i] == 0) {
            ready.push_back(i);
        }
    }
    plan.order.reserve(specs.size());
    while (!ready.empty()) {
        const std::size_t current = ready.back();
        ready.pop_back();
        plan.order.push_back(current);
        for (const std::size_t next : same_block_outputs[current]) {
            if (--same_block_inputs[next] == 0) {
                ready.push_back(next);
            }
        }
    }
    if (plan.order.size() != specs.size()) {
        return make_unexpected(GraphError::kCycle);
    }

    auto latency = compensate_latency(description, plan.order, plan.inputs_of, plan.from_of);
    if (!latency) {
        return make_unexpected(latency.error());
    }
    plan.latency = std::move(latency).value();
    return plan;
}

}  // namespace vocroz::audio_core

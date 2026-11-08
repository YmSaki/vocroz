// SPDX-License-Identifier: AGPL-3.0-only
//
// RenderGraph の構築（検証とトポロジカルソート）と処理の実装。

#include "render_graph.h"

#include <algorithm>
#include <map>
#include <set>
#include <span>
#include <utility>

#include "vocroz/audio_core/constants.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// ノードの種類ごとの出力チャンネル数。モノラルの入力（ソース）とストリップは 1、
// バスから後段は Master をステレオで出力するため 2。
std::size_t channel_count_of(NodeKind kind) noexcept {
    switch (kind) {
        case NodeKind::kSource:
        case NodeKind::kStrip:
            return 1;
        case NodeKind::kBus:
        case NodeKind::kLimiter:
        case NodeKind::kOutput:
            return kStereoChannels;
    }
    return 1;
}

// ノード単体の記述を検証する。
std::optional<GraphError> validate_node(const NodeSpec& spec, std::size_t fader_capacity,
                                        std::size_t meter_capacity) {
    if (spec.kind == NodeKind::kSource && !spec.source) {
        return GraphError::kMissingSource;
    }
    if ((spec.kind == NodeKind::kStrip || spec.kind == NodeKind::kBus) && !spec.fader) {
        return GraphError::kMissingFader;
    }
    if (spec.fader && spec.fader->value >= fader_capacity) {
        return GraphError::kFaderOutOfRange;
    }
    if (spec.meter && spec.meter->value >= meter_capacity) {
        return GraphError::kMeterOutOfRange;
    }
    return std::nullopt;
}

}  // namespace

common::Result<std::unique_ptr<RenderGraph>, GraphError> RenderGraph::build(
    const GraphDescription& description, std::size_t fader_capacity, std::size_t meter_capacity) {
    const std::vector<NodeSpec>& specs = description.nodes;

    // ノードの検証と、NodeId から記述上の位置への対応づけ。
    std::map<NodeId, std::size_t> index_of;
    std::set<FaderId> used_faders;
    std::set<MeterId> used_meters;
    std::size_t output_count = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const NodeSpec& spec = specs[i];
        if (!index_of.emplace(spec.id, i).second) {
            return make_unexpected(GraphError::kDuplicateNodeId);
        }
        if (const auto error = validate_node(spec, fader_capacity, meter_capacity)) {
            return make_unexpected(*error);
        }
        // 同じフェーダーを 2 つのノードが使うと、1 ブロックでランプが 2 回進んでしまう。
        if (spec.fader && !used_faders.insert(*spec.fader).second) {
            return make_unexpected(GraphError::kDuplicateFader);
        }
        if (spec.meter && !used_meters.insert(*spec.meter).second) {
            return make_unexpected(GraphError::kDuplicateMeter);
        }
        if (spec.kind == NodeKind::kOutput && ++output_count > 1) {
            return make_unexpected(GraphError::kTooManyOutputs);
        }
    }

    // 結線の検証と、記述上の位置での入力リスト。
    std::vector<std::vector<std::size_t>> inputs_of(specs.size());
    std::vector<std::vector<std::size_t>> outputs_of(specs.size());
    std::set<std::pair<std::size_t, std::size_t>> seen_connections;
    for (const Connection& connection : description.connections) {
        const auto from = index_of.find(connection.from);
        const auto to = index_of.find(connection.to);
        if (from == index_of.end() || to == index_of.end()) {
            return make_unexpected(GraphError::kUnknownNode);
        }
        const std::size_t from_index = from->second;
        const std::size_t to_index = to->second;
        if (from_index == to_index) {
            return make_unexpected(GraphError::kCycle);
        }
        const NodeKind from_kind = specs[from_index].kind;
        const NodeKind to_kind = specs[to_index].kind;
        // 同じ結線が重なると、意図せず 2 倍（+6 dB）で加算される。
        if (from_kind == NodeKind::kOutput || to_kind == NodeKind::kSource ||
            !seen_connections.emplace(from_index, to_index).second) {
            return make_unexpected(GraphError::kInvalidConnection);
        }
        // モノラルからステレオへは両チャンネルに複製するが、ステレオからモノラルへの
        // ダウンミックスの規則は決めていないので受け付けない。
        if (channel_count_of(from_kind) > channel_count_of(to_kind)) {
            return make_unexpected(GraphError::kChannelMismatch);
        }
        inputs_of[to_index].push_back(from_index);
        outputs_of[from_index].push_back(to_index);
    }

    // Kahn のアルゴリズムで処理順を決める。全ノードを並べられなければループがある。
    std::vector<std::size_t> pending_inputs(specs.size());
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        pending_inputs[i] = inputs_of[i].size();
        if (pending_inputs[i] == 0) {
            ready.push_back(i);
        }
    }
    std::vector<std::size_t> order;
    order.reserve(specs.size());
    while (!ready.empty()) {
        const std::size_t current = ready.back();
        ready.pop_back();
        order.push_back(current);
        for (const std::size_t next : outputs_of[current]) {
            if (--pending_inputs[next] == 0) {
                ready.push_back(next);
            }
        }
    }
    if (order.size() != specs.size()) {
        return make_unexpected(GraphError::kCycle);
    }

    // バッファの確保と、処理順に並べたノードの構築。
    std::unique_ptr<RenderGraph> graph(new RenderGraph());
    std::size_t total_channels = 0;
    for (const NodeSpec& spec : specs) {
        total_channels += channel_count_of(spec.kind);
    }
    graph->storage_.assign(total_channels * kBlockSize, 0.0F);

    std::vector<std::size_t> position_of(specs.size());
    graph->nodes_.resize(specs.size());
    float* next_buffer = graph->storage_.data();
    for (std::size_t position = 0; position < order.size(); ++position) {
        const std::size_t spec_index = order[position];
        position_of[spec_index] = position;
        const NodeSpec& spec = specs[spec_index];
        Node& node = graph->nodes_[position];
        node.kind = spec.kind;
        node.source = spec.source;
        node.fader = spec.fader;
        node.meter = spec.meter;
        node.channel_count = channel_count_of(spec.kind);
        for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
            node.channels[ch] = next_buffer;
            next_buffer += kBlockSize;
        }
    }
    for (std::size_t spec_index = 0; spec_index < specs.size(); ++spec_index) {
        Node& node = graph->nodes_[position_of[spec_index]];
        for (const std::size_t input : inputs_of[spec_index]) {
            node.inputs.push_back(position_of[input]);
        }
        if (node.kind == NodeKind::kOutput) {
            graph->output_ = node.channels.data();
        }
    }
    return graph;
}

void RenderGraph::mix_inputs(Node& node, std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
        std::fill(node.channels[ch], node.channels[ch] + frames, 0.0F);
    }
    for (const std::size_t input_position : node.inputs) {
        const Node& input = nodes_[input_position];
        for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
            // モノラルの入力は、ステレオの全チャンネルへ同じゲインで加える（パンは Beta1 で扱う）。
            const float* source = input.channels[input.channel_count == 1 ? 0 : ch];
            float* destination = node.channels[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                destination[i] += source[i];
            }
        }
    }
}

const float* const* RenderGraph::process(std::size_t frames, FaderBank& faders,
                                         MeterBank& meters) noexcept VOCROZ_NONBLOCKING {
    frames = std::min(frames, kBlockSize);
    for (Node& node : nodes_) {
        float* const* channels = node.channels.data();
        switch (node.kind) {
            case NodeKind::kSource:
                node.source->pull(std::span<float>(node.channels[0], frames));
                break;
            case NodeKind::kStrip:
                mix_inputs(node, frames);
                // チャンネルミュート中も入力を監視できるよう、メーターはフェーダーの前で測る。
                if (node.meter) {
                    meters.measure(*node.meter, channels, node.channel_count, frames);
                }
                faders.process(*node.fader, channels, node.channel_count, frames);
                break;
            case NodeKind::kBus:
                mix_inputs(node, frames);
                faders.process(*node.fader, channels, node.channel_count, frames);
                if (node.meter) {
                    meters.measure(*node.meter, channels, node.channel_count, frames);
                }
                break;
            case NodeKind::kLimiter:
                mix_inputs(node, frames);
                node.limiter.process(channels, node.channel_count, frames);
                break;
            case NodeKind::kOutput:
                mix_inputs(node, frames);
                if (node.meter) {
                    meters.measure(*node.meter, channels, node.channel_count, frames);
                }
                break;
        }
    }
    return output_;
}

}  // namespace vocroz::audio_core

// SPDX-License-Identifier: AGPL-3.0-only
//
// RenderGraph の構築（バッファとディレイラインの確保）と処理の実装。
// 記述の検証と処理順の決定は graph_validator.h が行う。

#include "render_graph.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "vocroz/audio_core/constants.h"

#include "graph_validator.h"

namespace vocroz::audio_core {
namespace {

using common::make_unexpected;

// storage の先頭から channel_count 本のブロック長のバッファを切り出す。
void assign_buffers(std::array<float*, kStereoChannels>& channels, std::size_t channel_count,
                    float*& next_buffer) noexcept {
    for (std::size_t ch = 0; ch < channel_count; ++ch) {
        channels[ch] = next_buffer;
        next_buffer += kBlockSize;
    }
}

// 出力のすべてのサンプルが有限（NaN でも無限大でもない）かを返す。
bool all_finite(const std::array<float*, kStereoChannels>& channels, std::size_t channel_count,
                std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < channel_count; ++ch) {
        for (std::size_t i = 0; i < frames; ++i) {
            if (!std::isfinite(channels[ch][i])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

common::Result<std::unique_ptr<RenderGraph>, GraphError> RenderGraph::build(
    const GraphDescription& description, std::size_t fader_capacity, std::size_t meter_capacity) {
    auto planned = plan_graph(description, fader_capacity, meter_capacity);
    if (!planned) {
        return make_unexpected(planned.error());
    }
    const GraphPlan& plan = planned.value();
    const std::vector<NodeSpec>& specs = description.nodes;

    // バッファの確保。ノードの出力、プリフェーダーの信号、センドレベルの作業領域。
    std::unique_ptr<RenderGraph> graph(new RenderGraph());
    std::size_t total_channels = kStereoChannels;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const std::size_t outputs = output_channels_of(specs[i]);
        const std::size_t buffers = std::max(outputs, input_channels_of(specs[i]));
        total_channels += buffers + (plan.has_pre_fader_send[i] ? outputs : 0);
    }
    graph->storage_.assign(total_channels * kBlockSize, 0.0F);
    float* next_buffer = graph->storage_.data();
    assign_buffers(graph->scratch_, kStereoChannels, next_buffer);

    // 処理順に並べたノードの構築。
    std::vector<std::size_t> position_of(specs.size());
    graph->nodes_.resize(specs.size());
    for (std::size_t position = 0; position < plan.order.size(); ++position) {
        const std::size_t spec_index = plan.order[position];
        position_of[spec_index] = position;
        const NodeSpec& spec = specs[spec_index];
        Node& node = graph->nodes_[position];
        node.kind = spec.kind;
        node.source = spec.source;
        node.fader = spec.fader;
        node.meter = spec.meter;
        node.channel_count = output_channels_of(spec);
        node.input_channel_count = input_channels_of(spec);
        node.buffer_channel_count = std::max(node.channel_count, node.input_channel_count);
        node.plugin = spec.plugin;
        node.recording = spec.recording;
        node.playback = spec.playback;
        assign_buffers(node.channels, node.buffer_channel_count, next_buffer);
        if (plan.has_pre_fader_send[spec_index]) {
            assign_buffers(node.pre_fader, node.channel_count, next_buffer);
        }
        if (spec.kind == NodeKind::kStrip && spec.delay_samples > 0) {
            for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
                node.delays.emplace_back(spec.delay_samples);
            }
        }
        if (spec.kind == NodeKind::kDelay) {
            for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
                node.delays.emplace_back(spec.delay_samples);
            }
            graph->delay_nodes_.push_back(position);
        }
        if (spec.kind == NodeKind::kOutput) {
            graph->outputs_.push_back({spec.device_channel, node.channels[0], node.channels[1]});
        }
    }
    for (std::size_t spec_index = 0; spec_index < specs.size(); ++spec_index) {
        Node& node = graph->nodes_[position_of[spec_index]];
        for (const std::size_t c : plan.inputs_of[spec_index]) {
            const Connection& connection = description.connections[c];
            Input input;
            input.position = position_of[plan.from_of[c]];
            input.pre_fader = connection.tap == SendTap::kPreFader;
            input.level = connection.level;
            const std::size_t compensation = plan.latency.compensation_samples[c];
            if (compensation > 0) {
                const std::size_t channels = output_channels_of(specs[plan.from_of[c]]);
                for (std::size_t ch = 0; ch < channels; ++ch) {
                    input.compensation.emplace_back(compensation);
                }
            }
            node.inputs.push_back(std::move(input));
        }
    }
    graph->midi_.build(description, plan, position_of);
    return graph;
}

void RenderGraph::mix_inputs(Node& node, std::size_t frames,
                             FaderBank& faders) noexcept VOCROZ_NONBLOCKING {
    for (std::size_t ch = 0; ch < node.buffer_channel_count; ++ch) {
        std::fill(node.channels[ch], node.channels[ch] + frames, 0.0F);
    }
    for (Input& input : node.inputs) {
        const Node& from = nodes_[input.position];
        const float* const* source = input.pre_fader ? from.pre_fader.data() : from.channels.data();
        if (input.level || !input.compensation.empty()) {
            // 送り元のバッファは他の結線も読む。
            // そのため、補正の遅延とセンドレベルは作業領域に写してから掛ける。
            for (std::size_t ch = 0; ch < from.channel_count; ++ch) {
                std::copy(source[ch], source[ch] + frames, scratch_[ch]);
            }
            for (std::size_t ch = 0; ch < input.compensation.size(); ++ch) {
                input.compensation[ch].process(std::span<float>(scratch_[ch], frames));
            }
            if (input.level) {
                faders.process(*input.level, scratch_.data(), from.channel_count, frames);
            }
            source = scratch_.data();
        }
        for (std::size_t ch = 0; ch < node.input_channel_count; ++ch) {
            // モノラルの入力は、ステレオの全チャンネルへ同じゲインで加える（パンは扱わない）。
            const float* samples = source[from.channel_count == 1 ? 0 : ch];
            float* destination = node.channels[ch];
            for (std::size_t i = 0; i < frames; ++i) {
                destination[i] += samples[i];
            }
        }
    }
}

void RenderGraph::keep_pre_fader(Node& node, std::size_t frames) noexcept VOCROZ_NONBLOCKING {
    if (node.pre_fader[0] == nullptr) {
        return;
    }
    for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
        std::copy(node.channels[ch], node.channels[ch] + frames, node.pre_fader[ch]);
    }
}

void RenderGraph::process_plugin(Node& node, std::size_t frames,
                                 std::span<const MidiEvent> midi) noexcept VOCROZ_NONBLOCKING {
    if (!node.plugin_failed) {
        PluginProcessContext context;
        context.channels = std::span<float* const>(node.channels.data(), node.buffer_channel_count);
        context.frames = frames;
        context.midi = midi;
        const bool ok = node.plugin->process(context) == PluginProcessStatus::kOk;
        // NaN や無限大は、後段のバスとリミッターを通っても元に戻らない。
        // 出力全体を壊すので、失敗と同じに扱う。
        node.plugin_failed = !ok || !all_finite(node.channels, node.channel_count, frames);
    }
    if (node.plugin_failed) {
        // フェイルセーフ: 処理できなかったプラグインは素通しにせず無音にする（AGENTS.md 1.2）。
        // 素通しにすると、プラグインで下げていた音量（コンプレッサーやゲイン）が突然戻るため。
        for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
            std::fill(node.channels[ch], node.channels[ch] + frames, 0.0F);
        }
    }
}

void RenderGraph::process_node(std::size_t position, std::size_t frames, FaderBank& faders,
                               MeterBank& meters) noexcept VOCROZ_NONBLOCKING {
    Node& node = nodes_[position];
    float* const* channels = node.channels.data();
    switch (node.kind) {
        case NodeKind::kSource:
            node.source->pull(channels, frames);
            break;
        case NodeKind::kStrip:
            mix_inputs(node, frames, faders);
            for (std::size_t ch = 0; ch < node.delays.size(); ++ch) {
                node.delays[ch].process(std::span<float>(node.channels[ch], frames));
            }
            keep_pre_fader(node, frames);
            // チャンネルミュート中も入力を監視できるよう、メーターはフェーダーの前で測る。
            if (node.meter) {
                meters.measure(*node.meter, channels, node.channel_count, frames);
            }
            faders.process(*node.fader, channels, node.channel_count, frames);
            break;
        case NodeKind::kBus:
            mix_inputs(node, frames, faders);
            keep_pre_fader(node, frames);
            faders.process(*node.fader, channels, node.channel_count, frames);
            if (node.meter) {
                meters.measure(*node.meter, channels, node.channel_count, frames);
            }
            break;
        case NodeKind::kDelay:
            // 出力は前のブロックまでの入力から作る。入力の書き込みは全ノードの処理後に行う。
            for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
                node.delays[ch].read(std::span<float>(node.channels[ch], frames));
            }
            break;
        case NodeKind::kLimiter:
            mix_inputs(node, frames, faders);
            node.limiter.process(channels, node.channel_count, frames);
            break;
        case NodeKind::kOutput:
            mix_inputs(node, frames, faders);
            if (node.meter) {
                meters.measure(*node.meter, channels, node.channel_count, frames);
            }
            break;
        case NodeKind::kPlugin:
            mix_inputs(node, frames, faders);
            // 失敗したプラグインにも経路の処理は続ける。発音中のノートの追跡を、届いたイベントと
            // 食い違わせないため。
            process_plugin(node, frames, midi_.events_for(position));
            break;
        case NodeKind::kRecordTap:
            mix_inputs(node, frames, faders);
            node.recording->push(channels, frames);
            break;
        case NodeKind::kPlayer:
            node.playback->render(channels, frames);
            break;
    }
}

std::span<const OutputBlock> RenderGraph::process(
    std::size_t frames, FaderBank& faders, MeterBank& meters,
    const MidiBlockClock& midi_clock) noexcept VOCROZ_NONBLOCKING {
    frames = std::min(frames, kBlockSize);
    midi_.pull_sources(midi_clock, frames);
    for (std::size_t position = 0; position < nodes_.size(); ++position) {
        process_node(position, frames, faders, meters);
    }
    // 遅延ノードの入力（ループで戻ってくる信号を含む）は、このブロックの処理がすべて終わってから
    // 決まるので、最後にまとめて書き込む。遅延は 1 ブロック以上なので、書いたサンプルは
    // 次のブロック以降の read() で読まれる。
    for (const std::size_t position : delay_nodes_) {
        Node& node = nodes_[position];
        mix_inputs(node, frames, faders);
        for (std::size_t ch = 0; ch < node.channel_count; ++ch) {
            node.delays[ch].write(std::span<const float>(node.channels[ch], frames));
        }
    }
    return outputs_;
}

void RenderGraph::adopt_midi_state(const RenderGraph& previous) noexcept VOCROZ_NONBLOCKING {
    midi_.adopt(previous.midi_);
}

}  // namespace vocroz::audio_core

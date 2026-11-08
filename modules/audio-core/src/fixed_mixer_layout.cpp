// SPDX-License-Identifier: AGPL-3.0-only
//
// FixedMixerLayout の実装。

#include "vocroz/audio_core/fixed_mixer_layout.h"

#include <algorithm>
#include <cstdint>

namespace vocroz::audio_core {
namespace {

// ノードの識別子の割り当て。ストリップ、ソース、Master 系の順に並べ、重ならないようにする。
NodeId strip_node(std::size_t channel) noexcept {
    return NodeId{static_cast<std::uint32_t>(channel)};
}

NodeId source_node(std::size_t channel_count, std::size_t channel) noexcept {
    return NodeId{static_cast<std::uint32_t>(channel_count + channel)};
}

NodeId master_node(std::size_t channel_count, std::size_t offset) noexcept {
    return NodeId{static_cast<std::uint32_t>((2 * channel_count) + offset)};
}

}  // namespace

FixedMixerLayout::FixedMixerLayout(std::size_t channel_count) noexcept
    : channel_count_(std::max<std::size_t>(channel_count, 1)) {}

FaderId FixedMixerLayout::channel_fader(std::size_t channel) const noexcept {
    return FaderId{static_cast<std::uint16_t>(channel)};
}

MeterId FixedMixerLayout::channel_meter(std::size_t channel) const noexcept {
    return MeterId{static_cast<std::uint16_t>(channel)};
}

FaderId FixedMixerLayout::master_fader() const noexcept {
    return FaderId{static_cast<std::uint16_t>(channel_count_)};
}

MeterId FixedMixerLayout::master_meter() const noexcept {
    return MeterId{static_cast<std::uint16_t>(channel_count_)};
}

GraphDescription FixedMixerLayout::describe(
    std::span<const std::shared_ptr<AudioSource>> sources) const {
    GraphDescription graph;
    const NodeId master = master_node(channel_count_, 0);
    const NodeId limiter = master_node(channel_count_, 1);
    const NodeId output = master_node(channel_count_, 2);

    for (std::size_t ch = 0; ch < channel_count_; ++ch) {
        graph.nodes.push_back(
            NodeSpec::make_strip(strip_node(ch), channel_fader(ch), channel_meter(ch)));
        graph.connections.push_back({strip_node(ch), master});
        if (ch < sources.size() && sources[ch]) {
            graph.nodes.push_back(
                NodeSpec::make_source(source_node(channel_count_, ch), sources[ch]));
            graph.connections.push_back({source_node(channel_count_, ch), strip_node(ch)});
        }
    }
    // Master のメーターはリミッターの後の最終出力で測るので、バスにはメーターを付けない。
    graph.nodes.push_back(NodeSpec::make_bus(master, master_fader(), std::nullopt));
    graph.nodes.push_back(NodeSpec::make_limiter(limiter));
    graph.nodes.push_back(NodeSpec::make_output(output, master_meter()));
    graph.connections.push_back({master, limiter});
    graph.connections.push_back({limiter, output});
    return graph;
}

}  // namespace vocroz::audio_core

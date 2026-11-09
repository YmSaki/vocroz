// SPDX-License-Identifier: AGPL-3.0-only
//
// mixer_graph_builder.h の実装。

#include "mixer_graph_builder.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "vocroz/session/routing_delay.h"

namespace vocroz::engine {
namespace {

namespace ac = audio_core;
namespace cp = control_protocol;

/// メインの出力（Master の出力先）が書く先頭のチャンネル。
constexpr std::size_t kMainOutputChannel = 0;

/// ノードの識別子を振りながらグラフを組み立てる。
class GraphWriter {
public:
    ac::NodeId add(ac::NodeSpec spec) {
        spec.id = ac::NodeId{next_id_++};
        const ac::NodeId id = spec.id;
        graph_.nodes.push_back(std::move(spec));
        return id;
    }

    void connect(ac::NodeId from, ac::NodeId to, ac::SendTap tap = ac::SendTap::kPostFader,
                 std::optional<ac::FaderId> level = std::nullopt) {
        graph_.connections.emplace_back(from, to, tap, level);
    }

    ac::GraphDescription take(bool feedback_approved) {
        graph_.feedback_approved = feedback_approved;
        return std::move(graph_);
    }

private:
    ac::GraphDescription graph_;
    std::uint32_t next_id_ = 0;
};

/// センドの送り元と送り先として引ける、entity_id ごとのノード。
/// 追加の出力は、送り先として引くと出力の手前の安全リミッターになる。
using NodeTable = std::map<std::string, ac::NodeId, std::less<>>;

[[nodiscard]] ac::SendTap to_graph_tap(cp::SendTap tap) noexcept {
    switch (tap) {
        case cp::SendTap::kPostFader:
            return ac::SendTap::kPostFader;
        case cp::SendTap::kPreFader:
            return ac::SendTap::kPreFader;
    }
    return ac::SendTap::kPostFader;
}

[[nodiscard]] std::size_t input_delay_of(const cp::RoutingConfig& config,
                                         std::string_view strip_id) {
    for (const auto& delay : config.input_delays) {
        if (delay.strip_id == strip_id) {
            return session::delay_samples(delay.delay_ms);
        }
    }
    return 0;
}

[[nodiscard]] std::optional<ac::NodeId> find(const NodeTable& table, std::string_view id) {
    const auto it = table.find(id);
    if (it == table.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace

BuiltMixerGraph build_mixer_graph(const MixerAssignments& assignments,
                                  std::string_view master_entity_id,
                                  const cp::RoutingConfig& config, bool feedback_approved) {
    GraphWriter writer;
    NodeTable sources_of_sends;
    NodeTable targets_of_sends;

    // Master と、メインの出力への暗黙の経路。安全リミッターを必ず通す。
    const auto master =
        writer.add(ac::NodeSpec::make_bus(ac::NodeId{}, assignments.master_fader, std::nullopt));
    const auto main_limiter = writer.add(ac::NodeSpec::make_limiter(ac::NodeId{}));
    const auto main_output = writer.add(
        ac::NodeSpec::make_output(ac::NodeId{}, assignments.master_meter, kMainOutputChannel));
    writer.connect(master, main_limiter);
    writer.connect(main_limiter, main_output);
    sources_of_sends.emplace(master_entity_id, master);
    targets_of_sends.emplace(master_entity_id, master);

    // ストリップと、ストリップから Master への暗黙の結線。
    for (const auto& strip : assignments.strips) {
        const auto node = writer.add(ac::NodeSpec::make_strip(
            ac::NodeId{}, strip.fader, strip.meter, input_delay_of(config, strip.strip_id)));
        if (strip.source) {
            const auto source = writer.add(ac::NodeSpec::make_source(ac::NodeId{}, strip.source));
            writer.connect(source, node);
        }
        writer.connect(node, master);
        sources_of_sends.emplace(strip.strip_id, node);
    }

    for (const auto& bus : config.buses) {
        const auto fader = assignments.route_faders.find(bus.entity_id);
        if (fader == assignments.route_faders.end()) {
            continue;
        }
        const auto meter = assignments.bus_meters.find(bus.entity_id);
        const auto node = writer.add(ac::NodeSpec::make_bus(
            ac::NodeId{}, fader->second,
            meter == assignments.bus_meters.end() ? std::nullopt
                                                  : std::optional<ac::MeterId>{meter->second}));
        sources_of_sends.emplace(bus.entity_id, node);
        targets_of_sends.emplace(bus.entity_id, node);
    }

    // 追加の出力にも安全リミッターを通す。キューの返しも出演者の耳に届く出力なので、
    // メインの出力と同じく過大な音を出さない。
    for (const auto& output : config.outputs) {
        const auto limiter = writer.add(ac::NodeSpec::make_limiter(ac::NodeId{}));
        const auto node = writer.add(ac::NodeSpec::make_output(
            ac::NodeId{}, std::nullopt, static_cast<std::size_t>(output.device_channel)));
        writer.connect(limiter, node);
        targets_of_sends.emplace(output.entity_id, limiter);
    }

    for (const auto& delay : config.delays) {
        const auto node = writer.add(
            ac::NodeSpec::make_delay(ac::NodeId{}, session::delay_samples(delay.delay_ms)));
        sources_of_sends.emplace(delay.entity_id, node);
        targets_of_sends.emplace(delay.entity_id, node);
    }

    BuiltMixerGraph built;
    for (const auto& send : config.sends) {
        const auto from = find(sources_of_sends, send.from);
        const auto to = find(targets_of_sends, send.to);
        const auto level = assignments.route_faders.find(send.entity_id);
        if (!from || !to || level == assignments.route_faders.end()) {
            built.dropped_sends.push_back(send.entity_id);
            continue;
        }
        // センドには必ずセンドレベルを付ける。新しいセンドはミュートで始まり（初期ミュート）、
        // プリフェーダーのセンドは送り元のミュートでは止まらないため。
        writer.connect(*from, *to, to_graph_tap(send.tap), level->second);
    }
    built.graph = writer.take(feedback_approved);
    return built;
}

}  // namespace vocroz::engine

// SPDX-License-Identifier: AGPL-3.0-only
//
// mixer_graph_builder.h の実装。

#include "mixer_graph_builder.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>

#include "vocroz/audio_core/minus_me.h"
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

/// 挿す先 1 つの挿入をつないだ列。
struct InsertChain {
    /// 列の先頭と末尾のプラグインノード。挿入がないか、経路を切ったなら std::nullopt。
    std::optional<ac::NodeId> first;
    std::optional<ac::NodeId> last;
    /// 読み込みの済んでいない挿入があり、挿す先を通る経路を切る。
    bool cut = false;
    /// 末尾のプラグインの出力のチャンネル数。
    std::size_t output_channels = 0;
};

// 挿す先の挿入を、構成の順にプラグインノードとして直列につなぐ。1 つでも読み込みが済んで
// いなければノードを置かずに cut を返す。一部のプラグインだけを通した音も、オペレーターが
// 意図した音ではないため。
InsertChain add_insert_chain(GraphWriter& writer, const cp::RoutingConfig& config,
                             std::string_view target,
                             std::span<const session::InsertedPlugin> plugins) {
    std::vector<const session::InsertedPlugin*> chain;
    for (const auto& insert : config.inserts) {
        if (insert.target != target) {
            continue;
        }
        const auto it = std::ranges::find_if(plugins, [&](const session::InsertedPlugin& plugin) {
            return plugin.entity_id == insert.entity_id;
        });
        if (it == plugins.end() || !it->processor) {
            return InsertChain{std::nullopt, std::nullopt, true, 0};
        }
        chain.push_back(&*it);
    }
    InsertChain result;
    for (const auto* plugin : chain) {
        const auto node =
            writer.add(ac::NodeSpec::make_plugin(ac::NodeId{}, plugin->processor, plugin->fault));
        if (result.last) {
            writer.connect(*result.last, node);
        } else {
            result.first = node;
        }
        result.last = node;
        result.output_channels = plugin->processor->layout().output_channels;
    }
    return result;
}

/// Master かバス 1 つの、挿入とフェーダーをつないだ段。
struct BusStage {
    /// 前段（ストリップ、センド）の送り先。
    ac::NodeId input;
    /// 後段（センド、リミッター、出力、外部出力）の送り元。
    ac::NodeId output;
    /// 経路を切ったため、output がバスではなく入力のない無音の段である。
    bool cut = false;
};

// Master かバスの段を組み立てる。挿入があれば「前段 → 挿入の列 → バス → 後段」とつなぎ、
// フェーダーとミュートを挿入の後ろに置く。ミュートしたあとに、プラグインに残った残響や
// 遅延の音、無音の入力からプラグインが作る音を後段へ流さないため（初期ミュートとフェイルセーフ）。
// 前段の結線は先頭のプラグインが加算して受ける。
// 経路を切るなら、前段はバスで受けて行き止まりにし、入力のない安全リミッター（常に無音）を
// 後段の送り元にする。センドをグラフから外すと、センドのフェーダーをミュートすることになり、
// session の状態とずれるため。
BusStage add_bus_stage(GraphWriter& writer, ac::NodeId bus, const InsertChain& chain) {
    if (chain.cut) {
        return BusStage{bus, writer.add(ac::NodeSpec::make_limiter(ac::NodeId{})), true};
    }
    if (!chain.first) {
        return BusStage{bus, bus};
    }
    writer.connect(*chain.last, bus);
    return BusStage{*chain.first, bus};
}

// 子機への返送を足す。返送の候補は Master へ入る信号（ストリップ、Master へ送るバスと
// 遅延ノード）で、Master へ入るセンドのミュートと音量も含めて親機のメインのミックスと同じ
// バランスにする。Master のフェーダーとミュートは通さない。Master はハウスの出力の操作で、
// 出演者のモニターまで止めるものではないため。
// その子機の声を含む候補は add_minus_me_returns() が除く。
void add_returns(ac::GraphDescription& graph, ac::NodeId master_input,
                 const std::vector<ac::MinusMeReturn>& returns) {
    if (returns.empty()) {
        return;
    }
    // 取り出し位置とセンドレベルも渡す。送り元だけを渡すと、Master へのセンドのミュートと
    // 音量が返送に効かなくなる。
    std::vector<ac::MinusMeInput> mix_inputs;
    for (const auto& connection : graph.connections) {
        if (connection.to == master_input) {
            mix_inputs.emplace_back(connection.from, connection.tap, connection.level);
        }
    }
    // 候補と返送のノードはすべてこのグラフのものなので失敗しない。万一失敗したら返送を足さない
    // （add_minus_me_returns() は失敗したらグラフを変えない）。音の経路を増やさない側に倒す。
    (void)ac::add_minus_me_returns(graph, mix_inputs, returns);
}

}  // namespace

std::string strip_midi_key(std::string_view strip_id) {
    return "strip:" + std::string(strip_id);
}

std::string device_midi_key(std::string_view device_id) {
    return "device:" + std::string(device_id);
}

BuiltMixerGraph build_mixer_graph(const MixerAssignments& assignments,
                                  std::string_view master_entity_id,
                                  const cp::RoutingConfig& config, bool feedback_approved,
                                  std::span<const session::InsertedPlugin> plugins) {
    GraphWriter writer;
    NodeTable sources_of_sends;
    NodeTable targets_of_sends;
    // 経路を切った段の、後段の送り元（入力のない無音の段）。
    std::vector<ac::NodeId> silent_stages;

    // Master と、メインの出力への暗黙の経路。安全リミッターを必ず通す。
    const auto master_chain = add_insert_chain(writer, config, master_entity_id, plugins);
    const auto master = add_bus_stage(
        writer,
        writer.add(ac::NodeSpec::make_bus(ac::NodeId{}, assignments.master_fader, std::nullopt)),
        master_chain);
    const auto main_limiter = writer.add(ac::NodeSpec::make_limiter(ac::NodeId{}));
    const auto main_output = writer.add(
        ac::NodeSpec::make_output(ac::NodeId{}, assignments.master_meter, kMainOutputChannel));
    writer.connect(master.output, main_limiter);
    writer.connect(main_limiter, main_output);
    sources_of_sends.emplace(master_entity_id, master.output);
    if (master.cut) {
        silent_stages.push_back(master.output);
    }
    targets_of_sends.emplace(master_entity_id, master.input);

    // ストリップと、ストリップから Master への暗黙の結線。
    for (const auto& strip : assignments.strips) {
        // ストリップのチャンネル数は受信ストリームに合わせる。子機のステレオの入力を 1 本の
        // ストリップで左右を保ったまま Master
        // へ送るため（docs/adr/0035-client-input-expansion.md）。
        // ストリームがまだなければモノラルで置く（無音なので違いはない）。
        // 挿入があれば、末尾のプラグインの出力に合わせる（モノラルの声にステレオのリバーブなど）。
        const auto chain = add_insert_chain(writer, config, strip.strip_id, plugins);
        const std::size_t channels = chain.last     ? chain.output_channels
                                     : strip.source ? strip.source->channel_count()
                                                    : 1;
        const auto node =
            writer.add(ac::NodeSpec::make_strip(ac::NodeId{}, strip.fader, strip.meter,
                                                input_delay_of(config, strip.strip_id), channels));
        if (chain.last) {
            writer.connect(*chain.last, node);
        }
        if (strip.source) {
            // 経路を切るときもソースのノードは置く。グラフはどのノードもブロックごとに処理するので、
            // 受信ストリームから取り出し続け、つないだときに古い音が溜まっていないようにする。
            const auto source = writer.add(ac::NodeSpec::make_source(ac::NodeId{}, strip.source));
            if (!chain.cut) {
                writer.connect(source, chain.first ? *chain.first : node);
            }
        }
        writer.connect(node, master.input);
        sources_of_sends.emplace(strip.strip_id, node);
    }

    for (const auto& bus : config.buses) {
        const auto fader = assignments.route_faders.find(bus.entity_id);
        if (fader == assignments.route_faders.end()) {
            continue;
        }
        const auto meter = assignments.bus_meters.find(bus.entity_id);
        const auto chain = add_insert_chain(writer, config, bus.entity_id, plugins);
        const auto stage = add_bus_stage(
            writer,
            writer.add(ac::NodeSpec::make_bus(ac::NodeId{}, fader->second,
                                              meter == assignments.bus_meters.end()
                                                  ? std::nullopt
                                                  : std::optional<ac::MeterId>{meter->second})),
            chain);
        sources_of_sends.emplace(bus.entity_id, stage.output);
        if (stage.cut) {
            silent_stages.push_back(stage.output);
        }
        targets_of_sends.emplace(bus.entity_id, stage.input);
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
    std::vector<ac::MinusMeReturn> returns;
    for (const auto& assignment : assignments.returns) {
        ac::MinusMeReturn minus_me{{}, assignment.sink};
        for (const auto& strip_id : assignment.own_strip_ids) {
            // ストリップの削除と返送の付け替えの間の組み立てでは、なくなったストリップを指しうる。
            if (const auto node = find(sources_of_sends, strip_id)) {
                minus_me.own_inputs.push_back(*node);
            }
        }
        returns.push_back(std::move(minus_me));
    }
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
        // 経路を切った段からのプリフェーダーのセンドは、ポストフェーダーとして無音の段から取る。
        // 無音の段はフェーダーを持たないので取り出し位置の違いはなく、プリフェーダーのまま
        // 張るとグラフの検証（プリフェーダーの送り元はストリップかバスだけ）に通らず、
        // 読み込みを待つ間のルーティング全体の適用が失敗するため。
        const auto tap = std::ranges::find(silent_stages, *from) != silent_stages.end()
                             ? ac::SendTap::kPostFader
                             : to_graph_tap(send.tap);
        writer.connect(*from, *to, tap, level->second);
    }
    if (const auto& external = assignments.external_stream; external && external->sink) {
        // 送り出せるのは Master かバスだけ（session が検証する）。ストリップや遅延ノードは
        // 外部へ送る音の単位ではないので、万一指していてもタップを置かない。
        const auto is_bus = external->source_id == master_entity_id ||
                            std::ranges::any_of(config.buses, [&](const cp::RoutingBus& bus) {
                                return bus.entity_id == external->source_id;
                            });
        if (const auto source = find(sources_of_sends, external->source_id); source && is_bus) {
            // 外部の放送機器へも過大な音を送らないよう、メインの出力と同じく安全リミッターを通す。
            const auto limiter = writer.add(ac::NodeSpec::make_limiter(ac::NodeId{}));
            const auto tap =
                writer.add(ac::NodeSpec::make_stream_tap(ac::NodeId{}, external->sink));
            writer.connect(*source, limiter);
            writer.connect(limiter, tap);
        }
    }
    built.graph = writer.take(feedback_approved);
    for (const auto& midi : assignments.midi_sources) {
        built.graph.midi_sources.push_back(ac::MidiSourceSpec{midi.id, midi.source});
    }
    add_returns(built.graph, master.input, returns);
    return built;
}

}  // namespace vocroz::engine

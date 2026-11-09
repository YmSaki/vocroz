// SPDX-License-Identifier: AGPL-3.0-only
//
// レンダーグラフの MIDI の経路。PCM の結線とは独立に、MIDI
// ソースのイベントをプラグインノードへ配る。 ブロックごとに各ソースから 1
// 回だけ取り出し、つながったプラグインごとに、複数ソースのイベントを
// サンプル位置の順に合わせる。経路ごとに発音中のノートを追跡し、ソースの Panic と、グラフの
// 差し替えで外れた経路に Note Off を送る（音残りを防ぐ、docs/adr/0029-midi-and-vsti.md）。
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "vocroz/audio_core/graph_description.h"
#include "vocroz/audio_core/midi_event.h"
#include "vocroz/audio_core/ports/midi_source.h"
#include "vocroz/audio_core/ports/plugin_processor.h"
#include "vocroz/audio_core/realtime.h"

#include "active_notes.h"
#include "graph_validator.h"

namespace vocroz::audio_core {

/// レンダーグラフ 1 つぶんの MIDI の経路。
/// スレッド安全性: build() と破棄はオーディオスレッド外、それ以外はオーディオスレッドだけが呼ぶ。
class MidiRouter {
public:
    /// 検証済みの記述から経路を構築し、イベントの領域を確保する。
    /// @param description グラフの記述（plan_graph() で検証済み）
    /// @param plan 処理計画
    /// @param position_of 記述上のノードの位置から、レンダーグラフの処理順の位置への対応
    void build(const GraphDescription& description, const GraphPlan& plan,
               const std::vector<std::size_t>& position_of);

    /// ブロックの先頭で、各ソースからこのブロックのイベントを取り出す。
    /// @param clock このブロックのサンプル位置と時刻の対応（frames は下の frames で上書きする）
    /// @param frames このブロックのサンプル数
    void pull_sources(const MidiBlockClock& clock, std::size_t frames) noexcept VOCROZ_NONBLOCKING;

    /// プラグインノードへ渡すこのブロックのイベントを作る。pull_sources() の後に、ノードごとに 1
    /// 回呼ぶ。 先頭に Note Off（Panic と外れた経路のぶん）を置き、続けて各ソースのイベントを
    /// サンプル位置の順に並べる（同じ位置はソースの経路の記述順、同じソースの中は届いた順）。
    /// @param node_position レンダーグラフの処理順のノードの位置
    /// @return sample_offset の昇順のイベント。MIDI を受け取らないノードなら空
    [[nodiscard]] std::span<const MidiEvent> events_for(std::size_t node_position) noexcept
        VOCROZ_NONBLOCKING;

    /// 差し替え前のグラフから、発音中のノートを引き継ぐ。グラフを取り込んだ直後、最初の処理の前に呼ぶ。
    /// 同じソースと同じプラグインの経路は状態をそのまま引き継ぎ、外れた経路のノートは、
    /// プラグインがこのグラフにあれば次の events_for() で Note Off を送る。
    /// プラグインごと外れた場合は処理しないので送れない（再挿入の前に session がプラグインを
    /// リセットする）。
    /// @param previous 差し替え前のグラフの経路
    void adopt(const MidiRouter& previous) noexcept VOCROZ_NONBLOCKING;

private:
    struct Source {
        std::shared_ptr<MidiSource> source;
        std::vector<MidiEvent> events;  // kMaxMidiEventsPerBlock
        std::size_t count = 0;
        bool panic = false;
    };

    struct Route {
        std::size_t source = 0;  // sources_ の位置
        ActiveNotes notes;  // この経路でプラグインへ送って、まだ止めていないノート
    };

    struct Destination {
        const PluginProcessor* plugin = nullptr;  // 差し替えでの同一性の判定だけに使う
        std::vector<std::size_t> routes;          // routes_ の位置（記述順）
        std::vector<std::size_t> cursors;         // 合わせるときの各経路の読み位置
        ActiveNotes orphaned;  // 外れた経路から引き継いだ、まだ止めていないノート
        std::vector<MidiEvent> merged;
    };

    std::vector<Source> sources_;
    std::vector<Route> routes_;
    std::vector<Destination> destinations_;
    // 処理順のノードの位置から destinations_ の位置への対応。プラグインノード以外は std::nullopt。
    std::vector<std::optional<std::size_t>> destination_of_;
};

}  // namespace vocroz::audio_core

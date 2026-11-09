// SPDX-License-Identifier: AGPL-3.0-only
//
// session の MixerControl の実装。session が決めた値（チャンネルの追加と削除、ミュート、ゲイン、
// ルーティング）を audio-core の Mixer に渡す。
//
// 責務:
// - ストリップ、Master、バス、センド、メーターへのフェーダーとメーターの識別子の割り当て
// - 変更のたびに mixer_graph_builder でグラフを組み立て、Mixer::apply_graph() で差し替える
// - フェーダーの操作がキューの満杯で失敗したときの再送（順序を保つ）
// - 子機の受信ストリーム（AudioSource）のストリップへの接続（子機の受信の組み立てが使う）
// -
// 子機への返送（Minus-Me）の渡し先（AudioSink）のストリップへの接続（子機への送信の組み立てが使う）
// - MIDI ソース（MIDI デバイス、子機の MIDI）のグラフへの接続。ストリップを削除したら、
//   そのストリップの子機の MIDI も外す
// - UI に送るメーターの値の読み取り
// オーディオスレッドへの受け渡しは Mixer が SPSC キューとグラフのスワップで行う。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/audio_core/ports/audio_sink.h"
#include "vocroz/audio_core/ports/audio_source.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/control_protocol/events.h"
#include "vocroz/session/ports/mixer_control.h"

#include "mixer_graph_builder.h"

namespace vocroz::engine {

/// session の値を audio-core のミキサーへ渡す。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドから呼ぶ。
class EngineMixer final : public session::MixerControl {
public:
    /// @param mixer  ミキサー。MixerConfig の容量は kMixerFaderCapacity と kMixerMeterCapacity
    ///               以上にする。このオブジェクトより長く生存させる
    /// @param logger グラフを適用できなかったときの記録先。このオブジェクトより長く生存させる
    EngineMixer(audio_core::Mixer& mixer, common::Logger& logger);

    /// ストリップを削除する直前に呼ぶ処理を設定する。子機の MIDI の受け口に、そのストリップの
    /// Panic とキューの解放を伝えるのに使う（docs/adr/0029-midi-and-vsti.md）。
    /// @param handler ストリップの entity_id を受け取る処理。空なら何もしない
    void set_strip_removal_handler(std::function<void(const std::string&)> handler);

    // --- session::MixerControl ---
    void add_channel(const std::string& strip_id) override;
    void remove_channel(const std::string& strip_id) override;
    void set_channel_muted(const std::string& strip_id, bool muted) override;
    void set_channel_gain(const std::string& strip_id, control_protocol::GainDb gain_db) override;
    void set_master_muted(bool muted) override;
    void set_master_gain(control_protocol::GainDb gain_db) override;
    [[nodiscard]] bool apply_routing(const control_protocol::RoutingConfig& config,
                                     bool feedback_approved) override;
    void set_route_muted(const std::string& entity_id, bool muted) override;
    void set_route_gain(const std::string& entity_id, control_protocol::GainDb gain_db) override;

    /// ストリップに子機の受信ストリームをつなぐ。ストリップのフェーダーとミュートはそのまま。
    /// @param strip_id ストリップの entity_id
    /// @param source   受信ストリーム（null にしない）
    /// @return ストリップがあり、グラフを差し替えられたら true
    [[nodiscard]] bool attach_source(const std::string& strip_id,
                                     std::shared_ptr<audio_core::AudioSource> source);

    /// ストリップから受信ストリームを外す。ストリップは無音になる。
    /// ストリームの最後の参照は、古いグラフの破棄（collect()）でオーディオスレッド外で外れる。
    /// @param strip_id ストリップの entity_id
    void detach_source(const std::string& strip_id);
    /// 子機への返送（Minus-Me）の渡し先をつなぐ。返送には、その子機のすべてのストリップの声を
    /// 含まない Master へのミックスが流れる（docs/adr/0036-return-monitor.md）。送信を止めるか
    /// どうかは渡し先（MediaStreamEncoder）が返送のミュートで決める。同じキーの返送があれば
    /// 置き換える（子機の入力が増減したときに、自分のストリップの一覧を付け替える）。
    /// @param key           返送を区別するキー（その子機との接続）
    /// @param own_strip_ids その子機のストリップの entity_id（1 本以上）
    /// @param sink          返送の渡し先（null にしない）
    /// @return 引数が正しく、グラフを差し替えられたら true。失敗したら前の状態のまま
    [[nodiscard]] bool attach_return(std::uint64_t key, std::vector<std::string> own_strip_ids,
                                     std::shared_ptr<audio_core::AudioSink> sink);
    /// 返送の渡し先を外す。渡し先の最後の参照は、古いグラフの破棄（collect()）で
    /// オーディオスレッド外で外れる。
    /// @param key attach_return() に渡したキー
    void detach_return(std::uint64_t key);

    /// MIDI ソースをグラフに加える。同じキーのソースがあれば置き換え、識別子は引き継ぐ。
    /// @param key    ソースを区別するキー（MidiSourceAssignment::key）
    /// @param source イベントの取り出し先（null にしない）
    /// @return 引数が正しく、グラフを差し替えられたら true。失敗したら前の状態のまま
    [[nodiscard]] bool attach_midi_source(const std::string& key,
                                          std::shared_ptr<audio_core::MidiSource> source);
    /// MIDI ソースをグラフから外す。発音中のノートは、外した後の最初のブロックで止まる。
    /// ソースの最後の参照は、古いグラフの破棄（collect()）でオーディオスレッド外で外れる。
    /// @param key attach_midi_source() に渡したキー
    void detach_midi_source(const std::string& key);

    /// 周期処理。キューの満杯で失敗したフェーダーの操作を送り直し、古いグラフを破棄する。
    void collect();

    /// UI に送るメーターの値を読む。ピークは読むたびにリセットされる。
    /// @return ストリップと Master のプリフェーダーのピーク（entity_id の昇順）
    [[nodiscard]] std::vector<control_protocol::MeterLevel> read_meters();

private:
    // フェーダーの操作。キューが満杯なら retry_ に積んで、順序を保ったまま送り直す。
    struct FaderOp {
        enum class Kind { kReset, kMute, kGain };
        Kind kind = Kind::kReset;
        audio_core::FaderId fader;
        bool muted = true;
        float gain_db = 0.0F;
    };

    // 識別子の割り当て表。0 から capacity 未満の空いている最小の値を返す。
    class IdPool {
    public:
        explicit IdPool(std::size_t capacity) : used_(capacity, false) {}
        [[nodiscard]] std::optional<std::uint16_t> acquire();
        void release(std::uint16_t id);

    private:
        std::vector<bool> used_;
    };

    [[nodiscard]] StripAssignment* find_strip(std::string_view strip_id);
    // 今の割り当てと構成でグラフを組み立てて差し替える。
    [[nodiscard]] bool rebuild(const MixerAssignments& assignments,
                               const control_protocol::RoutingConfig& config,
                               bool feedback_approved);
    void submit(const FaderOp& op);
    [[nodiscard]] bool try_submit(const FaderOp& op);
    void mute_dropped_sends(const std::vector<std::string>& dropped);
    // バスとセンドのフェーダーとメーターを、構成に合わせて割り当て直す。
    // 構成から消えたものは返し、新しいものは空きから割り当てる。割り当てられなければ false。
    [[nodiscard]] bool assign_routes(const control_protocol::RoutingConfig& config,
                                     MixerAssignments& assignments, IdPool& faders, IdPool& meters,
                                     std::vector<audio_core::FaderId>& added,
                                     std::vector<audio_core::FaderId>& released) const;

    audio_core::Mixer& mixer_;
    common::Logger& logger_;
    IdPool faders_;
    IdPool meters_;
    MixerAssignments assignments_;
    control_protocol::RoutingConfig routing_;
    bool feedback_approved_ = false;
    std::deque<FaderOp> retry_;
    std::function<void(const std::string&)> on_strip_removal_;
    // 次に割り当てる MIDI ソースの識別子。使い回さないので、外したソースの経路の状態を
    // 別のソースが引き継ぐことはない。
    std::uint32_t next_midi_source_ = 0;
};

}  // namespace vocroz::engine

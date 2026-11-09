// SPDX-License-Identifier: AGPL-3.0-only
//
// 結合テストの親機で、session の MixerControl ポートを受けて、ストリップと Master
// のミュートとゲインを 保持し、ストリップごとの PCM を混ぜる。 製品では pa-engine が audio-core
// のミキサーとグラフで実装する位置にあたる。結合テストで確かめたいのは
// 「どのストリップがどのミュートとゲインで鳴るか」という session との約束なので、DSP
// は単純な和に留める。
#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>

#include "vocroz/control_protocol/entities.h"
#include "vocroz/session/ports/mixer_control.h"

namespace vocroz::test_harness {

/// 1 本のストリップのミキサー側の状態。
struct HarnessChannel {
    /// チャンネルミュート。追加した時点ではミュート（フェイルセーフ）。
    bool muted = true;
    /// フェーダーの値（nullopt は -∞ dB）。
    control_protocol::GainDb gain_db = 0.0;
};

/// 結合テストのミキサー。
/// スレッド安全性: 1 つのスレッドから使う（テストでは仮想時刻を進めるスレッド）。
class HarnessMixer final : public session::MixerControl {
public:
    void add_channel(const std::string& strip_id) override;
    void remove_channel(const std::string& strip_id) override;
    void set_channel_muted(const std::string& strip_id, bool muted) override;
    void set_channel_gain(const std::string& strip_id, control_protocol::GainDb gain_db) override;
    void set_master_muted(bool muted) override;
    void set_master_gain(control_protocol::GainDb gain_db) override;

    /// ルーティングは結合テストの対象外なので、構成を受け付けるだけで音の経路は変えない
    /// （ストリップから Master への暗黙の経路だけで鳴らす）。
    /// @return 常に true
    [[nodiscard]] bool apply_routing(const control_protocol::RoutingConfig& config,
                                     bool feedback_approved) override;
    /// ルーティングを扱わないので何もしない。
    void set_route_muted(const std::string& entity_id, bool muted) override;
    /// ルーティングを扱わないので何もしない。
    void set_route_gain(const std::string& entity_id, control_protocol::GainDb gain_db) override;

    /// ストリップの PCM を、そのストリップのミュートとゲインを掛けて out に足す。
    /// ミキサーにないストリップは足さない（承認前の接続や削除済みのストリップの音を出さない）。
    /// @param strip_id ストリップ
    /// @param input    ストリップの PCM
    /// @param out      足し込む先。input と同じ長さ
    void accumulate(const std::string& strip_id, std::span<const float> input,
                    std::span<float> out) const;

    /// Master のミュートとゲインを out に掛ける。
    /// @param out Master の PCM
    void apply_master(std::span<float> out) const;

    /// ミキサーにあるストリップ。
    [[nodiscard]] const std::map<std::string, HarnessChannel>& channels() const noexcept {
        return channels_;
    }

    /// Master がミュート中なら true。起動時はミュート（初期ミュート）。
    [[nodiscard]] bool master_muted() const noexcept {
        return master_muted_;
    }

private:
    std::map<std::string, HarnessChannel> channels_;
    bool master_muted_ = true;
    control_protocol::GainDb master_gain_db_ = 0.0;
};

}  // namespace vocroz::test_harness

// SPDX-License-Identifier: AGPL-3.0-only
//
// セッションが保存して次回の起動に引き継ぐ設定の値。
// ミュートの状態は保存しない。起動時と再起動時は必ずミュートから始める製品要件があり、
// 保存した値で音が出る状態に戻ることがないようにするため。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::session {

/// 保存するバスかセンドのフェーダーの値。ミュートは保存しない（復元したバスとセンドはミュートで
/// 始まる）。
struct SavedRouteGain {
    /// バスかセンドの entity_id。
    std::string entity_id;
    /// フェーダー（dB）。
    control_protocol::GainDb gain_db = 0.0;

    friend bool operator==(const SavedRouteGain&, const SavedRouteGain&) = default;
};

/// 親機の設定。
struct HostSettings {
    /// 次に作るセッションの名前。
    std::string session_name;
    /// 最後に使った出力デバイスの永続 ID。抜去時に別のデバイスへ切り替えないため、
    /// 起動時もこの ID のデバイスだけを開く（開けなければ未選択のまま）。
    std::optional<std::string> output_device_id;
    /// Master のフェーダー（dB）。ミュートとは別の値なので保存してよい。
    control_protocol::GainDb master_gain_db = 0.0;
    /// 適用中のルーティングの構成のうち、バス、追加の出力、遅延ノードと、それらの間のセンド。
    /// チャンネルストリップの entity_id はセッションごとに振り直すので、ストリップを参照する
    /// センドと手動ディレイは保存しない（docs/adr/0033-routing-persistence.md）。
    control_protocol::RoutingConfig routing;
    /// routing のバスとセンドのフェーダーの値。
    std::vector<SavedRouteGain> route_gains;

    friend bool operator==(const HostSettings&, const HostSettings&) = default;
};

/// 子機の設定。
struct ClientSettings {
    /// 親機の承認パネルとストリップに出す名前。
    std::string display_name;
    /// 最後に選んだ入力デバイスの永続 ID。std::nullopt は「入力なし」。
    std::optional<std::string> selected_input_id;
    /// 送信レベル（-60〜+12 dB）。
    double send_level_db = 0.0;

    friend bool operator==(const ClientSettings&, const ClientSettings&) = default;
};

/// 設定を読めなかった理由。どの理由でも、セッションは既定値（全ミュート）で起動する。
enum class SettingsError {
    kNotFound,   ///< 保存された設定がない（初回の起動）
    kCorrupted,  ///< 設定が壊れていて、バックアップからも復元できない
    kIoError,    ///< 読み書きに失敗した
};

}  // namespace vocroz::session

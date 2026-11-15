// SPDX-License-Identifier: AGPL-3.0-only
//
// セッションが保存して次回の起動に引き継ぐ設定の値。
// ミュートの状態は保存しない。起動時と再起動時は必ずミュートから始める製品要件があり、
// 保存した値で音が出る状態に戻ることがないようにするため。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::session {

/// 全体の目標レイテンシーの既定値（ms）。合奏向けの低遅延プロファイルの上限
/// （docs/adr/0002-opus-low-delay.md）。
inline constexpr double kDefaultLatencyTargetMs = 20.0;

/// 保存するバスかセンドのフェーダーの値。ミュートは保存しない（復元したバスとセンドはミュートで
/// 始まる）。
struct SavedRouteGain {
    /// バスかセンドの entity_id。
    std::string entity_id;
    /// フェーダー（dB）。
    control_protocol::GainDb gain_db = 0.0;

    friend bool operator==(const SavedRouteGain&, const SavedRouteGain&) = default;
};

/// 保存するプラグインの状態 1 つの大きさの上限（バイト）。エフェクトの状態はふつう数 KB から
/// 数百 KB なので、壊れたプラグインが巨大な状態を返しても保存のたびに大きなファイルを書かないよう、
/// 1 MiB で打ち切る。超えた状態は保存せず、次の起動ではプラグインの既定の状態から始める。
inline constexpr std::size_t kMaxPluginStateBytes = std::size_t{1024} * 1024;

/// 保存する挿入のプラグインの状態（docs/adr/0042-plugin-insert.md）。
/// 保存するのは、保存するルーティング（HostSettings::routing）にある挿入のものだけ。
struct SavedPluginState {
    /// 挿入の entity_id。
    std::string insert_id;
    /// 状態を取ったプラグインの識別子。挿入のプラグインを替えたら、別のプラグインの状態は戻さない。
    std::string plugin_id;
    /// PluginInstance::save_state() のバイト列（プラグイン独自の形式）。
    std::vector<std::uint8_t> state;

    friend bool operator==(const SavedPluginState&, const SavedPluginState&) = default;
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
    /// 全体の目標レイテンシー（ms）。回線の状態に合わせてオペレーターが選んだ値を次の起動でも使う。
    double latency_target_ms = kDefaultLatencyTargetMs;
    /// SRTP 外部出力の設定（宛先、送り出すバス、プロファイル）。送信中かどうかは保存しない。
    /// 起動時は必ず止めた状態から始め、送信はオペレーターの操作でだけ始める（初期ミュートの
    /// 製品要件）。鍵は秘密情報なので SecretStore に分けて保存する
    /// （docs/adr/0040-srtp-external-output.md）。
    control_protocol::ExternalOutputConfig external_output;
    /// 録音の設定（形式、メディアフォルダ、録る対象のうち Master とバス）。録音中かどうかは
    /// 保存せず、起動時は必ず止めた状態から始める。ストリップの entity_id はセッションごとに
    /// 振り直すので、録る対象のストリップは保存しない（docs/adr/0043-recording-and-media-player.md）。
    /// folder が空なら、エンジンの既定のメディアフォルダを使う。
    control_protocol::RecorderConfig recorder;

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
    /// マイク入力のチャンネル数（1 か 2）。
    /// アプリ音声を送るかは保存しない。起動するたびに送らない状態から始め、出演者が選び直す
    /// （PC の通知音などを意図せず送らないため）。
    std::uint8_t input_channels = 1;
    /// 最後に選んだ返送モニターの出力デバイスの永続 ID。std::nullopt は「出力なし」。
    std::optional<std::string> output_device_id;

    friend bool operator==(const ClientSettings&, const ClientSettings&) = default;
};

/// 設定を読めなかった理由。どの理由でも、セッションは既定値（全ミュート）で起動する。
enum class SettingsError {
    kNotFound,   ///< 保存された設定がない（初回の起動）
    kCorrupted,  ///< 設定が壊れていて、バックアップからも復元できない
    kIoError,    ///< 読み書きに失敗した
};

}  // namespace vocroz::session

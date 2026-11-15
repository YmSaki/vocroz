// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージ。
// UI
// の接続時に全状態のスナップショットを送り、以降は差分イベントで知らせる（オブザーバーパターン）。
// コマンドへの応答（Ack、Reject、GainAck）、メーターの統計値、通知もここに含める。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"

namespace vocroz::control_protocol {

/// 親機モードの全状態（mixer_snapshot）。
struct MixerSnapshotEvent {
    MixerSnapshot snapshot;
    friend bool operator==(const MixerSnapshotEvent&, const MixerSnapshotEvent&) = default;
};

/// 子機モードの全状態（client_snapshot）。
struct ClientSnapshotEvent {
    ClientSnapshot snapshot;
    friend bool operator==(const ClientSnapshotEvent&, const ClientSnapshotEvent&) = default;
};

/// セッションの状態が変わった（session_updated）。
struct SessionUpdated {
    SessionInfo session;
    friend bool operator==(const SessionUpdated&, const SessionUpdated&) = default;
};

/// ストリップが追加されたか、状態が変わった（channel_updated）。新しいストリップは末尾に並ぶ。
struct ChannelUpdated {
    ChannelStrip channel;
    friend bool operator==(const ChannelUpdated&, const ChannelUpdated&) = default;
};

/// ストリップが削除された（channel_removed）。
struct ChannelRemoved {
    std::string entity_id;
    friend bool operator==(const ChannelRemoved&, const ChannelRemoved&) = default;
};

/// Master の状態が変わった（master_updated）。
struct MasterUpdated {
    MasterStrip master;
    friend bool operator==(const MasterUpdated&, const MasterUpdated&) = default;
};

/// 承認待ちの一覧が変わった（pending_updated）。一覧全体を送る。
struct PendingUpdated {
    std::vector<PendingParticipant> pending;
    friend bool operator==(const PendingUpdated&, const PendingUpdated&) = default;
};

/// 子機の状態が変わった（client_updated）。
struct ClientUpdated {
    ClientSnapshot client;
    friend bool operator==(const ClientUpdated&, const ClientUpdated&) = default;
};

/// ルーティングの構成かステージングが変わった（routing_updated）。ルーティング全体を送る。
struct RoutingUpdated {
    RoutingInfo routing;
    friend bool operator==(const RoutingUpdated&, const RoutingUpdated&) = default;
};

/// バスかセンドのフェーダーかミュートが変わった（route_level_updated）。
struct RouteLevelUpdated {
    RouteLevel level;
    friend bool operator==(const RouteLevelUpdated&, const RouteLevelUpdated&) = default;
};

/// SRTP 外部出力の状態が変わった（external_output_updated）。
struct ExternalOutputUpdated {
    ExternalOutput external_output;
    friend bool operator==(const ExternalOutputUpdated&, const ExternalOutputUpdated&) = default;
};

/// プラグインのスキャンか挿入の状態が変わった（plugins_updated）。スナップショットの直後にも送る。
struct PluginsUpdated {
    PluginsInfo plugins;
    friend bool operator==(const PluginsUpdated&, const PluginsUpdated&) = default;
};

/// プラグインの一覧の 1 ページ（plugin_catalog_page）。一覧が変わったときと、スナップショットの
/// 後に、PluginsUpdated に続けて先頭から順に送る。
struct PluginCatalogPage {
    /// 一覧の世代（PluginsInfo::catalog_generation）。
    std::uint64_t generation = 0;
    /// このページの先頭の項目の、一覧の中の位置（0 から）。
    std::uint64_t offset = 0;
    /// kMaxPluginCatalogPageEntries 件まで。
    std::vector<PluginEntry> plugins;
    friend bool operator==(const PluginCatalogPage&, const PluginCatalogPage&) = default;
};

/// 離散コマンドを適用した（ack）。
struct Ack {
    std::string request_id;
    /// 適用後の entity の revision。
    Revision applied_revision = 0;
    friend bool operator==(const Ack&, const Ack&) = default;
};

/// 離散コマンドを拒否した（reject）。
struct Reject {
    std::string request_id;
    ErrorCode error_code = ErrorCode::kEngineError;
    friend bool operator==(const Reject&, const Reject&) = default;
};

/// 連続値のコマンドを適用した（gain_ack）。seq 以下のコマンドはすべて適用済み。
struct GainAck {
    std::string entity_id;
    Sequence seq = 0;
    /// 適用時点の entity の revision（連続値では進まない）。
    Revision applied_revision = 0;
    friend bool operator==(const GainAck&, const GainAck&) = default;
};

/// 1 つの entity のメーターの値。
struct MeterLevel {
    std::string entity_id;
    /// プリフェーダーのピーク（dBFS）。std::nullopt は無音。
    std::optional<double> peak_db;
    friend bool operator==(const MeterLevel&, const MeterLevel&) = default;
};

/// メーターの統計値（meters）。エンジンが 10〜30 Hz で送る。生の波形は送らない。
struct Meters {
    /// entity_id の昇順。
    std::vector<MeterLevel> levels;
    friend bool operator==(const Meters&, const Meters&) = default;
};

/// 1 本の受信ストリームの回線の観測値。
struct LinkStat {
    /// チャンネルストリップの entity_id。
    std::string entity_id;
    /// 実測の到着間隔のジッター（ms）。まだ測れていなければ std::nullopt。
    std::optional<double> jitter_ms;
    /// ジッターバッファの今の目標の深さ（ms）。再生していなければ std::nullopt。
    std::optional<double> buffer_ms;
    friend bool operator==(const LinkStat&, const LinkStat&) = default;
};

/// 回線の観測値（link_stats）。エンジンが 1 Hz ほどで送る。観測値なので revision を持たない。
struct LinkStats {
    /// entity_id の昇順。
    std::vector<LinkStat> stats;
    friend bool operator==(const LinkStats&, const LinkStats&) = default;
};

/// エンジンからの通知（notice）。UI は非モーダルのトーストで表示する。
struct Notice {
    NoticeSeverity severity = NoticeSeverity::kInfo;
    /// 通知の種別。UI の文字列リソースのキー（notice.<code>）に対応する。英小文字、数字、_ だけ。
    std::string code;
    /// 文言に埋め込む値（参加者名など）。
    std::map<std::string, std::string> params;
    friend bool operator==(const Notice&, const Notice&) = default;
};

/// エンジンから UI へのメッセージ。
using EngineToUiMessage =
    std::variant<MixerSnapshotEvent, ClientSnapshotEvent, SessionUpdated, ChannelUpdated,
                 ChannelRemoved, MasterUpdated, PendingUpdated, ClientUpdated, RoutingUpdated,
                 RouteLevelUpdated, ExternalOutputUpdated, PluginsUpdated, PluginCatalogPage, Ack,
                 Reject, GainAck, Meters, LinkStats, Notice>;

}  // namespace vocroz::control_protocol

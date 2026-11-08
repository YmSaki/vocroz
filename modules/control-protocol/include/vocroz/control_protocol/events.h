// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンから UI へのメッセージ。
// UI
// の接続時に全状態のスナップショットを送り、以降は差分イベントで知らせる（オブザーバーパターン）。
// コマンドへの応答（Ack、Reject、GainAck）、メーターの統計値、通知もここに含める。
#pragma once

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
                 ChannelRemoved, MasterUpdated, PendingUpdated, ClientUpdated, Ack, Reject, GainAck,
                 Meters, Notice>;

}  // namespace vocroz::control_protocol

// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンが UI に知らせる状態の DTO。スナップショットと差分イベントの中身になる。
// 正となる状態はエンジンだけが持ち、UI はこれを受け取って表示するだけにする（エンジン主権）。
//
// revision は entity ごとの版で、コマンドで変更できる値が変わったときだけ進む。
// エンジンが観測して変わるだけの値（接続状態、信号状態など）の変化では進まない
// （docs/adr/0014-control-protocol-messages.md）。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/control_protocol/states.h"

namespace vocroz::control_protocol {

/// entity の版。0 から kMaxCounter まで。
using Revision = std::uint64_t;

/// 連続値のコマンドのシーケンス番号。entity ごとに UI が振り、大きいほうを後勝ちにする。
using Sequence = std::uint64_t;

/// ゲイン（dB）。std::nullopt は -∞ dB を表す（JSON では null）。
/// -∞ dB は音量値であり、ミュートとは別に扱う。
using GainDb = std::optional<double>;

/// 親機のセッション全体の状態。ステータス帯に表示する。
struct SessionInfo {
    std::string entity_id;
    Revision revision = 0;
    /// セッション名。IDLE では空。
    std::string name;
    SessionState state = SessionState::kIdle;
    /// ERROR のときの原因。
    std::optional<ErrorCode> error_code;
    /// 招待 URL。READY と ACTIVE のときだけ値を持つ。
    std::optional<std::string> invite_url;
    OutputDeviceState output_device_state = OutputDeviceState::kNone;
    /// OPEN のときのデバイス名。
    std::optional<std::string> output_device_name;
    /// 利用可能な更新のバージョン。なければ std::nullopt。
    std::optional<std::string> update_available;

    friend bool operator==(const SessionInfo&, const SessionInfo&) = default;
};

/// 入力チャンネルストリップ 1 本の状態。並び順はエンジンが決め、UI は並べ替えない。
struct ChannelStrip {
    std::string entity_id;
    Revision revision = 0;
    /// ストリップに固定された参加者。LOST からの再接続でも変わらない。
    std::string participant_id;
    std::string name;
    /// 参加者を見分けるための短い識別 ID（例: "#08"）。
    std::string short_id;
    /// PENDING にはならない（PENDING の参加者はストリップを持たないため）。
    ConnectionState connection_state = ConnectionState::kConnecting;
    /// connection_state が CONNECTED のときだけ値を持つ。
    std::optional<SignalState> signal_state;
    /// 子機側の送信ミュート。親機からは解除できない。初期ミュートの要件により既定値は true。
    bool send_muted = true;
    /// 親機側のチャンネルミュート。初期ミュートの要件により既定値は true。
    bool channel_muted = true;
    GainDb gain_db = 0.0;
    /// FAILED のときの原因。
    std::optional<ErrorCode> error_code;

    friend bool operator==(const ChannelStrip&, const ChannelStrip&) = default;
};

/// Master ストリップの状態。
struct MasterStrip {
    std::string entity_id;
    Revision revision = 0;
    GainDb gain_db = 0.0;
    /// 初期ミュートの要件により既定値は true。
    bool master_muted = true;
    /// master_muted が true のときの理由。
    std::optional<MasterMuteReason> master_mute_reason = MasterMuteReason::kOperator;

    friend bool operator==(const MasterStrip&, const MasterStrip&) = default;
};

/// 承認待ちの参加者。承認するとストリップが作られ、この entity は消える。
struct PendingParticipant {
    std::string entity_id;
    Revision revision = 0;
    std::string name;
    std::string short_id;

    friend bool operator==(const PendingParticipant&, const PendingParticipant&) = default;
};

/// 親機モードの全状態。UI の接続時にエンジンが送る。
struct MixerSnapshot {
    SessionInfo session;
    /// エンジンが決めた並び順。
    std::vector<ChannelStrip> channels;
    MasterStrip master;
    std::vector<PendingParticipant> pending;

    friend bool operator==(const MixerSnapshot&, const MixerSnapshot&) = default;
};

/// 子機で選べる入力デバイス。
struct InputDevice {
    /// デバイス固有の永続 ID。
    std::string id;
    std::string name;

    friend bool operator==(const InputDevice&, const InputDevice&) = default;
};

/// 子機モードの全状態。entity は 1 つで、接続状態の遷移では revision を進めない。
struct ClientSnapshot {
    std::string entity_id;
    Revision revision = 0;
    ClientState client_state = ClientState::kDisconnected;
    /// FAILED の原因か、DISCONNECTED の直前の終了理由。
    std::optional<ErrorCode> failure_code;
    /// 接続先のセッション名。わかるまでは std::nullopt。
    std::optional<std::string> session_name;
    std::vector<InputDevice> input_devices;
    /// 選択中の入力デバイスの ID。std::nullopt は「入力なし」。
    std::optional<std::string> selected_input_id;
    /// 選択中のデバイスが抜去されて入力がない。
    bool input_missing = false;
    /// 送信ミュート。マイクを選んだだけでは送信しないよう、既定値は true。
    bool send_muted = true;
    /// 送信レベル（-60〜+12 dB）。
    double send_level_db = 0.0;

    friend bool operator==(const ClientSnapshot&, const ClientSnapshot&) = default;
};

}  // namespace vocroz::control_protocol

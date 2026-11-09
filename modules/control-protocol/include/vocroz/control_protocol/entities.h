// SPDX-License-Identifier: AGPL-3.0-only
//
// エンジンが UI に知らせる状態の DTO。スナップショットと差分イベントの中身になる。
// 正となる状態はエンジンだけが持ち、UI はこれを受け取って表示するだけにする（エンジン主権）。
//
// revision は entity ごとの版で、コマンドで変更できる値が変わったときだけ進む。
// エンジンが観測して変わるだけの値（接続状態、信号状態など）の変化では進まない
// （docs/adr/0015-control-protocol-messages.md）。
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

/// 親機で選べる出力デバイス。
struct OutputDevice {
    /// デバイス固有の永続 ID（エンドポイント ID）。
    std::string id;
    std::string name;

    friend bool operator==(const OutputDevice&, const OutputDevice&) = default;
};

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
    /// 選択中の出力デバイスの ID。NONE では std::nullopt。LOST では抜けたデバイスの ID を残す
    /// （挿し直したデバイスを選び直しやすくするため）。
    std::optional<std::string> output_device_id;
    /// 選べる出力デバイス。UI の接続時と、選択や抜去のときにエンジンが列挙し直す。
    std::vector<OutputDevice> output_devices;
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

/// ルーティングのバス（ユーザーバス、キューバス）。ステレオで、フェーダーとミュートを持つ。
/// entity_id は "bus-" で始める（ストリップなどエンジンが振る ID と衝突させないため）。
struct RoutingBus {
    std::string entity_id;
    std::string name;
    BusKind kind = BusKind::kUser;

    friend bool operator==(const RoutingBus&, const RoutingBus&) = default;
};

/// 追加の物理出力。メインの出力（Master の出力先）とは別に、同じ出力デバイスの別のチャンネルの組へ
/// 書き出す。entity_id は "out-" で始める。
struct RoutingOutput {
    std::string entity_id;
    std::string name;
    /// 書き出す先頭のチャンネル（0 始まり）。ステレオなので device_channel と次のチャンネルに書く。
    std::uint64_t device_channel = 0;

    friend bool operator==(const RoutingOutput&, const RoutingOutput&) = default;
};

/// センド（ノードからノードへの結線）。フェーダー（センドレベル）とミュートを持つ。
/// entity_id は "send-" で始める。ストリップから Master と、Master からメインの出力への結線は
/// 暗黙にあるので、センドには書かない。
struct RoutingSend {
    std::string entity_id;
    /// 送り元のノード（ストリップ、"master"、バス、遅延ノード）の entity_id。
    std::string from;
    /// 送り先のノード（"master"、バス、出力、遅延ノード）の entity_id。
    std::string to;
    SendTap tap = SendTap::kPostFader;

    friend bool operator==(const RoutingSend&, const RoutingSend&) = default;
};

/// フィードバック経路の遅延ノード。ループを通してよい唯一のノード。entity_id は "delay-" で始める。
struct RoutingDelay {
    std::string entity_id;
    double delay_ms = 0.0;

    friend bool operator==(const RoutingDelay&, const RoutingDelay&) = default;
};

/// 入力ストリップの手動ディレイ。書かれていないストリップは 0 ms。
struct InputDelay {
    std::string strip_id;
    double delay_ms = 0.0;

    friend bool operator==(const InputDelay&, const InputDelay&) = default;
};

/// ルーティングの構成。UI がステージングし、エンジンが検証して適用する。
struct RoutingConfig {
    std::vector<RoutingBus> buses;
    std::vector<RoutingOutput> outputs;
    std::vector<RoutingSend> sends;
    std::vector<RoutingDelay> delays;
    std::vector<InputDelay> input_delays;

    friend bool operator==(const RoutingConfig&, const RoutingConfig&) = default;
};

/// ステージングした構成の検証の結果。
struct RoutingCheck {
    /// 適用できない理由。std::nullopt なら適用できる（ループがあれば承認が要る）。
    std::optional<RoutingIssue> issue;
    /// 理由の原因になった entity_id。わかるときだけ値を持つ。
    std::optional<std::string> issue_entity_id;
    /// ループに含まれるノードの entity_id（昇順）。ルーティング画面で強調するために使う。
    std::vector<std::string> looped_entity_ids;
    /// 適用するにはオペレーターの承認が要る（遅延ノードを通るループだけがある）。
    bool needs_approval = false;

    friend bool operator==(const RoutingCheck&, const RoutingCheck&) = default;
};

/// ステージング中の構成と、その検証の結果。
struct StagedRouting {
    RoutingConfig config;
    RoutingCheck check;

    friend bool operator==(const StagedRouting&, const StagedRouting&) = default;
};

/// バスかセンドのフェーダーとミュート。entity_id はバスかセンドのもの。
/// 構成とは別の entity にし、フェーダーの操作でルーティング全体の revision を進めないようにする。
struct RouteLevel {
    std::string entity_id;
    Revision revision = 0;
    GainDb gain_db = 0.0;
    /// 初期ミュートの要件により既定値は true。新しいバスとセンドはミュートで作られる。
    bool muted = true;

    friend bool operator==(const RouteLevel&, const RouteLevel&) = default;
};

/// ルーティング全体の状態。entity_id は "routing"。
/// revision は、適用中の構成かステージング中の構成が変わったときに進む。
struct RoutingInfo {
    std::string entity_id;
    Revision revision = 0;
    /// 適用中の構成。
    RoutingConfig applied;
    /// 適用中の構成のループを、オペレーターが承認して適用したなら true。
    bool feedback_approved = false;
    /// ステージング中の構成。なければ std::nullopt。
    std::optional<StagedRouting> staged;
    /// 適用中の構成のバスとセンドのフェーダーとミュート。applied のバス、センドの順に並ぶ。
    std::vector<RouteLevel> levels;

    friend bool operator==(const RoutingInfo&, const RoutingInfo&) = default;
};

/// 親機モードの全状態。UI の接続時にエンジンが送る。
struct MixerSnapshot {
    SessionInfo session;
    /// エンジンが決めた並び順。
    std::vector<ChannelStrip> channels;
    MasterStrip master;
    std::vector<PendingParticipant> pending;
    RoutingInfo routing;

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

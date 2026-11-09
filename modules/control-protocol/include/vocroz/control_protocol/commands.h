// SPDX-License-Identifier: AGPL-3.0-only
//
// UI からエンジンへのメッセージ（コマンド）。1 操作に 1 コマンド（コマンドパターン）。
//
// 離散コマンド（ミュート、承認など）は request_id、entity_id、expected_revision を持ち、
// エンジンは適用したら Ack、拒否したら Reject を返す。重複した request_id は無視される。
// 連続値（フェーダー、送信レベル）は SetGain で送り、entity ごとのシーケンス番号で後勝ちにする。
// SetGain は revision を進めず、expected_revision による競合の検出の対象外とする。
#pragma once

#include <optional>
#include <string>
#include <variant>

#include "vocroz/control_protocol/entities.h"

namespace vocroz::control_protocol {

/// セッションを作る（親機）。対象はセッションの entity。
struct CreateSession {
    friend bool operator==(const CreateSession&, const CreateSession&) = default;
};

/// セッションを終了する（親機）。対象はセッションの entity。
struct EndSession {
    friend bool operator==(const EndSession&, const EndSession&) = default;
};

/// チャンネルミュートを切り替える（親機）。対象はチャンネルストリップ。
struct SetChannelMute {
    bool muted = true;
    friend bool operator==(const SetChannelMute&, const SetChannelMute&) = default;
};

/// Master のミュートを切り替える（親機）。対象は Master。
struct SetMasterMute {
    bool muted = true;
    friend bool operator==(const SetMasterMute&, const SetMasterMute&) = default;
};

/// 参加を承認する（親機）。対象は承認待ちの参加者。
struct ApproveParticipant {
    friend bool operator==(const ApproveParticipant&, const ApproveParticipant&) = default;
};

/// 参加を拒否する（親機）。対象は承認待ちの参加者。
struct RejectParticipant {
    friend bool operator==(const RejectParticipant&, const RejectParticipant&) = default;
};

/// LOST などのストリップを削除する（親機）。対象はチャンネルストリップ。
struct RemoveStrip {
    friend bool operator==(const RemoveStrip&, const RemoveStrip&) = default;
};

/// 招待で参加する（子機）。対象は子機の entity。
struct Join {
    /// 招待コードか招待 URL。形式の検証は session が行う。
    std::string invite;
    friend bool operator==(const Join&, const Join&) = default;
};

/// セッションから抜ける（子機）。対象は子機の entity。
struct Leave {
    friend bool operator==(const Leave&, const Leave&) = default;
};

/// 入力デバイスを選ぶ（子機）。選んでも送信ミュートは変わらない。対象は子機の entity。
struct SelectInput {
    /// デバイスの永続 ID。std::nullopt は「入力なし」。
    std::optional<std::string> device_id;
    friend bool operator==(const SelectInput&, const SelectInput&) = default;
};

/// 送信ミュートを切り替える（子機）。対象は子機の entity。
struct SetSendMute {
    bool muted = true;
    friend bool operator==(const SetSendMute&, const SetSendMute&) = default;
};

/// 出力デバイスを選ぶ（親機）。対象はセッションの entity。
/// 選んでも Master のミュートは変わらない（音を出すかはオペレーターが Master で決める）。
/// デバイスを開けなければ DEVICE_UNAVAILABLE で拒否し、ほかのデバイスへは切り替えない。
struct SelectOutput {
    /// デバイスの永続 ID。std::nullopt は「出力なし」（出力を止める）。
    std::optional<std::string> device_id;
    friend bool operator==(const SelectOutput&, const SelectOutput&) = default;
};

/// ルーティングの構成をステージングする（親機）。対象は "routing"。
/// エンジンは構成を検証して結果とともに保持するだけで、音声のグラフはまだ変えない。
/// 検証で問題が見つかってもステージングは受け付け、理由を RoutingCheck で知らせる。
struct StageRouting {
    RoutingConfig config;
    friend bool operator==(const StageRouting&, const StageRouting&) = default;
};

/// ステージング中の構成を適用する（親機）。対象は "routing"。
/// 遅延ノードを通るループがある構成は、approve_feedback が true のときだけ適用する。
struct CommitRouting {
    /// オペレーターがループを確認して承認した。
    bool approve_feedback = false;
    friend bool operator==(const CommitRouting&, const CommitRouting&) = default;
};

/// ステージング中の構成を捨てる（親機）。対象は "routing"。
struct DiscardRouting {
    friend bool operator==(const DiscardRouting&, const DiscardRouting&) = default;
};

/// バスかセンドのミュートを切り替える（親機）。対象はバスかセンド。
struct SetRouteMute {
    bool muted = true;
    friend bool operator==(const SetRouteMute&, const SetRouteMute&) = default;
};

/// 返送モニター（Minus-Me）のミュートを切り替える（親機）。対象はチャンネルストリップ。
/// ミュート中は、そのストリップの子機への返送の送信を止める（docs/adr/0036-return-monitor.md）。
struct SetReturnMute {
    bool muted = true;

    friend bool operator==(const SetReturnMute&, const SetReturnMute&) = default;
};

/// 全体の目標レイテンシーを変える（親機）。対象は "session"。
struct SetLatencyTarget {
    /// 目標（ms）。kMinLatencyTargetMs〜kMaxLatencyTargetMs。
    double target_ms = 20.0;
    friend bool operator==(const SetLatencyTarget&, const SetLatencyTarget&) = default;
};

/// 離散コマンドの種類ごとの本体。
using DiscreteCommandBody =
    std::variant<CreateSession, EndSession, SetChannelMute, SetMasterMute, ApproveParticipant,
                 RejectParticipant, RemoveStrip, Join, Leave, SelectInput, SetSendMute,
                 StageRouting, CommitRouting, DiscardRouting, SetRouteMute, SelectOutput,
                 SetReturnMute, SetLatencyTarget>;

/// 離散コマンド。
struct DiscreteCommand {
    /// UI が振る要求の ID。Ack と Reject で同じ値が返る。
    std::string request_id;
    /// 操作の対象の entity。
    std::string entity_id;
    /// UI が表示している entity の revision。エンジンの revision と違えば REVISION_CONFLICT
    /// で拒否される。
    Revision expected_revision = 0;
    DiscreteCommandBody body;

    friend bool operator==(const DiscreteCommand&, const DiscreteCommand&) = default;
};

/// 連続値のコマンド（フェーダー、送信レベル、バスとセンドのレベル）。UI はドラッグ中に最大 60 Hz
/// で送る。 エンジンは受け取るたびに 5〜20 ms のゲインランプで適用し、GainAck を返す。
struct SetGain {
    std::string entity_id;
    /// entity ごとのシーケンス番号。エンジンは適用済みより小さい番号を捨てる。
    Sequence seq = 0;
    GainDb gain_db;

    friend bool operator==(const SetGain&, const SetGain&) = default;
};

/// UI が接続したことを知らせる。エンジンはそのモードのスナップショットを返す。
struct Hello {
    friend bool operator==(const Hello&, const Hello&) = default;
};

/// UI からエンジンへのメッセージ。
using UiToEngineMessage = std::variant<Hello, DiscreteCommand, SetGain>;

}  // namespace vocroz::control_protocol

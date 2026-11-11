// SPDX-License-Identifier: AGPL-3.0-only
//
// UI からエンジンへのメッセージ（コマンド）。1 操作に 1 コマンド（コマンドパターン）。
//
// 離散コマンド（ミュート、承認など）は request_id、entity_id、expected_revision を持ち、
// エンジンは適用したら Ack、拒否したら Reject を返す。重複した request_id は無視される。
// 連続値（フェーダー、送信レベル）は SetGain で送り、entity ごとのシーケンス番号で後勝ちにする。
// SetGain は revision を進めず、expected_revision による競合の検出の対象外とする。
#pragma once

#include <cstdint>
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

/// マイク入力のチャンネル数を切り替える（子機）。対象は子機の entity。
/// 親機では別の入力として扱われ、ストリップが作り直される（docs/adr/0035-client-input-expansion.md）。
struct SetInputChannels {
    /// 1（モノラル）か 2（ステレオ）。範囲の検証はデコーダーが行う。
    std::uint8_t channels = 1;
    friend bool operator==(const SetInputChannels&, const SetInputChannels&) = default;
};

/// アプリ音声を送るかを切り替える（子機）。対象は子機の entity。
/// 送信ミュートはマイクと共通で、切り替えても変わらない。
struct SetAppAudio {
    bool enabled = false;
    friend bool operator==(const SetAppAudio&, const SetAppAudio&) = default;
};

/// 出力デバイスを選ぶ。
/// 親機では対象はセッションの entity で、Master を鳴らすデバイスを選ぶ。選んでも Master の
/// ミュートは変わらない（音を出すかはオペレーターが Master で決める）。
/// 子機では対象は子機の entity で、返送モニターを鳴らすデバイスを選ぶ。返送のミュートは親機が
/// 決めるので、選んでも変わらない（docs/adr/0036-return-monitor.md）。
/// どちらも、デバイスを開けなければ DEVICE_UNAVAILABLE で拒否し、ほかのデバイスへは切り替えない。
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

/// SRTP 外部出力の設定を変える（親機）。対象は "external_output"。
/// 送信中と接続テスト中は INVALID_STATE で拒否する（設定は止めてから変える）。
/// 送り出すバスがなければ NOT_FOUND で拒否する。
struct SetExternalOutputConfig {
    ExternalOutputConfig config;
    friend bool operator==(const SetExternalOutputConfig&,
                           const SetExternalOutputConfig&) = default;
};

/// SRTP 外部出力の鍵を設定する（親機）。対象は "external_output"。
/// 形式は SDES（RFC 4568）の inline: の鍵（30 バイトの Base64）。形式が正しくなければ
/// INVALID_STATE で拒否する。送信中と接続テスト中も INVALID_STATE で拒否する。
/// 鍵はエンジンが DPAPI で保存し、スナップショットとイベントには設定済みかどうかだけを載せる。
struct SetExternalOutputKey {
    std::string key;
    friend bool operator==(const SetExternalOutputKey&, const SetExternalOutputKey&) = default;
};

/// SRTP 外部出力の送信を開始か停止する（親機）。対象は "external_output"。
/// 宛先か鍵が未設定なら、開始は INVALID_STATE で拒否する（平文では送らない）。
struct SetExternalOutputRunning {
    bool running = false;
    friend bool operator==(const SetExternalOutputRunning&,
                           const SetExternalOutputRunning&) = default;
};

/// SRTP 外部出力の接続テストを始める（親機）。対象は "external_output"。
/// 停止中だけ行える。同じ SRTP の経路で試験音を数秒送り、結果を ExternalOutput::last_result で
/// 知らせる。
struct TestExternalOutput {
    friend bool operator==(const TestExternalOutput&, const TestExternalOutput&) = default;
};

/// プラグインをスキャンし直す（親機）。対象は "plugins"。
/// スキャン中なら、今のスキャンが済んでからもう一度スキャンする。変更のないファイルは
/// スキャンしない（docs/adr/0032-plugin-catalog-and-loader.md）。
struct RescanPlugins {
    friend bool operator==(const RescanPlugins&, const RescanPlugins&) = default;
};

/// 挿入のプラグインのエディターを開く（親機）。対象は "plugins"。
/// 開いていれば前面に出す。挿入がなければ NOT_FOUND、読み込みが済んでいなければ（読み込み中か
/// 失敗）INVALID_STATE で拒否する。ウィンドウを開けなかったことは、後で通知
/// （plugin_editor_failed）で知らせる（docs/adr/0041-plugin-editor-and-failure-notice.md）。
struct OpenPluginEditor {
    /// 適用中のルーティングの挿入の entity_id。
    std::string insert_id;
    friend bool operator==(const OpenPluginEditor&, const OpenPluginEditor&) = default;
};

/// 離散コマンドの種類ごとの本体。
using DiscreteCommandBody =
    std::variant<CreateSession, EndSession, SetChannelMute, SetMasterMute, ApproveParticipant,
                 RejectParticipant, RemoveStrip, Join, Leave, SelectInput, SetSendMute,
                 StageRouting, CommitRouting, DiscardRouting, SetRouteMute, SelectOutput,
                 SetReturnMute, SetInputChannels, SetAppAudio, SetLatencyTarget,
                 SetExternalOutputConfig, SetExternalOutputKey, SetExternalOutputRunning,
                 TestExternalOutput, RescanPlugins, OpenPluginEditor>;

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

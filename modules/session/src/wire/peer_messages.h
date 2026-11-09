// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機と親機の間の制御メッセージ（信頼制御層の上で運ぶ本文）の型。
// 形式は使う側の session が決める（docs/adr/0019-reliable-control-layer.md）。
// 向きごとに別の variant にして、親機が子機向けのメッセージを受け取るような取り違えを型で防ぐ。
// 子機から親機へ送れるのは、参加の申請と自分の状態の通知だけで、親機のミキサー（Master や
// 他のチャンネル）を操作するメッセージはない。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vocroz/control_protocol/states.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/input_stream.h"

namespace vocroz::session::wire {

/// 子機 → 親機: 招待による参加の申請。
struct JoinRequest {
    /// 招待コードの解決で得た招待トークン。
    InviteToken invite_token{};
    /// 以前に承認されたときの再接続トークン。持っていれば同じストリップへ戻る。
    std::optional<ReconnectToken> reconnect_token;
    /// 承認パネルとストリップに出す名前（検証済みの UTF-8）。
    std::string display_name;

    friend bool operator==(const JoinRequest&, const JoinRequest&) = default;
};

/// 子機 → 親機: 回線の途絶後の再接続の申請。
struct ReconnectRequest {
    ReconnectToken reconnect_token{};

    friend bool operator==(const ReconnectRequest&, const ReconnectRequest&) = default;
};

/// 子機 → 親機: 送信ミュートの通知。
struct SendMuteReport {
    bool send_muted = true;
    /// ミュートの世代番号。解除のたびに 1 増える。
    std::uint32_t epoch = 0;

    friend bool operator==(const SendMuteReport&, const SendMuteReport&) = default;
};

/// 子機 → 親機: 入力デバイスの有無の通知（マイクの抜去）。
struct InputReport {
    bool input_missing = false;

    friend bool operator==(const InputReport&, const InputReport&) = default;
};

/// 子機 → 親機: 送る入力（ストリーム）の一覧の申告。
/// 参加の申請と再接続の後と、入力の構成を変えたときに送る。一覧は毎回すべてを送り、親機は前回の
/// 一覧と置き換える。申告を受け取る前の子機は、ストリーム 0 のモノラルのマイク 1 本とみなす。
struct StreamsReport {
    /// 1〜kMaxInputStreams 個。ストリーム ID は重複しない。
    std::vector<InputStream> streams;
    friend bool operator==(const StreamsReport&, const StreamsReport&) = default;
};

/// 子機 → 親機: 退出。
struct LeaveNotice {
    friend bool operator==(const LeaveNotice&, const LeaveNotice&) = default;
};

/// 親機 → 子機: 申請を受け付け、承認を待っている。
struct JoinPending {
    /// セッション名（検証済みの UTF-8）。
    std::string session_name;

    friend bool operator==(const JoinPending&, const JoinPending&) = default;
};

/// 親機 → 子機: 承認した（再接続による復帰を含む）。
struct JoinApproved {
    /// 以後の再接続に使うトークン。
    ReconnectToken reconnect_token{};

    friend bool operator==(const JoinApproved&, const JoinApproved&) = default;
};

/// 親機 → 子機: 申請を受け付けなかった。
struct JoinDenied {
    /// 理由。kRejected、kInviteExpired、kInviteRevoked、kInviteInvalid、kReconnectTimeout、
    /// kSessionEnded のいずれか。
    control_protocol::ErrorCode reason = control_protocol::ErrorCode::kRejected;

    friend bool operator==(const JoinDenied&, const JoinDenied&) = default;
};

/// 親機 → 子機: セッションを終了した。
struct SessionEndedNotice {
    friend bool operator==(const SessionEndedNotice&, const SessionEndedNotice&) = default;
};

/// 親機 → 子機: 返送モニター（Minus-Me）のミュートの通知。
/// 親機はミュート中に返送の送信を止める。子機は世代番号で、ミュート前の古いフレームと
/// 解除後の新しいフレームを区別する（docs/adr/0036-return-monitor.md）。
struct ReturnMuteNotice {
    bool muted = true;
    /// ミュートの世代番号。親機が解除のたびに 1 増やす。
    std::uint32_t epoch = 0;

    friend bool operator==(const ReturnMuteNotice&, const ReturnMuteNotice&) = default;
};

/// 子機から親機へのメッセージ。
using ClientToHostMessage = std::variant<JoinRequest, ReconnectRequest, SendMuteReport, InputReport,
                                         LeaveNotice, StreamsReport>;

/// 親機から子機へのメッセージ。
using HostToClientMessage =
    std::variant<JoinPending, JoinApproved, JoinDenied, SessionEndedNotice, ReturnMuteNotice>;

}  // namespace vocroz::session::wire

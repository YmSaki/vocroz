// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機の UI から届いたコマンドの適用。
// 重複した request_id の無視、revision の競合による拒否、コマンドごとの対象の検証（認可）を
// ここ 1 か所で行い、状態の変更と副作用は HostContext に任せる。
#pragma once

#include <map>
#include <optional>
#include <string>

#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"

#include "host_context.h"
#include "request_deduplicator.h"
#include "session_constants.h"

namespace vocroz::session {

/// 親機の UI からのメッセージ（Hello、離散コマンド、SetGain）を処理する。
/// スレッド安全性: コントロールスレッドからだけ使う。
class HostCommandHandler {
public:
    /// @param context 親機のセッションの状態。このオブジェクトより長く生存すること。
    explicit HostCommandHandler(HostContext& context);

    /// UI からのメッセージを適用し、Ack、Reject、GainAck、スナップショットで応答する。
    void handle_ui_message(const control_protocol::UiToEngineMessage& message);

private:
    using CommandResult = common::Result<control_protocol::Revision, control_protocol::ErrorCode>;

    void handle_discrete(const control_protocol::DiscreteCommand& command);
    std::optional<control_protocol::Revision> revision_of(const std::string& entity_id);
    CommandResult apply(const control_protocol::DiscreteCommand& command);
    static CommandResult invalid_state();
    CommandResult create_session();
    CommandResult end_session();
    CommandResult set_channel_mute(const std::string& entity_id, bool muted);
    CommandResult set_master_mute(bool muted);
    CommandResult approve(const std::string& entity_id);
    CommandResult reject(const std::string& entity_id);
    CommandResult remove_strip(const std::string& entity_id);
    CommandResult stage_routing(const control_protocol::RoutingConfig& config);
    CommandResult commit_routing(bool approve_feedback);
    CommandResult discard_routing();
    CommandResult set_route_mute(const std::string& entity_id, bool muted);
    void handle_gain(const control_protocol::SetGain& command);
    void handle_route_gain(control_protocol::RouteLevel& level,
                           const control_protocol::SetGain& command);

    HostContext& ctx_;
    RequestDeduplicator requests_{kRememberedRequestIds};
    /// Master の SetGain の最後に適用した seq。古い seq の値で戻さないため。
    std::optional<control_protocol::Sequence> last_master_gain_seq_;
    /// バスとセンドの SetGain の最後に適用した seq（entity_id ごと）。
    std::map<std::string, control_protocol::Sequence> last_route_gain_seq_;
};

}  // namespace vocroz::session

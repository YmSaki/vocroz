// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッションの状態と、状態の遷移に伴う共通の副作用。
// UI
// のコマンド（HostCommandHandler）と子機からの通知（HostPeerHandler）の両方が同じ状態を変えるので、
// 正となる状態と「遷移したら何をするか」をここ 1 か所に置き、両者から使う。
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/events.h"
#include "vocroz/session/host_session.h"
#include "vocroz/session/settings.h"

#include "invite_book.h"
#include "participant_registry.h"
#include "routing_book.h"
#include "session_constants.h"
#include "state/connection_state_machine.h"
#include "state/output_device_state_machine.h"
#include "state/session_state_machine.h"
#include "wire/peer_messages.h"

namespace vocroz::session {

/// 招待の登録に失敗したとき、次に発行し直すまでの間隔。
/// ランデブーサーバーが止まっている間に、tick() のたびに要求を送り続けないため。
inline constexpr std::chrono::seconds kInviteRetryInterval{10};

/// フェーダーの値を親機のフェーダーの範囲（-∞〜+10 dB）に収める。
/// 下限（-60 dB）は control-protocol のデコーダーが検証済み。
[[nodiscard]] inline control_protocol::GainDb clamp_fader(control_protocol::GainDb gain) noexcept {
    if (gain && *gain > kMaxFaderGainDb) {
        return kMaxFaderGainDb;
    }
    return gain;
}

/// 通知の文言に入れる参加者の呼び名。名前が空なら短い識別 ID で呼ぶ。
[[nodiscard]] inline const std::string& label(
    const control_protocol::ChannelStrip& strip) noexcept {
    return strip.name.empty() ? strip.short_id : strip.name;
}

/// 親機のセッションの状態と、共通の副作用。
/// 内部の型なので、状態はメンバー変数として公開し、HostSession の各部分から直接読み書きする。
/// スレッド安全性: コントロールスレッドからだけ使う。
struct HostContext {
    HostContext(HostSession::Config config, HostSession::Ports ports);

    // --- 状態の遷移 ---

    /// セッション状態を進める。受け付けないイベントなら false。
    bool advance_session(state::SessionEvent event);

    /// 出力デバイスの状態を進める。受け付けないイベントなら false。
    bool advance_output(state::OutputDeviceEvent event);

    /// 参加者の接続状態を進め、それに伴う値（信号状態、猶予の起点、原因）をそろえて UI に知らせる。
    /// 接続状態は観測値なので revision は進めない。受け付けないイベントなら false。
    bool advance_connection(Participant& participant, state::ConnectionEvent event);

    /// CONNECTED か RECONNECTING の参加者の有無で、READY と ACTIVE を切り替える。
    void update_session_activity();

    /// 参加の申請を受け付ける状態（READY か ACTIVE）なら true。
    [[nodiscard]] bool accepting_joins() const noexcept;

    // --- 副作用 ---

    /// 新しい招待を作り、ランデブーサーバーへ登録を要求する。
    void issue_invite();

    /// 招待を失効させ、すべての子機を切断し、ストリップを片付けて設定を保存する。
    void teardown();

    /// Master をミュートする。理由が変わるだけでも revision を進め、古い表示に基づく解除を
    /// REVISION_CONFLICT で拒否できるようにする。
    void mute_master(control_protocol::MasterMuteReason reason);

    /// 開いている出力デバイスを閉じ、出力デバイスを未選択（NONE）にする。
    void close_output();

    /// 開いている出力デバイスを閉じてから、指定したデバイスを開く。開けたら設定に保存する。
    /// 開けなければ未選択（NONE）のままにし、保存した設定は変えない。UI には知らせない。
    /// @param device_id デバイスの永続 ID
    /// @return 開けたら true
    bool open_output(const std::string& device_id);

    /// 選べる出力デバイスの一覧を列挙し直す。UI には知らせない。
    void refresh_output_devices();

    /// 設定を保存する。失敗はログに残して続行する。
    void save_settings();

    /// 申請を拒否して接続を閉じる。
    void deny(PeerHandle peer, control_protocol::ErrorCode reason);

    /// 参加者と接続の結び付きを解き、接続を閉じる。ストリップと再接続トークンは残す。
    void release_peer(Participant& participant);

    /// ストリップを削除したあと、そのストリップを参照するルーティングを片付ける。
    /// 適用中の構成が変わったらミキサーへ適用し直し、UI にルーティングを知らせる。
    /// @param strip_id 削除したストリップ。registry からは取り除き済みであること
    void forget_strip_in_routing(const std::string& strip_id);

    /// ステージング中のルーティングを検証し直し、ミキサーへ適用して適用中にする。UI
    /// には知らせない。
    /// @param approve_feedback オペレーターが遅延ノードを通るループを承認した
    /// @return 適用できなければ INVALID_STATE（構成の問題、承認がない）か ENGINE_ERROR（ミキサーが
    ///         受け付けない）。どちらの場合も今のグラフとステージングをそのまま残す
    [[nodiscard]] common::Result<void, control_protocol::ErrorCode> commit_staged_routing(
        bool approve_feedback);

    /// 保存したルーティングの構成を起動時に戻す。問題のない構成はそのまま適用し、バスとセンドは
    /// 保存したフェーダーの値とミュートで始める。承認の要るループがあればステージングに置いて
    /// オペレーターの承認を待ち、問題のある構成は戻さない（docs/adr/0033-routing-persistence.md）。
    void restore_routing();

    /// 全体の目標レイテンシーを受信側へ反映し、セッションの状態と設定に記録する。
    /// UI への通知と保存は呼び出し側が行う。
    /// @param target_ms 目標（ms）。範囲は検証済みであること
    void apply_latency_target(double target_ms);

    /// 今あるチャンネルストリップの entity_id。ルーティングの検証に使う。
    [[nodiscard]] std::vector<std::string> strip_ids() const;

    /// 子機へ制御メッセージを送る。
    void send(PeerHandle peer, const wire::HostToClientMessage& message);

    // --- UI への通知 ---

    void publish_session();
    void publish_master();
    void publish_channel(const Participant& participant);
    void publish_pending();
    void publish_routing();
    void publish_route_level(const control_protocol::RouteLevel& level);
    void notice(control_protocol::NoticeSeverity severity, const char* code,
                const std::string& name);
    void log(common::LogLevel level, const char* message);

    /// 親機モードの全状態を返す。
    [[nodiscard]] control_protocol::MixerSnapshot snapshot() const;

    HostSession::Config config;
    HostSession::Ports ports;
    HostSettings settings;
    control_protocol::SessionInfo session;
    control_protocol::MasterStrip master;
    RendezvousId rendezvous_id{};
    /// 招待の登録に失敗したあと、次に発行し直してよい時刻。
    std::optional<common::Clock::TimePoint> next_issue_at;
    InviteBook invites;
    ParticipantRegistry registry;
    RoutingBook routing;
};

}  // namespace vocroz::session

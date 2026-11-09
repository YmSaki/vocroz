// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機のセッション管理。pa-engine の中で、UI からのコマンド、子機からの制御メッセージ、
// 接続と出力デバイスの変化を受け取り、セッションの状態の正（エンジン主権）を保持する。
//
// 責務:
// - セッション、接続、出力デバイスの状態機械を進める（src/state/）
// - 招待の発行、期限切れでの発行し直し、失効と、申請の招待トークンの照合
// - 承認と拒否、再接続トークンによる同じストリップへの復帰、全員 LOST での READY への復帰
// - コマンドの適用（revision の競合の拒否、重複した request_id の無視）と認可
// - 初期ミュートと、出力デバイスの抜去時の Master の SAFETY ミュート
// - 出力デバイスの選択（保存したデバイスだけを開き、ほかのデバイスへフォールバックしない）
// 副作用（ミキサー、接続、ランデブーサーバー、設定、UI への通知）はすべてポート経由で行う。
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/identifiers.h"
#include "vocroz/session/ports/mixer_control.h"
#include "vocroz/session/ports/output_device_control.h"
#include "vocroz/session/ports/peer_link.h"
#include "vocroz/session/ports/playout_control.h"
#include "vocroz/session/ports/rendezvous_client.h"
#include "vocroz/session/ports/secure_random.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::session {

/// 親機のセッション管理。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドからだけ呼ぶ。
/// オーディオスレッドとネットワーク受信スレッドからの通知は、コンポジションルートが
/// コントロールスレッドへ移してから渡す。
class HostSession {
public:
    /// 親機の固定の設定。
    struct Config {
        /// 親機の Noise の静的公開鍵。招待に含める。
        HostPublicKey host_public_key{};
        /// 招待 URL の基点（ランデブーサーバーの URL。例: "https://vocroz.example"）。
        std::string invite_base_url;
    };

    /// 注入するポート。ライフタイムはコンポジションルートが保証する（AGENTS.md 4.3）。
    struct Ports {
        UiEventSink& ui;
        MixerControl& mixer;
        OutputDeviceControl& outputs;
        PeerLink& peers;
        RendezvousClient& rendezvous;
        SettingsStore& settings;
        SecureRandom& random;
        const common::Clock& clock;
        common::Logger& logger;
        PlayoutControl& playout;
    };

    /// @param config 固定の設定
    /// @param ports  注入するポート
    HostSession(Config config, Ports ports);
    ~HostSession();

    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    /// 設定を読み、初期状態（Master ミュート、セッションなし）をミキサーへ反映する。
    /// 設定が読めなくても既定値で始める。ミュートは保存しないので、前回音が出ていても必ず
    /// ミュートから始まる。保存した出力デバイスがあればそのデバイスだけを開き、開けなければ
    /// 未選択のままにする。起動時に 1 回だけ呼ぶ。
    void start();

    /// UI からのメッセージを適用する。応答とイベントは UiEventSink へ送る。
    /// 離散コマンドは、重複した request_id なら何も返さず無視し、対象がなければ NOT_FOUND、
    /// revision が違えば REVISION_CONFLICT、親機で適用できないか今の状態では適用できなければ
    /// INVALID_STATE で拒否する。
    /// @param message control-protocol のデコーダーで検証済みのメッセージ
    void handle_ui_message(const control_protocol::UiToEngineMessage& message);

    /// 招待の登録の結果を受け取る。
    /// @param token  登録を要求した招待トークン（古い要求の結果を見分けるため）
    /// @param result 登録の結果
    void on_invite_registered(
        const InviteToken& token,
        const common::Result<RegisteredInvite, control_protocol::ErrorCode>& result);

    /// 子機から制御メッセージが届いた。形式が不正か、子機から親機への種別でなければ捨てる。
    /// 子機は自分の状態の通知しか送れず、Master や他のチャンネルを操作する手段はない。
    /// @param peer    届いた接続
    /// @param message 制御メッセージの本文
    void on_peer_message(PeerHandle peer, std::span<const std::uint8_t> message);

    /// 承認した接続の経路が確立し、メディアを受け付け始めた（CONNECTING → CONNECTED）。
    /// @param peer 接続
    void on_peer_established(PeerHandle peer);

    /// 承認した接続の経路を確立できなかった（CONNECTING → FAILED、UNREACHABLE）。
    /// @param peer 接続
    void on_peer_path_failed(PeerHandle peer);

    /// 接続から 5 秒受信が途絶えた（→ RECONNECTING）。
    /// @param peer 接続
    void on_peer_timeout(PeerHandle peer);

    /// 途絶えていた接続から、またパケットが届いた（RECONNECTING → CONNECTED）。
    /// @param peer 接続
    void on_peer_resumed(PeerHandle peer);

    /// 接続が閉じられた（→ LOST。ストリップは残す）。
    /// @param peer 接続
    void on_peer_closed(PeerHandle peer);

    /// 接続の信号状態が変わった。CONNECTED のときだけ反映する。
    /// @param peer  接続
    /// @param state 新しい信号状態
    void on_signal_state(PeerHandle peer, control_protocol::SignalState state);

    /// 使用中の出力デバイスが消えた。出力を閉じ、Master を SAFETY でミュートし、
    /// 他のデバイスへは切り替えない。オペレーターが SelectOutput で選び直すまで LOST のまま。
    void on_output_device_lost();

    /// エンジンの致命的な異常を知らせる。セッションを ERROR にし、Master を SAFETY でミュートする。
    /// @param code 原因
    void on_engine_fault(control_protocol::ErrorCode code);

    /// 現在の招待を失効させ、新しい招待を発行する（招待の手動の失効）。
    /// READY と ACTIVE のときだけ効く。
    void revoke_invite();

    /// 時刻で決まる処理（招待の期限、再接続の猶予）を進める。100 ms〜1 s の間隔で呼ぶ。
    void tick();

    /// 親機モードの全状態を返す（UI の接続時のスナップショットと同じ内容）。
    [[nodiscard]] control_protocol::MixerSnapshot snapshot() const;

private:
    // 状態と処理の本体は src/ の型（状態機械、参加者の一覧、招待の記録、制御メッセージ）に
    // 依存するので、公開ヘッダーに出さないよう実装の中に閉じ込める。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::session

// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機のセッション管理。remote-pa の子機モードの中で、UI からのコマンド、招待の解決の結果、
// 親機からの制御メッセージ、入力デバイスの変化を受け取り、子機の状態の正（エンジン主権）を保持する。
//
// 責務:
// - 子機の接続状態（ClientState）の状態機械を進める（src/state/）
// - 招待の文字列のパース、参加の申請、承認待ち、再接続トークンによる同じストリップへの復帰
// - 送信ミュート（初期ミュート、ミュートの世代番号）、入力デバイスの選択と「入力なし」の検知
// - 返送モニターを鳴らす出力デバイスの選択と抜去の検知（docs/adr/0036-return-monitor.md）
// - コマンドの適用（revision の競合の拒否、重複した request_id の無視）
// 副作用（キャプチャ、親機への接続、ランデブーサーバー、設定、UI への通知）はポート経由で行う。
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vocroz/common/ports/clock.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/control_protocol/commands.h"
#include "vocroz/control_protocol/entities.h"
#include "vocroz/control_protocol/states.h"
#include "vocroz/session/invite_code.h"
#include "vocroz/session/ports/capture_control.h"
#include "vocroz/session/ports/host_link.h"
#include "vocroz/session/ports/output_device_control.h"
#include "vocroz/session/ports/rendezvous_client.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/session/ports/ui_event_sink.h"

namespace vocroz::session {

/// 子機のセッション管理。
/// スレッド安全性: すべてのメンバー関数をコントロールスレッドからだけ呼ぶ。
class ClientSession {
public:
    /// 注入するポート。ライフタイムはコンポジションルートが保証する（AGENTS.md 4.3）。
    struct Ports {
        UiEventSink& ui;
        CaptureControl& capture;
        HostLink& link;
        /// 返送モニターを鳴らす出力デバイス。
        OutputDeviceControl& outputs;
        RendezvousClient& rendezvous;
        SettingsStore& settings;
        const common::Clock& clock;
        common::Logger& logger;
    };

    /// @param ports 注入するポート
    explicit ClientSession(Ports ports);
    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    /// 設定を読み、初期状態（送信ミュート、未接続）をキャプチャへ反映する。
    /// 保存した返送の出力デバイスがあれば開き、開けなければ「出力なし」の表示にする。
    /// 設定が読めなくても既定値で始める。起動時に 1 回だけ呼ぶ。
    void start();

    /// UI からのメッセージを適用する。応答とイベントは UiEventSink へ送る。
    /// 離散コマンドは、重複した request_id なら無視し、対象がなければ NOT_FOUND、revision が
    /// 違えば REVISION_CONFLICT、招待の形式が正しくなければ INVITE_INVALID、子機で適用できないか
    /// 今の状態では適用できなければ INVALID_STATE で拒否する。
    /// @param message control-protocol のデコーダーで検証済みのメッセージ
    void handle_ui_message(const control_protocol::UiToEngineMessage& message);

    /// 招待コードの解決の結果を受け取る。
    /// @param code   解決を要求した招待コード（古い要求の結果を見分けるため）
    /// @param result 解決の結果
    void on_invite_resolved(
        const InviteCode& code,
        const common::Result<ResolvedInvite, control_protocol::ErrorCode>& result);

    /// 親機への経路と鍵交換ができた（最初の接続か、再接続）。参加か再接続を申請する。
    void on_link_established();

    /// 親機から制御メッセージが届いた。形式が不正か、親機から子機への種別でなければ捨てる。
    /// @param message 制御メッセージの本文
    void on_link_message(std::span<const std::uint8_t> message);

    /// 親機からの受信が 5 秒途絶えた。承認後なら RECONNECTING にして再接続を始める。
    void on_link_lost();

    /// 途絶えていた接続から、またパケットが届いた（RECONNECTING → CONNECTED）。
    void on_link_resumed();

    /// 親機への経路を確立できなかった。
    /// @param reason 原因（通常は UNREACHABLE）
    void on_link_failed(control_protocol::ErrorCode reason);

    /// 承認後の経路の確立が終わり、送信を始められる（CONNECTING → CONNECTED）。
    void on_media_established();

    /// 入力デバイスの一覧が変わった（列挙の完了、抜き差し）。
    /// 選択中のデバイスがなくなったら「入力なし」にし、送信ミュートの状態は変えない。
    /// @param devices 今ある入力デバイス
    void on_input_devices_changed(std::vector<control_protocol::InputDevice> devices);

    /// 返送を鳴らしていた出力デバイスが抜去された。出力を閉じ、ほかのデバイスへは切り替えない。
    /// 選択中の ID は残し、出演者が選び直すのを待つ。
    void on_output_device_lost();

    /// 時刻で決まる処理（再接続の猶予）を進める。100 ms〜1 s の間隔で呼ぶ。
    void tick();

    /// 子機モードの全状態を返す（UI の接続時のスナップショットと同じ内容）。
    [[nodiscard]] control_protocol::ClientSnapshot snapshot() const;

private:
    // 状態と処理の本体は src/ の型（状態機械、制御メッセージ）に依存するので、
    // 公開ヘッダーに出さないよう実装の中に閉じ込める。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vocroz::session

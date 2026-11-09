// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のコンポジションルート（Windows だけ）。
// アダプター（名前付きパイプ、WASAPI、名前付きミューテックス、スリープの抑止）と
// ドメイン（audio-core の Mixer、session の HostSession）を生成して注入し、
// コントロールスレッドのループを回す（AGENTS.md 4.3、docs/adr/0028-engine-assembly.md）。
//
// スレッド（AGENTS.md 2.3）:
// - コントロールスレッド: run() を呼んだメインスレッド。session、ミキサーの操作、UI への送信
// - オーディオスレッド: device の出力のスレッド（Mixer::render() だけ）
// - パイプの受信スレッド: UI のメッセージのデコード。session へはループに積んで渡す
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

#include "vocroz/audio_core/mixer.h"
#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/audio_core/ports/audio_output.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/steady_clock.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/network/https_rendezvous_client.h"
#include "vocroz/network/ports/https_transport.h"
#include "vocroz/session/host_session.h"
#include "vocroz/session/ports/secret_store.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/transport/static_key_pair.h"

#include "control_loop.h"
#include "crypto_secure_random.h"
#include "device_loss_relay.h"
#include "engine_activity.h"
#include "engine_mixer.h"
#include "invite_result_relay.h"
#include "launch_options.h"
#include "offline_rendezvous_client.h"
#include "ui_event_publisher.h"
#include "ui_message_receiver.h"
#include "unconnected_peer_link.h"
#include "unconnected_playout_control.h"
#include "win/execution_state_sleep_inhibitor.h"
#include "win/win32_named_mutex.h"

namespace vocroz::engine {

/// pa-engine の組み立てと実行。
/// スレッド安全性: メインスレッドで作り、run() もメインスレッドから 1 回だけ呼ぶ。
class CompositionRoot {
public:
    /// @param options 検証済みの起動オプション（command は kRun）
    explicit CompositionRoot(const LaunchOptions& options);
    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;
    ~CompositionRoot();

    /// エンジンを起動し、停止を要求されるか自動終了の条件を満たすまで動かす。
    /// @return 終了コード（engine_constants.h）
    [[nodiscard]] int run();

private:
    // 起動の段階。失敗したら終了コードを返す。
    [[nodiscard]] std::optional<int> start();
    void open_saved_output_device();
    void on_output_device_lost();
    void on_tick();
    void apply_activity();
    void shutdown();

    // ランデブーサーバーとの通信を、ビルドで与えた URL の HTTPS で始める。URL がないか
    // HTTPS を使えなければ、オフラインと同じ振る舞い（セッションを作れない）にする。
    void create_rendezvous_client();

    LaunchOptions options_;
    // 絶対パスにそろえた --data-dir。決められなければ空（start() が終了コード 1 で終わる）。
    std::filesystem::path data_dir_;

    // --- 横断的なアダプター（ドメインより先に作り、後に破棄する） ---
    // <data-dir>\logs のログファイル。データフォルダを決められなければ標準エラー出力。
    std::unique_ptr<common::Logger> logger_;
    common::SteadyClock clock_;
    ControlLoop loop_;
    std::optional<win::Win32NamedMutex> instance_lock_;
    std::optional<win::Win32NamedMutex> session_lock_;
    win::ExecutionStateSleepInhibitor sleep_inhibitor_;
    std::unique_ptr<control_protocol::MessageChannel> ui_channel_;
    std::unique_ptr<audio_core::AudioDeviceCatalog> device_catalog_;

    // --- session のポートの実装 ---
    std::unique_ptr<session::SettingsStore> settings_;
    std::unique_ptr<session::SecretStore> secrets_;
    // ランデブーサーバーとの通信。https_rendezvous_ があればそれを、なければ offline_rendezvous_
    // を session に渡す。https_rendezvous_ は破棄時に残りの要求（失効）を送り終えるまで待つので、
    // それが使う https_transport_ と invite_relay_ より後に宣言する（先に破棄する）。
    InviteResultRelay invite_relay_;
    OfflineRendezvousClient offline_rendezvous_;
    std::unique_ptr<network::HttpsTransport> https_transport_;
    std::unique_ptr<network::HttpsRendezvousClient> https_rendezvous_;
    session::RendezvousClient* rendezvous_ = &offline_rendezvous_;
    CryptoSecureRandom random_;
    UnconnectedPeerLink peers_;
    UnconnectedPlayoutControl playout_;
    std::optional<transport::StaticKeyPair> host_keys_;
    audio_core::Mixer mixer_;
    EngineMixer engine_mixer_;
    std::unique_ptr<UiEventPublisher> ui_publisher_;
    std::unique_ptr<session::HostSession> session_;
    std::unique_ptr<UiMessageReceiver> ui_receiver_;
    DeviceLossRelay device_loss_relay_;
    std::unique_ptr<audio_core::AudioOutput> output_;

    // --- 周期処理の状態 ---
    IdleShutdownTimer idle_timer_;
    common::Clock::TimePoint next_session_tick_{};
    bool session_exists_ = false;
    int exit_code_ = 0;
};

}  // namespace vocroz::engine

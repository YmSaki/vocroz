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
// - ネットワーク受信スレッド: network の UDP ソケットのスレッド。子機のメディアの復号から
//   ジッターバッファへの投入まで（PeerReceivePath）。鍵交換と制御はループに積んで渡す
// - JUCE のメッセージスレッド（JUCE を使うビルドだけ）: MIDI デバイスの一覧、開閉、抜去の通知と、
//   挿入のプラグインの読み込みと破棄。グラフへの反映はループに積んで渡す
//   （docs/adr/0039-pa-engine-juce-message-thread.md、docs/adr/0042-plugin-insert.md）
// - プラグインのスキャンのスレッド（JUCE を使うビルドだけ）: ファイルごとにスキャン用ヘルパーを
//   起動する。使えるプラグインの一覧はループに積んで渡す（docs/adr/0042-plugin-insert.md）
// - SRTP 外部出力の送信スレッド（送信中と接続テスト中だけ）: RTP、SRTP、宛先への送信。
//   接続テストの結果はループに積んで渡す（docs/adr/0040-srtp-external-output.md）
// - JUCE の MIDI の受信スレッド: MIDI デバイスのメッセージをキューに積むだけ
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
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/session/host_session.h"
#include "vocroz/session/peer_midi_inbox.h"
#include "vocroz/session/ports/plugin_catalog_store.h"
#include "vocroz/session/ports/plugin_scanner.h"
#include "vocroz/session/ports/secret_store.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/transport/crypto_random_source.h"
#include "vocroz/transport/static_key_pair.h"

#include "control_loop.h"
#include "crypto_secure_random.h"
#include "device_loss_relay.h"
#include "device_midi_inputs.h"
#include "device_midi_relay.h"
#include "engine_activity.h"
#include "engine_mixer.h"
#include "engine_output_devices.h"
#include "engine_plugin_rack.h"
#include "external_output_driver.h"
#include "host_peer_links.h"
#include "host_rendezvous_driver.h"
#include "invite_result_relay.h"
#include "launch_options.h"
#include "network_external_socket_opener.h"
#include "no_plugin_rack.h"
#include "no_plugin_scan_control.h"
#include "offline_rendezvous_client.h"
#include "peer_receive_path.h"
#include "peer_receive_relay.h"
#include "plugin_scan_worker.h"
#include "rendezvous_id_tap.h"
#include "session_peer_events.h"
#include "thread_peer_send_scheduler.h"
#include "ui_event_publisher.h"
#include "ui_message_receiver.h"
#include "win/execution_state_sleep_inhibitor.h"
#include "win/win32_named_mutex.h"
#if defined(VOCROZ_PA_ENGINE_HAS_JUCE)
#include "vocroz/audio_core/ports/midi_device_catalog.h"

#include "juce/juce_message_thread.h"
#endif

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
    void on_tick();
    void apply_activity();
    void shutdown();

    // ランデブーサーバーとの通信を、ビルドで与えた URL の HTTPS で始める。URL がないか
    // HTTPS を使えなければ、オフラインと同じ振る舞い（セッションを作れない）にする。
    void create_rendezvous_client();
    // 子機の受信（UDP ソケット、ランデブーへの登録、接続）を用意する。
    // ソケットを開けなければ false を返す。
    [[nodiscard]] bool create_peer_network();
    // MIDI デバイスの一覧を読み直すよう、メッセージスレッドへ頼む。
    // JUCE を使わないビルドでは何もしない。
    void request_midi_refresh();
    // JUCE のメッセージスレッドを動かし、挿入のプラグインの読み込み（メッセージスレッド）と
    // スキャン（専用のスレッド）を用意する。JUCE を使わないビルドでは何もせず、挿入は
    // 読み込まない（NoPluginRack）。
    void start_juce();
    // MIDI デバイスを閉じ、プラグインを破棄して、メッセージスレッドを止める。
    // オーディオ出力を閉じてから呼ぶ（グラフが持つプラグインの処理をもう呼ばせないため）。
    void stop_juce();

    LaunchOptions options_;
    // 絶対パスにそろえた --data-dir。決められなければ空（start() が終了コード 1 で終わる）。
    std::filesystem::path data_dir_;

    // --- 横断的なアダプター（ドメインより先に作り、後に破棄する） ---
    // <data-dir>\logs のログファイル。データフォルダを決められなければ標準エラー出力。
    std::unique_ptr<common::Logger> logger_;
    common::SteadyClock clock_;
    ControlLoop loop_;
#if defined(VOCROZ_PA_ENGINE_HAS_JUCE)
    // MIDI デバイスとプラグインが使う JUCE のメッセージスレッド。止めた後も post() は false を
    // 返すだけなので、それを使うもの（plugin_rack_ と、グラフが持つプラグインの処理）より前に
    // 宣言する（後に破棄する）。
    JuceMessageThread juce_thread_;
#endif
    std::optional<win::Win32NamedMutex> instance_lock_;
    std::optional<win::Win32NamedMutex> session_lock_;
    win::ExecutionStateSleepInhibitor sleep_inhibitor_;
    std::unique_ptr<control_protocol::MessageChannel> ui_channel_;
    std::unique_ptr<audio_core::AudioDeviceCatalog> device_catalog_;

    // --- session のポートの実装 ---
    std::unique_ptr<session::SettingsStore> settings_;
    std::unique_ptr<session::SecretStore> secrets_;
    // 挿入のプラグインの読み込み。engine_plugin_rack_ があればそれを、なければ（JUCE を使わない
    // ビルド）no_plugin_rack_ を session に渡す。グラフ（mixer_）が持つプラグインの処理は
    // engine_plugin_rack_ に手放したことを知らせるので、mixer_ より前に宣言する（後に破棄する）。
    NoPluginRack no_plugin_rack_;
    std::unique_ptr<EnginePluginRack> engine_plugin_rack_;
    session::PluginRack* plugin_rack_ = &no_plugin_rack_;
    // スキャンの記録の保存先とスキャナー。スキャンのスレッド（plugin_scan_）より前に宣言する。
    std::unique_ptr<session::PluginCatalogStore> plugin_catalog_store_;
    std::unique_ptr<session::PluginScanner> plugin_scanner_;
    // スキャンのスレッド。session にスキャンし直しの依頼先として渡すので session_ より前に宣言する
    // （後に破棄する）。結果は loop_ に積んで session_ に渡すので、shutdown() で session_ より先に
    // 止める。なければ（JUCE を使わないビルド）no_plugin_scan_ を渡す。
    std::unique_ptr<PluginScanWorker> plugin_scan_;
    NoPluginScanControl no_plugin_scan_;
    session::PluginScanControl* plugin_scan_control_ = &no_plugin_scan_;
    // ランデブーサーバーとの通信。https_rendezvous_ があればそれを、なければ offline_rendezvous_
    // を session に渡す。https_rendezvous_ は破棄時に残りの要求（失効）を送り終えるまで待つので、
    // それが使う https_transport_ と invite_relay_ より後に宣言する（先に破棄する）。
    InviteResultRelay invite_relay_;
    OfflineRendezvousClient offline_rendezvous_;
    std::unique_ptr<network::HttpsTransport> https_transport_;
    std::unique_ptr<network::HttpsRendezvousClient> https_rendezvous_;
    session::RendezvousClient* rendezvous_ = &offline_rendezvous_;
    // session に渡す RendezvousClient。登録の要求に載ったランデブー ID で UDP の登録を始める。
    std::unique_ptr<RendezvousIdTap> rendezvous_tap_;
    CryptoSecureRandom random_;
    transport::CryptoRandomSource network_random_;
    std::optional<transport::StaticKeyPair> host_keys_;
    audio_core::Mixer mixer_;
    EngineMixer engine_mixer_;
    // 子機の MIDI
    // の受け口。ストリップのキューはグラフも参照する（docs/adr/0029-midi-and-vsti.md）。
    session::PeerMidiInbox midi_inbox_;
    // MIDI デバイスの開閉を、ループでグラフに反映する。
    DeviceMidiRelay device_midi_relay_;
    // 抜去の後始末は session に任せる（session が出力を閉じ、Master を SAFETY でミュートする）。
    DeviceLossRelay device_loss_relay_;
    // 開いている出力はミキサーと device_loss_relay_ を呼ぶので、それらより後に宣言する。
    std::unique_ptr<EngineOutputDevices> outputs_;

    // --- 子機の受信（docs/adr/0028-engine-assembly.md） ---
    // 受信スレッドは endpoint_ が持ち、endpoint_ の破棄で止まる。受信スレッドが使うもの
    // （peer_relay_、peer_receive_、router_、peer_links_ が持つ接続）は、
    // endpoint_ より前に宣言する（後に破棄する）。
    SessionPeerEvents peer_events_;
    PeerReceiveRelay peer_relay_;
    PeerReceivePath peer_receive_;
    network::ReceiveRouter router_;
    std::unique_ptr<network::NetworkEnvironment> network_environment_;
    // 子機ごとの送信スレッドを作る。送信スレッドは endpoint_ で送るので、shutdown() で
    // endpoint_ より先に止める（docs/adr/0036-return-monitor.md）。
    ThreadPeerSendScheduler peer_sending_;
    std::unique_ptr<HostPeerLinks> peer_links_;
    std::unique_ptr<network::UdpEndpoint> endpoint_;
    // endpoint_ で送るので、endpoint_ より後に宣言する（先に破棄する）。
    std::unique_ptr<HostRendezvousDriver> rendezvous_driver_;
    // --- SRTP 外部出力（docs/adr/0040-srtp-external-output.md） ---
    // 宛先ごとのソケットは network_environment_ で名前解決し、子機との endpoint_ とは別に開く。
    // ドライバーは engine_mixer_ にタップをつなぐので、それより後に宣言する（先に破棄する）。
    std::unique_ptr<NetworkExternalSocketOpener> external_sockets_;
    std::unique_ptr<ExternalOutputDriver> external_output_;
    std::unique_ptr<UiEventPublisher> ui_publisher_;
    std::unique_ptr<session::HostSession> session_;
    std::unique_ptr<UiMessageReceiver> ui_receiver_;

#if defined(VOCROZ_PA_ENGINE_HAS_JUCE)
    // --- MIDI デバイス（docs/adr/0039-pa-engine-juce-message-thread.md） ---
    // midi_catalog_ と device_midi_ は JUCE の資源を持つので、メッセージスレッドの上で作り、
    // stop_juce() でメッセージスレッドの上で破棄する。
    std::unique_ptr<audio_core::MidiDeviceCatalog> midi_catalog_;
    std::unique_ptr<DeviceMidiInputs> device_midi_;
#endif

    // --- 周期処理の状態 ---
    IdleShutdownTimer idle_timer_;
    common::Clock::TimePoint next_session_tick_{};
    common::Clock::TimePoint next_midi_scan_{};
    bool session_exists_ = false;
    int exit_code_ = 0;
};

}  // namespace vocroz::engine

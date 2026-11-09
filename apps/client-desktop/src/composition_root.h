// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードのコンポジションルート（Windows で JUCE を使うビルドだけ）。
// アダプター（WebView、WASAPI、Winsock、WinHTTP、設定の保存）を作って ClientRuntime に注入し、
// JUCE のタイマーで ClientRuntime::poll()
// を回す（AGENTS.md 4.3、docs/adr/0030-remote-pa-assembly.md）。
//
// スレッド（AGENTS.md 2.3）:
// - コントロールスレッド: JUCE のメッセージスレッド。生成、破棄、poll()、UI からのメッセージ
// - オーディオスレッド: WASAPI のキャプチャのスレッド（CaptureProcessor）
// - 送信スレッド: ClientRuntime が持つ SenderThread
// - ネットワーク受信スレッド: Winsock のソケットの受信スレッド（ReceiveRouter と PeerInbox）
// - HTTPS の作業用スレッド: HttpsRendezvousClient（結果は InviteInbox へ）
#pragma once

#include <memory>

#include "vocroz/audio_core/ports/audio_device_catalog.h"
#include "vocroz/client/client_app.h"
#include "vocroz/common/ports/logger.h"
#include "vocroz/common/steady_clock.h"
#include "vocroz/network/ports/https_transport.h"
#include "vocroz/network/ports/network_environment.h"
#include "vocroz/network/ports/udp_endpoint.h"
#include "vocroz/network/receive_router.h"
#include "vocroz/session/ports/rendezvous_client.h"
#include "vocroz/session/ports/settings_store.h"
#include "vocroz/transport/crypto_random_source.h"
#include "vocroz/webui/web_ui_host.h"

#include "client_runtime.h"
#include "invite_inbox.h"
#include "peer_inbox.h"
#include "win/wasapi_input_device_opener.h"

namespace vocroz::client {

/// 子機モードの組み立てと実行。
/// スレッド安全性: JUCE のメッセージスレッドで作り、同じスレッドで破棄する。
class CompositionRoot final : public ClientApp {
public:
    /// アダプターを作り、画面を開いて子機モードを始める。
    /// @param config 起動の設定
    /// @param logger ログの出力先。戻り値より長く生存させる
    /// @return 実行中の子機モード。起動できなければその原因
    [[nodiscard]] static common::Result<std::unique_ptr<CompositionRoot>, ClientAppError> create(
        ClientAppConfig config, common::Logger& logger);

    /// create() だけが作れるようにする鍵（std::make_unique から使うため公開する）。
    class ConstructionKey {
        friend class CompositionRoot;
        ConstructionKey() = default;
    };

    CompositionRoot(ConstructionKey key, ClientAppConfig config, common::Logger& logger);
    ~CompositionRoot() override;

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

private:
    class PollTimer;

    [[nodiscard]] common::Result<void, ClientAppError> start();
    void make_rendezvous_client();
    void on_close_requested();

    ClientAppConfig config_;
    common::Logger& logger_;

    // --- 横断的なアダプター（ドメインより先に作り、後に破棄する） ---
    common::SteadyClock clock_;
    transport::CryptoRandomSource random_;
    std::unique_ptr<session::SettingsStore> settings_;
    std::unique_ptr<audio_core::AudioDeviceCatalog> devices_;
    win::WasapiInputDeviceOpener inputs_;
    std::unique_ptr<network::NetworkEnvironment> environment_;

    // --- 招待の解決（作業用スレッドの結果を受け箱へ） ---
    InviteInbox invites_;
    std::unique_ptr<network::HttpsTransport> https_;
    std::unique_ptr<session::RendezvousClient> rendezvous_;
    std::string rendezvous_host_;

    // --- UDP の受信（受信スレッドを持つソケットを、受け取る側より後に作り、先に破棄する） ---
    PeerInbox peer_inbox_;
    network::ReceiveRouter router_;
    std::unique_ptr<network::UdpEndpoint> endpoint_;

    // --- 画面と子機の動作 ---
    std::unique_ptr<webui::WebUiHost> web_ui_;
    std::unique_ptr<ClientRuntime> runtime_;
    // poll() のタイマーは最後に作り、最初に止める。
    std::unique_ptr<PollTimer> timer_;
};

}  // namespace vocroz::client

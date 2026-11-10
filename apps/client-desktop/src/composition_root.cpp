// SPDX-License-Identifier: AGPL-3.0-only
//
// composition_root.h の実装と、make_client_app()。

#include "composition_root.h"

#include <utility>

#include <juce_events/juce_events.h>

#include "vocroz/device/factories.h"
#include "vocroz/network/factories.h"
#include "vocroz/network/https_rendezvous_client.h"
#include "vocroz/network/rendezvous_url.h"
#include "vocroz/persistence/factories.h"
#include "vocroz/transport/noise_ik_handshake.h"
#include "vocroz/webui/factories.h"

#include "client_constants.h"
#include "unavailable_rendezvous_client.h"

namespace vocroz::client {

/// ClientRuntime::poll() を一定の間隔で呼ぶ JUCE のタイマー。
/// タイマーのコールバックはメッセージスレッドで実行されるので、UI からのメッセージと同じ
/// スレッド（コントロールスレッド）で session が動く。
class CompositionRoot::PollTimer final : public juce::Timer {
public:
    explicit PollTimer(ClientRuntime& runtime) : runtime_(runtime) {
        startTimer(static_cast<int>(kPollInterval.count()));
    }
    ~PollTimer() override {
        stopTimer();
    }
    PollTimer(const PollTimer&) = delete;
    PollTimer& operator=(const PollTimer&) = delete;

    void timerCallback() override {
        runtime_.poll();
    }

private:
    ClientRuntime& runtime_;
};

common::Result<std::unique_ptr<CompositionRoot>, ClientAppError> CompositionRoot::create(
    ClientAppConfig config, common::Logger& logger) {
    auto root = std::make_unique<CompositionRoot>(ConstructionKey{}, std::move(config), logger);
    if (auto started = root->start(); !started) {
        return common::make_unexpected(started.error());
    }
    return root;
}

CompositionRoot::CompositionRoot(ConstructionKey /*key*/, ClientAppConfig config,
                                 common::Logger& logger)
    : config_(std::move(config)),
      logger_(logger),
      inputs_(logger),
      app_audio_(logger),
      outputs_(logger) {}

CompositionRoot::~CompositionRoot() {
    // 周期の処理を止めてから、参加中なら切断の通知を送る。送信スレッドは ClientRuntime の
    // 破棄のときに、積まれたパケットを送り切ってから止まる。
    timer_.reset();
    if (runtime_) {
        runtime_->leave();
    }
}

common::Result<void, ClientAppError> CompositionRoot::start() {
    // 乱数が使えないと、接続 ID と鍵が推測できる値になるので起動しない。
    if (!transport::generate_connection_id()) {
        logger_.log(common::LogLevel::kError, "暗号ライブラリを初期化できません");
        return common::make_unexpected(ClientAppError::kCryptoUnavailable);
    }
    settings_ = persistence::make_json_settings_store(config_.data_dir, logger_);
    devices_ = device::make_wasapi_audio_device_catalog(logger_);
    environment_ = network::make_win_network_environment();
    make_rendezvous_client();

    // 返送は低遅延プロファイル（docs/adr/0002-opus-low-delay.md）のモノラルで受ける。
    auto returns = transport::PlayoutStream::create(transport::PlayoutConfig{});
    if (!returns) {
        logger_.log(common::LogLevel::kError, "返送の Opus のデコーダーを作れません");
        return common::make_unexpected(ClientAppError::kEncoderFailed);
    }
    returns_ = std::move(returns).value();
    receive_path_ = std::make_unique<HostReceivePath>(peer_inbox_, *returns_);
    router_ = std::make_unique<network::ReceiveRouter>(*receive_path_);

    // ポート 0 で OS に選ばせる。子機は待ち受けず、ホールパンチで相手に知らせたポートだけを使う。
    auto endpoint = network::make_winsock_udp_endpoint(0, *router_);
    if (!endpoint) {
        logger_.log(common::LogLevel::kError, "UDP のソケットを開けません");
        return common::make_unexpected(ClientAppError::kSocketFailed);
    }
    endpoint_ = std::move(endpoint).value();

    webui::WebUiHostOptions ui_options;
    ui_options.window = {.title = "VOCROZ",
                         .width = kWindowWidth,
                         .height = kWindowHeight,
                         .min_width = kWindowMinWidth,
                         .min_height = kWindowMinHeight};
    ui_options.launch = {
        .mode = webui::UiMode::kClient, .lang = config_.lang, .join = config_.join};
    ui_options.assets = config_.ui_assets;
    ui_options.user_data_folder = config_.data_dir / kWebViewDataFolderName;
    ui_options.on_close_requested = [this] { on_close_requested(); };
    web_ui_ = webui::make_juce_web_ui_host(std::move(ui_options));

    auto runtime =
        ClientRuntime::create(ClientRuntime::Ports{.ui_channel = web_ui_->channel(),
                                                   .endpoint = *endpoint_,
                                                   .router = *router_,
                                                   .inbox = peer_inbox_,
                                                   .path = *receive_path_,
                                                   .returns = *returns_,
                                                   .environment = *environment_,
                                                   .devices = *devices_,
                                                   .inputs = inputs_,
                                                   .app_audio = app_audio_,
                                                   .outputs = outputs_,
                                                   .rendezvous = *rendezvous_,
                                                   .invites = invites_,
                                                   .settings = *settings_,
                                                   .random = random_,
                                                   .clock = clock_,
                                                   .logger = logger_},
                              ClientRuntime::Config{.rendezvous_host = rendezvous_host_,
                                                    .rendezvous_port = kRendezvousUdpPort,
                                                    .encoder = {}});
    if (!runtime) {
        switch (runtime.error()) {
            case ClientStartError::kKeyGenerationFailed:
                logger_.log(common::LogLevel::kError, "子機の鍵を生成できません");
                return common::make_unexpected(ClientAppError::kCryptoUnavailable);
            case ClientStartError::kEncoderFailed:
                logger_.log(common::LogLevel::kError, "Opus のエンコーダーを作れません");
                return common::make_unexpected(ClientAppError::kEncoderFailed);
        }
        return common::make_unexpected(ClientAppError::kEncoderFailed);
    }
    runtime_ = std::move(runtime).value();
    runtime_->start();
    timer_ = std::make_unique<PollTimer>(*runtime_);
    logger_.log(common::LogLevel::kInfo, "子機モードを起動しました");
    return {};
}

void CompositionRoot::make_rendezvous_client() {
    // 招待の HTTPS と UDP のランデブーは、同じベース URL のサーバーを使う
    // （docs/adr/0030-remote-pa-assembly.md）。URL
    // がないか形が誤っていれば、参加だけをできなくする。
    if (config_.rendezvous_url.empty()) {
        logger_.log(common::LogLevel::kWarning,
                    "ランデブーサーバーの URL が指定されていないため、参加できません");
    } else if (const auto url = network::parse_rendezvous_url(config_.rendezvous_url); !url) {
        logger_.log(common::LogLevel::kError, "ランデブーサーバーの URL の形が誤っています");
    } else if (auto transport = network::make_winhttp_https_transport(config_.rendezvous_url);
               !transport) {
        logger_.log(common::LogLevel::kError, "ランデブーサーバーとの HTTPS を準備できません");
    } else {
        https_ = std::move(transport).value();
        rendezvous_host_ = url.value().host;
        rendezvous_ = std::make_unique<network::HttpsRendezvousClient>(*https_, invites_);
        return;
    }
    rendezvous_ = std::make_unique<UnavailableRendezvousClient>(invites_);
}

void CompositionRoot::on_close_requested() {
    // 参加中に閉じると演奏が途切れるので、参加中だけモーダルで確かめる（既定はキャンセル）。
    if (runtime_ && runtime_->in_session()) {
        if (!config_.confirm_leave || !config_.confirm_leave()) {
            return;
        }
        runtime_->leave();
    }
    if (config_.quit) {
        config_.quit();
    }
}

common::Result<std::unique_ptr<ClientApp>, ClientAppError> make_client_app(ClientAppConfig config,
                                                                           common::Logger& logger) {
    auto root = CompositionRoot::create(std::move(config), logger);
    if (!root) {
        return common::make_unexpected(root.error());
    }
    return std::unique_ptr<ClientApp>(std::move(root).value());
}

}  // namespace vocroz::client

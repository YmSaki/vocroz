// SPDX-License-Identifier: AGPL-3.0-only
//
// host_mode.h の実装。

#include "host_mode.h"

#include <string>
#include <utility>

#include <juce_events/juce_events.h>

#include "vocroz/ipc/factories.h"
#include "vocroz/webui/factories.h"

#include "app_constants.h"
#include "win/known_paths.h"
#include "win/win32_dialogs.h"

namespace vocroz::pa_desktop {
namespace {

/// ipc の名前付きパイプのクライアントでつなぐ EngineConnector。
class PipeEngineConnector final : public EngineConnector {
public:
    PipeEngineConnector(std::wstring pipe_name, common::Logger& logger)
        : pipe_name_(std::move(pipe_name)), logger_(logger) {}

    common::Result<std::unique_ptr<control_protocol::MessageChannel>, ConnectError> connect()
        override {
        auto channel = ipc::connect_pipe_client(pipe_name_, kEngineConnectWait, logger_);
        if (channel) {
            return std::move(channel).value();
        }
        switch (channel.error()) {
            case ipc::PipeError::kNotFound:
                return common::make_unexpected(ConnectError::kNotRunning);
            case ipc::PipeError::kAlreadyExists:
            case ipc::PipeError::kAccessDenied:
            case ipc::PipeError::kUntrustedServer:
            case ipc::PipeError::kSystemError:
                return common::make_unexpected(ConnectError::kRefused);
        }
        return common::make_unexpected(ConnectError::kRefused);
    }

private:
    std::wstring pipe_name_;
    common::Logger& logger_;
};

[[nodiscard]] std::string_view failure_log(AttachFailure failure) {
    switch (failure) {
        case AttachFailure::kRefused:
            return "エンジンのパイプを使えません（ほかのユーザーのものか、なりすまし）";
        case AttachFailure::kLaunchFailed:
            return "pa-engine を起動できませんでした";
        case AttachFailure::kEngineExited:
            return "pa-engine が接続の前に終了しました";
        case AttachFailure::kTimedOut:
            return "pa-engine に時間内に接続できませんでした";
    }
    return "pa-engine に接続できませんでした";
}

}  // namespace

/// 接続の手順を一定の間隔で進める JUCE のタイマー（コールバックはメッセージスレッド）。
class HostMode::AttachTimer final : public juce::Timer {
public:
    explicit AttachTimer(EngineAttacher& attacher) : attacher_(attacher) {
        startTimer(static_cast<int>(kAttachTickInterval.count()));
    }
    ~AttachTimer() override {
        stopTimer();
    }
    AttachTimer(const AttachTimer&) = delete;
    AttachTimer& operator=(const AttachTimer&) = delete;

    void timerCallback() override {
        attacher_.tick();
    }

private:
    EngineAttacher& attacher_;
};

common::Result<std::unique_ptr<HostMode>, HostModeError> HostMode::create(HostModeConfig config,
                                                                          common::Logger& logger) {
    auto pipe_name = ipc::engine_pipe_name(config.data_dir);
    if (!pipe_name) {
        logger.log(common::LogLevel::kError, "エンジンのパイプの名前を決められません");
        return common::make_unexpected(HostModeError::kPipeNameUnavailable);
    }
    const auto directory = win::executable_directory();
    if (!directory) {
        logger.log(common::LogLevel::kError, "pa-engine の場所を決められません");
        return common::make_unexpected(HostModeError::kEngineNotFound);
    }
    auto connector = std::make_unique<PipeEngineConnector>(std::move(pipe_name).value(), logger);
    auto launcher = std::make_unique<win::Win32EngineLauncher>(
        *directory / L"pa-engine.exe", config.data_dir,
        log_level_argument(config.options.log_level), logger);
    auto host = std::make_unique<HostMode>(ConstructionKey{}, std::move(config), logger,
                                           std::move(connector), std::move(launcher));
    host->open_window();
    return host;
}

HostMode::HostMode(ConstructionKey /*key*/, HostModeConfig config, common::Logger& logger,
                   std::unique_ptr<EngineConnector> connector,
                   std::unique_ptr<win::Win32EngineLauncher> launcher)
    : config_(std::move(config)),
      logger_(logger),
      connector_(std::move(connector)),
      launcher_(std::move(launcher)),
      attacher_(*connector_, *launcher_, clock_, *this),
      alive_(std::make_shared<HostMode*>(this)) {}

HostMode::~HostMode() {
    // 先に印を消し、メッセージスレッドに積まれた切断の処理が動かないようにする。
    alive_.reset();
    timer_.reset();
    detach();
    web_ui_.reset();
}

void HostMode::open_window() {
    webui::WebUiHostOptions ui_options;
    ui_options.window = {.title = "VOCROZ",
                         .width = kHostWindowWidth,
                         .height = kHostWindowHeight,
                         .min_width = kHostWindowMinWidth,
                         .min_height = kHostWindowMinHeight};
    ui_options.launch = {.mode = webui::UiMode::kHost, .lang = config_.options.lang, .join = {}};
    ui_options.assets = config_.ui_assets;
    ui_options.user_data_folder = config_.data_dir / kHostWebViewDataFolderName;
    ui_options.on_close_requested = [this] { on_close_requested(); };
    // 画面はエンジンの起動を待たずに開き、起動中であることが見えるようにする。
    web_ui_ = webui::make_juce_web_ui_host(std::move(ui_options));
    attacher_.begin();
    timer_ = std::make_unique<AttachTimer>(attacher_);
}

void HostMode::on_engine_attached(std::unique_ptr<control_protocol::MessageChannel> channel) {
    engine_ = std::move(channel);
    tap_ = std::make_unique<SessionActivityTap>(*engine_);
    relay_ = std::make_unique<control_protocol::ChannelRelay>(
        web_ui_->channel(), *tap_,
        [this](control_protocol::ChannelRelay::Side side) { on_relay_closed(side); });
    // つながる前に UI が送った hello は届いていないので、読み込み直して hello からやり直させる。
    // 再アタッチのときも、UI の表示を新しいスナップショットからやり直すために同じことをする。
    web_ui_->reload();
    logger_.log(common::LogLevel::kInfo, "エンジンに接続しました");
}

void HostMode::on_engine_attach_failed(AttachFailure failure) {
    logger_.log(common::LogLevel::kError, failure_log(failure));
    // 接続の手順（タイマーのコールバック）の中でダイアログを開かず、手順を抜けてから尋ねる。
    juce::MessageManager::callAsync([alive = std::weak_ptr<HostMode*>(alive_)] {
        if (const auto self = alive.lock()) {
            (*self)->ask_to_retry();
        }
    });
}

void HostMode::on_relay_closed(control_protocol::ChannelRelay::Side side) {
    // UI 側の経路は、画面を閉じるときにだけ閉じる。そのときは中継ごと破棄するので何もしない。
    if (side != control_protocol::ChannelRelay::Side::kEngine) {
        return;
    }
    // パイプの受信スレッドから呼ばれる。中継の破棄はこのスレッドでは行えない（自分の受信を
    // 止めることになる）ので、メッセージスレッドへ移す。
    juce::MessageManager::callAsync([alive = std::weak_ptr<HostMode*>(alive_)] {
        if (const auto self = alive.lock()) {
            (*self)->on_engine_lost();
        }
    });
}

void HostMode::on_engine_lost() {
    if (!relay_) {
        return;
    }
    logger_.log(common::LogLevel::kWarning, "エンジンとの接続が切れました。つなぎ直します");
    detach();
    attacher_.begin();
}

void HostMode::ask_to_retry() {
    if (win::ask_retry(engine_unavailable_text(config_.language))) {
        attacher_.begin();
        return;
    }
    if (config_.quit) {
        config_.quit();
    }
}

void HostMode::on_close_requested() {
    // 閉じてもエンジンは止まらないが、参加者がいるときは誤操作を防ぐため確かめる
    // （既定はキャンセル）。
    if (tap_ && tap_->session_active() && !win::confirm(host_close_text(config_.language))) {
        return;
    }
    if (config_.quit) {
        config_.quit();
    }
}

void HostMode::detach() noexcept {
    // 中継を先に止めてから、包みとパイプを破棄する。
    relay_.reset();
    tap_.reset();
    engine_.reset();
}

}  // namespace vocroz::pa_desktop

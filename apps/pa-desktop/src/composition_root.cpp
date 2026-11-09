// SPDX-License-Identifier: AGPL-3.0-only
//
// composition_root.h の実装。

#include "composition_root.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

#include "vocroz/client/client_app.h"
#include "vocroz/persistence/factories.h"
#include "vocroz/webui/ui_assets.h"

#include "app_config.h"
#include "app_constants.h"
#include "dialog_text.h"
#include "host_mode.h"
#include "win/known_paths.h"
#include "win/win32_dialogs.h"

namespace vocroz::pa_desktop {

// CMake の vocroz_embed_ui_assets()（apps/pa-desktop/CMakeLists.txt）が生成する関数。
webui::UiAssetTable embedded_ui_assets();

namespace {

// データフォルダは絶対パスにそろえる。pa-engine のパイプの名前はパスから作るので、
// 起動のたびに表記が違っても同じエンジンを指すように。
[[nodiscard]] std::optional<std::filesystem::path> resolve_data_dir(
    const std::optional<std::string>& option) {
    const auto base =
        option ? std::optional{std::filesystem::path{std::u8string(option->begin(), option->end())}}
               : win::default_data_dir();
    if (!base) {
        return std::nullopt;
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(*base, error);
    if (error) {
        return std::nullopt;
    }
    std::filesystem::create_directories(absolute, error);
    if (error) {
        return std::nullopt;
    }
    return absolute;
}

void quit_message_loop() {
    juce::MessageManager::getInstance()->stopDispatchLoop();
}

}  // namespace

CompositionRoot::CompositionRoot(LaunchOptions options) : options_(std::move(options)) {}

int CompositionRoot::run() {
    const DialogLanguage language =
        options_.lang ? dialog_language_from_tag(*options_.lang) : win::os_dialog_language();
    const auto data_dir = resolve_data_dir(options_.data_dir);
    if (!data_dir) {
        // ログの置き場所もないので、ダイアログだけで知らせる。
        win::show_error(startup_failed_text(language));
        return kExitFatal;
    }
    const bool client = options_.mode == Mode::kClient;
    auto logger = persistence::make_rotating_file_logger(
        persistence::LogOptions{.directory = persistence::log_directory(*data_dir),
                                .base_name = client ? kClientLogBaseName : kHostLogBaseName,
                                .min_level = options_.log_level});

    // JUCE の初期化はモードの組み立てより先に行い、組み立てたものを破棄した後に終える。
    const juce::ScopedJuceInitialiser_GUI juce_scope;
    std::unique_ptr<client::ClientApp> client_app;
    std::unique_ptr<HostMode> host_mode;
    if (client) {
        auto app = client::make_client_app(
            client::ClientAppConfig{
                .data_dir = *data_dir,
                .lang = options_.lang,
                .join = options_.join,
                .ui_assets = embedded_ui_assets(),
                .rendezvous_url = options_.rendezvous_url.value_or(kDefaultRendezvousUrl),
                .confirm_leave = [language] { return win::confirm(client_close_text(language)); },
                .quit = quit_message_loop},
            *logger);
        if (!app) {
            win::show_error(startup_failed_text(language));
            return kExitFatal;
        }
        client_app = std::move(app).value();
    } else {
        auto host = HostMode::create(HostModeConfig{.data_dir = *data_dir,
                                                    .options = options_,
                                                    .language = language,
                                                    .ui_assets = embedded_ui_assets(),
                                                    .quit = quit_message_loop},
                                     *logger);
        if (!host) {
            win::show_error(startup_failed_text(language));
            return kExitFatal;
        }
        host_mode = std::move(host).value();
    }

    juce::MessageManager::getInstance()->runDispatchLoop();

    // メッセージループを抜けたら、画面とキャプチャを JUCE の終了より先に片付ける。
    client_app.reset();
    host_mode.reset();
    logger->log(common::LogLevel::kInfo, "remote-pa を終了しました");
    return kExitOk;
}

}  // namespace vocroz::pa_desktop

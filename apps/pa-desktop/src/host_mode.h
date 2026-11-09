// SPDX-License-Identifier: AGPL-3.0-only
//
// 親機モードの組み立て（Windows で JUCE を使うビルドだけ）。
// webui の MessageChannel と ipc のクライアントを ChannelRelay でつなぎ、pa-engine へ中継する
// （AGENTS.md 5.4）。エンジンの起動と再アタッチは EngineAttacher の手順で行い、
// 接続が切れたら同じ手順でつなぎ直す（docs/adr/0030-remote-pa-assembly.md）。
//
// スレッド:
// - JUCE のメッセージスレッド: 生成、破棄、接続の手順、閉じるときの確認
// - パイプの受信スレッド: エンジンから UI
// への中継（ChannelRelay）。切断はメッセージスレッドへ移して扱う
// - WebView のブリッジ: UI からエンジンへの中継（メッセージスレッド）
#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/common/steady_clock.h"
#include "vocroz/control_protocol/channel_relay.h"
#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/webui/ui_assets.h"
#include "vocroz/webui/web_ui_host.h"

#include "dialog_text.h"
#include "engine_attacher.h"
#include "engine_connector.h"
#include "launch_options.h"
#include "session_activity_tap.h"
#include "win/win32_engine_launcher.h"

namespace vocroz::pa_desktop {

/// 親機モードの起動の設定。
struct HostModeConfig {
    std::filesystem::path data_dir;  ///< データフォルダ（絶対パスで、作成済み）
    LaunchOptions options;           ///< 起動オプション（lang と log_level を使う）
    DialogLanguage language = DialogLanguage::kEnglish;  ///< ダイアログの言語
    webui::UiAssetTable ui_assets;                       ///< 埋め込んだ UI
    std::function<void()> quit;  ///< アプリを終える（メッセージループを止める）
};

/// 起動できなかった原因。
enum class HostModeError {
    kPipeNameUnavailable,  ///< パイプの名前を決められない（ログイン中のユーザーを取得できない）
    kEngineNotFound,       ///< pa-engine の場所を決められない
};

/// 実行中の親機モード。
/// スレッド安全性: メッセージスレッドで作り、同じスレッドで破棄する。
class HostMode final : public EngineAttachListener {
public:
    /// 画面を開き、エンジンへの接続の手順を始める。
    /// @param config 起動の設定
    /// @param logger ログの出力先。戻り値より長く生存させる
    /// @return 実行中の親機モード。起動できなければその原因
    [[nodiscard]] static common::Result<std::unique_ptr<HostMode>, HostModeError> create(
        HostModeConfig config, common::Logger& logger);

    /// create() だけが作れるようにする鍵（std::make_unique から使うため公開する）。
    class ConstructionKey {
        friend class HostMode;
        ConstructionKey() = default;
    };

    HostMode(ConstructionKey key, HostModeConfig config, common::Logger& logger,
             std::unique_ptr<EngineConnector> connector,
             std::unique_ptr<win::Win32EngineLauncher> launcher);
    ~HostMode() override;

    HostMode(const HostMode&) = delete;
    HostMode& operator=(const HostMode&) = delete;

    // --- EngineAttachListener ---
    void on_engine_attached(std::unique_ptr<control_protocol::MessageChannel> channel) override;
    void on_engine_attach_failed(AttachFailure failure) override;

private:
    class AttachTimer;

    void open_window();
    void on_relay_closed(control_protocol::ChannelRelay::Side side);
    void on_engine_lost();
    void ask_to_retry();
    void on_close_requested();
    void detach() noexcept;

    HostModeConfig config_;
    common::Logger& logger_;
    common::SteadyClock clock_;
    std::unique_ptr<EngineConnector> connector_;
    std::unique_ptr<win::Win32EngineLauncher> launcher_;
    EngineAttacher attacher_;

    std::unique_ptr<webui::WebUiHost> web_ui_;
    // エンジンとの経路。tap_ が engine_ を包み、relay_ が web_ui_ と tap_ をつなぐ。
    std::unique_ptr<control_protocol::MessageChannel> engine_;
    std::unique_ptr<SessionActivityTap> tap_;
    std::unique_ptr<control_protocol::ChannelRelay> relay_;

    std::unique_ptr<AttachTimer> timer_;
    // パイプの受信スレッドからメッセージスレッドへ移した処理が、破棄の後に動かないための印。
    std::shared_ptr<HostMode*> alive_;
};

}  // namespace vocroz::pa_desktop

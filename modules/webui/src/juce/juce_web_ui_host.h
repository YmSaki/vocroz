// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の WebBrowserComponent（Windows では WebView2）で UI をホストするウィンドウ。
// ブリッジは JSON
// を中継するだけで、検証とコマンドの適用は行わない（docs/adr/0005-ui-juce-webview.md）。
#pragma once

#include <memory>
#include <string>

#include "vocroz/webui/web_ui_host.h"

#include "bridge_channel.h"

namespace vocroz::webui::juce_impl {

class UiWebView;
class UiWindow;

/// WebBrowserComponent による WebUiHost。
/// すべてのメンバー関数は、channel() の send() を除き JUCE のメッセージスレッドから呼ぶ。
class JuceWebUiHost final : public WebUiHost {
public:
    /// ウィンドウを作って表示し、埋め込んだ UI を読み込む。
    explicit JuceWebUiHost(WebUiHostOptions options);

    /// ブリッジを閉じてから、ウィンドウと WebView を破棄する。
    ~JuceWebUiHost() override;

    JuceWebUiHost(const JuceWebUiHost&) = delete;
    JuceWebUiHost& operator=(const JuceWebUiHost&) = delete;
    JuceWebUiHost(JuceWebUiHost&&) = delete;
    JuceWebUiHost& operator=(JuceWebUiHost&&) = delete;

    [[nodiscard]] control_protocol::MessageChannel& channel() override;
    void reload() override;

private:
    /// send() の呼び出し元のスレッドから、メッセージスレッドの WebView へメッセージを渡す。
    /// WebView の参照はメッセージスレッドだけで読み書きするので、破棄と送信の競合が起きない。
    class Sink final : public UiEventSink {
    public:
        /// WebView の参照。メッセージスレッドだけで読み書きする。
        struct Target {
            UiWebView* view = nullptr;
        };

        Sink();
        void post_to_ui(std::string message) override;

        /// 送り先の WebView を設定する。nullptr で、以降に届くメッセージを捨てる。
        /// メッセージスレッドから呼ぶ。
        void set_view(UiWebView* view);

    private:
        std::shared_ptr<Target> target_;
    };

    WebUiHostOptions options_;
    Sink sink_;
    BridgeChannel bridge_;
    std::unique_ptr<UiWebView> view_;
    std::unique_ptr<UiWindow> window_;
};

}  // namespace vocroz::webui::juce_impl

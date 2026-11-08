// SPDX-License-Identifier: AGPL-3.0-only
//
// JUCE の WebBrowserComponent による UI のホストの実装。

#include "juce/juce_web_ui_host.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "vocroz/webui/factories.h"
#include "vocroz/webui/ui_resource.h"

#include "webui_constants.h"

namespace vocroz::webui {
namespace juce_impl {
namespace {

juce::String to_juce_string(std::string_view text) {
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::Identifier to_identifier(std::string_view text) {
    return juce::Identifier{to_juce_string(text)};
}

juce::File to_juce_file(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return juce::File{juce::String::fromUTF8(reinterpret_cast<const char*>(utf8.data()),
                                             static_cast<int>(utf8.size()))};
}

// ui/src/ipc/launch-config.ts の UiMode と同じ文字列。
std::string_view mode_name(UiMode mode) {
    switch (mode) {
        case UiMode::kHost:
            return "host";
        case UiMode::kClient:
            return "client";
    }
    return "host";
}

}  // namespace

/// UI を表示する WebBrowserComponent。
/// ネイティブ連携を有効にしているので、埋め込んだ UI 以外のページには移動させない
/// （外部のページに window.__JUCE__ を触らせないため）。
class UiWebView final : public juce::WebBrowserComponent {
public:
    explicit UiWebView(const Options& options) : juce::WebBrowserComponent(options) {}

    /// ページの読み込みが終わっていれば、メッセージをイベントとして UI に送る。
    /// 読み込み前は JUCE がイベントを送れず、UI は読み込み後に hello を送ってスナップショットを
    /// 受け取るので、読み込み前のメッセージは捨てても表示は欠けない。
    void emit_if_loaded(const std::string& message) {
        if (loaded_) {
            emitEventIfBrowserIsVisible(to_identifier(kFromEngineEventId),
                                        juce::var{to_juce_string(message)});
        }
    }

    bool pageAboutToLoad(const juce::String& url) override {
        if (!url.startsWith(getResourceProviderRoot())) {
            return false;
        }
        loaded_ = false;
        return true;
    }

    void pageFinishedLoading(const juce::String& url) override {
        loaded_ = url.startsWith(getResourceProviderRoot());
    }

private:
    bool loaded_ = false;
};

/// UI のウィンドウ。閉じるボタンの扱いはコンポジションルートに任せる。
class UiWindow final : public juce::DocumentWindow {
public:
    UiWindow(const WebUiWindowOptions& options, std::function<void()> on_close_requested)
        : juce::DocumentWindow(to_juce_string(options.title),
                               juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                   juce::ResizableWindow::backgroundColourId),
                               juce::DocumentWindow::allButtons),
          on_close_requested_(std::move(on_close_requested)) {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        // 最大の大きさは制限しない（大きな画面での操作卓の表示を妨げないため）。
        constexpr int kUnlimited = std::numeric_limits<int>::max() / 2;
        setResizeLimits(options.min_width, options.min_height, kUnlimited, kUnlimited);
    }

    void closeButtonPressed() override {
        if (on_close_requested_) {
            on_close_requested_();
        }
    }

private:
    std::function<void()> on_close_requested_;
};

JuceWebUiHost::Sink::Sink() : target_(std::make_shared<Target>()) {}

void JuceWebUiHost::Sink::post_to_ui(std::string message) {
    // callAsync は呼び出した順にメッセージスレッドで実行するので、送った順に UI に届く。
    // target は shared_ptr でつかみ、ホストが先に破棄されても参照先が残るようにする。
    juce::MessageManager::callAsync([target = target_, message = std::move(message)] {
        if (target->view != nullptr) {
            target->view->emit_if_loaded(message);
        }
    });
}

void JuceWebUiHost::Sink::set_view(UiWebView* view) {
    target_->view = view;
}

JuceWebUiHost::JuceWebUiHost(WebUiHostOptions options)
    : options_(std::move(options)), bridge_(sink_) {
    const UiAssetTable assets = options_.assets;
    auto browser_options =
        juce::WebBrowserComponent::Options{}
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                                        .withUserDataFolder(to_juce_file(options_.user_data_folder))
                                        .withStatusBarDisabled())
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withEventListener(to_identifier(kToEngineEventId),
                               [this](const juce::var& payload) {
                                   // UI は JSON を文字列で送る。それ以外の形は UI
                                   // の不具合なので捨てる。
                                   if (payload.isString()) {
                                       bridge_.receive_from_ui(payload.toString().toStdString());
                                   }
                               })
            .withInitialisationData(to_juce_string(kInitModeKey),
                                    to_juce_string(mode_name(options_.launch.mode)))
            .withResourceProvider([assets](const juce::String& path)
                                      -> std::optional<juce::WebBrowserComponent::Resource> {
                const auto found = find_ui_resource(assets, path.toStdString());
                if (!found) {
                    return std::nullopt;
                }
                const auto* bytes = reinterpret_cast<const std::byte*>(found->data.data());
                return juce::WebBrowserComponent::Resource{
                    std::vector<std::byte>(bytes, bytes + found->data.size()),
                    to_juce_string(found->mime_type)};
            });
    if (options_.launch.lang) {
        browser_options = browser_options.withInitialisationData(
            to_juce_string(kInitLangKey), to_juce_string(*options_.launch.lang));
    }
    if (options_.launch.join) {
        browser_options = browser_options.withInitialisationData(
            to_juce_string(kInitJoinKey), to_juce_string(*options_.launch.join));
    }

    view_ = std::make_unique<UiWebView>(browser_options);
    sink_.set_view(view_.get());

    window_ = std::make_unique<UiWindow>(options_.window, options_.on_close_requested);
    window_->setContentNonOwned(view_.get(), false);
    window_->centreWithSize(options_.window.width, options_.window.height);
    window_->setVisible(true);
    view_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}

JuceWebUiHost::~JuceWebUiHost() {
    // 先にブリッジを閉じ、ほかのスレッドの send() が sink_ を呼ばないようにする。
    // まだ実行されていない送信の依頼は、view を外したので WebView に触れずに捨てられる。
    bridge_.close();
    sink_.set_view(nullptr);
    window_->clearContentComponent();
    window_.reset();
    view_.reset();
}

control_protocol::MessageChannel& JuceWebUiHost::channel() {
    return bridge_;
}

void JuceWebUiHost::reload() {
    view_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}

}  // namespace juce_impl

std::unique_ptr<WebUiHost> make_juce_web_ui_host(WebUiHostOptions options) {
    return std::make_unique<juce_impl::JuceWebUiHost>(std::move(options));
}

}  // namespace vocroz::webui

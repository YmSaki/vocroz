// SPDX-License-Identifier: AGPL-3.0-only
//
// UI のホストを生成するファクトリー関数。具象クラス（JUCE の実装）は公開しない（AGENTS.md 5.1）。
// JUCE を使うので、VOCROZ_WITH_JUCE が ON のときだけ定義される。
#pragma once

#include <memory>

#include "vocroz/webui/web_ui_host.h"

namespace vocroz::webui {

/// JUCE の WebBrowserComponent（Windows では WebView2）で UI
/// をホストするウィンドウを作り、表示する。
/// @param options ウィンドウの大きさ、起動時の設定、埋め込んだ ui/dist など
/// @return UI のホスト
/// スレッド安全性: JUCE のメッセージスレッドから、JUCEApplication の初期化の後に呼ぶ。
[[nodiscard]] std::unique_ptr<WebUiHost> make_juce_web_ui_host(WebUiHostOptions options);

}  // namespace vocroz::webui

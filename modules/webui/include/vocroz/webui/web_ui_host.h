// SPDX-License-Identifier: AGPL-3.0-only
//
// UI をホストするウィンドウ。親機モードと子機モードで共有し、モードごとの違い（ウィンドウの
// 大きさ、画面の選択）はコンポジションルートが WebUiHostOptions
// で渡す（docs/adr/0005-ui-juce-webview.md）。
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "vocroz/control_protocol/ports/message_channel.h"
#include "vocroz/webui/ui_assets.h"

namespace vocroz::webui {

/// 表示する画面のモード。起動ごとに固定し、起動中には切り替えない。
enum class UiMode {
    kHost,    ///< 親機（ミキサー画面）
    kClient,  ///< 子機
};

/// 窓に表示する画面。親機モードでは、ミキサーの窓とルーティングの窓を別のホストで開く
/// （docs/adr/0038-routing-window.md）。
enum class UiScreen {
    kMain,  ///< モードの主画面（親機はミキサー画面、子機は子機の画面）
    kRouting,  ///< 親機のルーティング画面
};

/// ウィンドウの大きさ。単位は論理ピクセル（OS の表示倍率を掛ける前の値）。
struct WebUiWindowOptions {
    std::string title;  ///< タイトルバーの文字列（UTF-8）
    int width = 0;
    int height = 0;
    int min_width = 0;
    int min_height = 0;
};

/// UI の起動時の設定。初期化データとして UI に渡す（ui/src/ipc/launch-config.ts）。
struct WebUiLaunchData {
    UiMode mode = UiMode::kHost;
    std::optional<std::string> lang;    ///< 表示言語の指定。なければ OS の表示言語
    std::optional<std::string> join;    ///< 子機モードで入力欄に入れておく招待
    UiScreen screen = UiScreen::kMain;  ///< 表示する画面
    /// コマンドの request_id の接頭辞。同じエンジンにつながる窓ごとに違う値を渡す。
    /// エンジンの応答はすべての窓に届くので、窓が自分の応答を見分けるのに使う。
    /// 空なら UI の既定の接頭辞を使う。
    std::string request_id_prefix;
};

/// UI のホストの設定。
struct WebUiHostOptions {
    WebUiWindowOptions window;
    WebUiLaunchData launch;
    UiAssetTable assets;  ///< 埋め込んだ ui/dist
    /// WebView2 のユーザーデータフォルダ。--data-dir の下を指定する。
    /// 既定の場所（実行ファイルの隣）はバージョン別フォルダの中で、更新のたびに失われるため。
    std::filesystem::path user_data_folder;
    /// ウィンドウの閉じるボタンが押されたときと、UI が窓を閉じるよう求めたときに、
    /// メッセージスレッドで呼ばれる関数。
    /// 終了の判断（本番中の確認など）はコンポジションルートが行う。
    std::function<void()> on_close_requested;
    /// UI がルーティング画面を開くよう求めたときに、メッセージスレッドで呼ばれる関数。
    /// 空なら求めを無視する（子機モードなど）。
    std::function<void()> on_open_routing;
};

/// UI をホストするウィンドウと、UI とのメッセージの経路。
/// 生成と破棄、reload() は JUCE のメッセージスレッドで行う。
/// channel() の MessageChannel
/// を使う中継（ChannelRelay）やリスナーは、このオブジェクトより先に破棄する。
class WebUiHost {
public:
    virtual ~WebUiHost() = default;

    /// UI とのメッセージの経路。ページの読み込み前と再読み込み中に送ったメッセージは捨てられる
    /// （UI は読み込み後に hello を送り、スナップショットから表示する）。
    /// UI からの受信は、メッセージスレッドでリスナーに渡す。
    [[nodiscard]] virtual control_protocol::MessageChannel& channel() = 0;

    /// ページを読み込み直す。UI は読み込み後に hello を送り、全状態を受け取り直す。
    /// pa-engine に再接続したときに使う。
    virtual void reload() = 0;

    /// ウィンドウを最小化から戻し、前面に出す。開いている窓をもう一度開こうとしたときに使う。
    virtual void bring_to_front() = 0;
};

}  // namespace vocroz::webui

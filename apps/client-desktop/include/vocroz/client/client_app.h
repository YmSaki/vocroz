// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードのアプリ。remote-pa のコンポジションルートが、子機モードで起動したときに
// make_client_app() で作る（docs/adr/0030-remote-pa-assembly.md）。
// データフォルダ、ログ、ダイアログ、メッセージループは remote-pa が親機モードと共有して持ち、
// ここには子機モードだけの組み立て（WebView、WASAPI、Winsock、WinHTTP と ClientRuntime）を置く。
// client-desktop は pa-desktop に依存できないので、remote-pa の持ち物は ClientAppConfig
// で受け取る。
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "vocroz/common/ports/logger.h"
#include "vocroz/common/result.h"
#include "vocroz/webui/ui_assets.h"

namespace vocroz::client {

/// 子機モードの起動の設定。
struct ClientAppConfig {
    /// データフォルダ（絶対パスで、作成済み）。設定と WebView のデータを置く
    std::filesystem::path data_dir;
    /// 画面の表示言語の指定。なければ OS の表示言語
    std::optional<std::string> lang;
    /// 起動時に入力欄に入れておく招待（招待 URL か招待コード）
    std::optional<std::string> join;
    /// 実行ファイルに埋め込んだ UI（ui/dist）。親機モードと同じものを使う
    webui::UiAssetTable ui_assets;
    /// ランデブーサーバーのベース URL。空なら参加できない（招待の解決は UNREACHABLE になる）
    std::string rendezvous_url;
    /// 参加中に閉じようとしたときの確認。モーダルで尋ね、閉じてよければ true を返す
    std::function<bool()> confirm_leave;
    /// アプリを終える（メッセージループを止める）
    std::function<void()> quit;
};

/// 子機モードの起動の失敗。
enum class ClientAppError {
    kCryptoUnavailable,  ///< 暗号ライブラリを初期化できない（鍵と接続 ID を作れない）
    kEncoderFailed,  ///< Opus のエンコーダーを作れない
    kSocketFailed,   ///< UDP のソケットを開けない
};

/// 実行中の子機モード。破棄すると、参加中なら切断し、キャプチャと送信を止めて画面を閉じる。
class ClientApp {
public:
    virtual ~ClientApp() = default;
};

/// 子機モードを組み立てて画面を開く。Windows で JUCE を使うビルドにだけある。
/// @param config 起動の設定
/// @param logger ログの出力先。戻り値より長く生存させる
/// @return 実行中の子機モード。起動できなければその原因
/// スレッド安全性: JUCE のメッセージスレッドから呼ぶ。戻り値もメッセージスレッドで破棄する。
[[nodiscard]] common::Result<std::unique_ptr<ClientApp>, ClientAppError> make_client_app(
    ClientAppConfig config, common::Logger& logger);

}  // namespace vocroz::client

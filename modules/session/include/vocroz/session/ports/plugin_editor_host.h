// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのエディター（プラグイン独自の画面）を、独立したネイティブウィンドウで開くポート。
// plugin-host が JUCE で実装する。ウィンドウを閉じてもプラグインの処理は続く
// （docs/adr/0041-plugin-editor-and-failure-notice.md）。
#pragma once

#include <string_view>

#include "vocroz/common/result.h"
#include "vocroz/session/ports/plugin_loader.h"

namespace vocroz::session {

/// エディターを開けなかった理由。
enum class PluginEditorError {
    kNotSupported,  ///< このホストでは開けない種類のインスタンス（別の実装で読み込んだもの）
    kOpenFailed,    ///< プラグインがエディターを作る途中で例外を送出した
};

/// プラグインのエディターのウィンドウ。インスタンスごとに 1 つまで開く。
/// ウィンドウを開いても閉じても、プラグインの処理（グラフのノード）には触れない。
/// インスタンスを破棄する前に close() を呼ぶ。エディターはインスタンスを参照するため。
/// スレッド安全性: すべて JUCE のメッセージスレッドから呼ぶ。破棄もそのスレッドで行う。
class PluginEditorHost {
public:
    virtual ~PluginEditorHost() = default;

    /// エディターのウィンドウを開く。開いていれば、そのウィンドウを前面に出す。
    /// プラグインが独自の画面を持たなければ、パラメーターの一覧の画面で代える。
    /// @param instance 読み込んだプラグイン（このホストを使う PluginLoader で読み込んだもの）
    /// @param title ウィンドウのタイトル（UTF-8）
    /// @return 開けなかったら理由。プラグインの処理は続く
    [[nodiscard]] virtual common::Result<void, PluginEditorError> open(PluginInstance& instance,
                                                                       std::string_view title) = 0;

    /// エディターのウィンドウを閉じる。開いていなければ何もしない。
    /// @param instance 読み込んだプラグイン
    virtual void close(const PluginInstance& instance) = 0;

    /// @param instance 読み込んだプラグイン
    /// @return エディターのウィンドウが開いていれば true。オペレーターが閉じたら false になる
    [[nodiscard]] virtual bool is_open(const PluginInstance& instance) const = 0;

    /// 開いているエディターのウィンドウをすべて閉じる。JUCE を終える前に呼ぶ。
    virtual void close_all() = 0;
};

}  // namespace vocroz::session

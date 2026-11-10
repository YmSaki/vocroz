// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのスキャン専用のスレッド（docs/adr/0042-plugin-insert.md）。
// スキャンはファイルごとにヘルパーを起動し、1 つに最大 30 秒かかるので、コントロールスレッドでも
// JUCE のメッセージスレッドでもなく、このスレッドで回す。PluginCatalog はこのスレッドだけが使う。
// スキャンが済むたびに、使えるプラグインの一覧を publish で返す。受け取る側がコントロール
// スレッドへ移して HostSession::set_available_plugins() に渡す。
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "vocroz/common/ports/logger.h"
#include "vocroz/session/plugin_catalog.h"
#include "vocroz/session/plugin_description.h"

namespace vocroz::engine {

/// スキャン専用のスレッドと、それが持つプラグインの一覧。
/// スレッド安全性: load_saved()、start()、request_rescan()、stop() はコントロールスレッドから呼ぶ。
/// publish はスキャンのスレッドから呼ばれる。
class PluginScanWorker {
public:
    /// 使えるプラグインの一覧を返す処理。スキャンのスレッドから呼ぶ。
    using Publish = std::function<void(std::vector<session::PluginDescription>)>;

    /// @param scanner ファイルの列挙とスキャン。このオブジェクトより長く生存させる
    /// @param store   スキャンの記録の保存先。このオブジェクトより長く生存させる
    /// @param publish スキャンが済んだら、使えるプラグインの一覧を渡す
    /// @param logger  このオブジェクトより長く生存させる
    PluginScanWorker(session::PluginScanner& scanner, session::PluginCatalogStore& store,
                     Publish publish, common::Logger& logger);
    /// 動いていれば stop() する。
    ~PluginScanWorker();
    PluginScanWorker(const PluginScanWorker&) = delete;
    PluginScanWorker& operator=(const PluginScanWorker&) = delete;

    /// 保存したスキャンの記録を読み、使えるプラグインの一覧を返す。呼んだスレッドで読む。
    /// 記録を読むだけなのですぐに済む。保存した挿入を起動時に戻すため、HostSession::start()
    /// の前に呼び、start() より前に 1 回だけ呼ぶ。
    /// @return 使えるプラグイン。記録がなければ空
    [[nodiscard]] std::vector<session::PluginDescription> load_saved();

    /// スレッドを立て、最初のスキャンを始める（変更のないファイルはスキャンしない）。
    void start();

    /// スキャンし直す。スキャン中なら、今のスキャンが済んでからもう一度スキャンする。
    void request_rescan();

    /// スキャンを止めてスレッドを join する。ファイル 1 つのスキャンの途中なら、それが済むまで
    /// 待つ（ヘルパーの制限時間で上限がある）。動いていなければ何もしない。
    void stop();

private:
    void run();
    // スキャンを最後まで（止める要求があればそこまで）進める。
    void scan_all();

    session::PluginCatalog catalog_;
    Publish publish_;
    common::Logger& logger_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool rescan_requested_ = false;
    bool stop_requested_ = false;
    std::thread thread_;
};

}  // namespace vocroz::engine

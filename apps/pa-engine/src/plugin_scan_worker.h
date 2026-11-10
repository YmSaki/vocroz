// SPDX-License-Identifier: AGPL-3.0-only
//
// プラグインのスキャン専用のスレッド（docs/adr/0042-plugin-insert.md）。
// スキャンはファイルごとにヘルパーを起動し、1 つに最大 30 秒かかるので、コントロールスレッドでも
// JUCE のメッセージスレッドでもなく、このスレッドで回す。PluginCatalog はこのスレッドだけが使う。
// スキャンを始めたときと済んだときに Listener で知らせる。受け取る側がコントロールスレッドへ移して
// HostSession::set_plugin_scanning() と set_available_plugins() に渡す。
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "vocroz/common/ports/logger.h"
#include "vocroz/session/plugin_catalog.h"
#include "vocroz/session/plugin_description.h"
#include "vocroz/session/plugin_scan_record.h"
#include "vocroz/session/ports/plugin_scan_control.h"

namespace vocroz::engine {

/// スキャン専用のスレッドと、それが持つプラグインの一覧。HostSession にはスキャンし直しの
/// 依頼先（PluginScanControl）として渡す。
/// スレッド安全性: load_saved()、start()、request_rescan()、stop() はコントロールスレッドから呼ぶ。
/// Listener はスキャンのスレッドから呼ばれる。
class PluginScanWorker final : public session::PluginScanControl {
public:
    /// 使えるプラグインの一覧と、読み込み不可のファイルの記録。
    struct Catalog {
        std::vector<session::PluginDescription> plugins;
        /// status が kScanned でない記録。
        std::vector<session::PluginScanRecord> blocked;
    };

    /// スキャンの始まりと終わりの通知先。どちらもスキャンのスレッドから呼ぶ。
    struct Listener {
        /// スキャンを始めた。
        std::function<void()> started;
        /// スキャンが済んだ（中断した場合も、そこまでの一覧で呼ぶ）。
        std::function<void(Catalog)> finished;
    };

    /// @param scanner  ファイルの列挙とスキャン。このオブジェクトより長く生存させる
    /// @param store    スキャンの記録の保存先。このオブジェクトより長く生存させる
    /// @param listener スキャンの始まりと終わりの通知先
    /// @param logger   このオブジェクトより長く生存させる
    PluginScanWorker(session::PluginScanner& scanner, session::PluginCatalogStore& store,
                     Listener listener, common::Logger& logger);
    /// 動いていれば stop() する。
    ~PluginScanWorker() override;
    PluginScanWorker(const PluginScanWorker&) = delete;
    PluginScanWorker& operator=(const PluginScanWorker&) = delete;

    /// 保存したスキャンの記録を読み、使えるプラグインの一覧を返す。呼んだスレッドで読む。
    /// 記録を読むだけなのですぐに済む。保存した挿入を起動時に戻すため、HostSession::start()
    /// の前に呼び、start() より前に 1 回だけ呼ぶ。
    /// @return 使えるプラグインと読み込み不可のファイル。記録がなければ空
    [[nodiscard]] Catalog load_saved();

    /// スレッドを立て、最初のスキャンを始める（変更のないファイルはスキャンしない）。
    void start();

    /// スキャンし直す。スキャン中なら、今のスキャンが済んでからもう一度スキャンする。
    void request_rescan() override;

    /// スキャンを止めてスレッドを join する。ファイル 1 つのスキャンの途中なら、それが済むまで
    /// 待つ（ヘルパーの制限時間で上限がある）。動いていなければ何もしない。
    void stop();

private:
    void run();
    // スキャンを最後まで（止める要求があればそこまで）進める。
    void scan_all();
    // 今の記録から一覧を作る。
    [[nodiscard]] Catalog catalog() const;

    session::PluginCatalog catalog_;
    Listener listener_;
    common::Logger& logger_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool rescan_requested_ = false;
    bool stop_requested_ = false;
    std::thread thread_;
};

}  // namespace vocroz::engine

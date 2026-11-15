// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin_scan_worker.h の実装。

#include "plugin_scan_worker.h"

#include <utility>

namespace vocroz::engine {

PluginScanWorker::PluginScanWorker(session::PluginScanner& scanner,
                                   session::PluginCatalogStore& store, Listener listener,
                                   common::Logger& logger)
    : catalog_(scanner, store), listener_(std::move(listener)), logger_(logger) {}

PluginScanWorker::~PluginScanWorker() {
    stop();
}

PluginScanWorker::Catalog PluginScanWorker::load_saved() {
    catalog_.load();
    return catalog();
}

PluginScanWorker::Catalog PluginScanWorker::catalog() const {
    Catalog result;
    result.plugins = catalog_.available_plugins();
    for (const auto& record : catalog_.records()) {
        if (record.status != session::PluginScanStatus::kScanned) {
            result.blocked.push_back(record);
        }
    }
    return result;
}

void PluginScanWorker::start() {
    if (thread_.joinable()) {
        return;
    }
    {
        const std::lock_guard lock{mutex_};
        rescan_requested_ = true;
        stop_requested_ = false;
    }
    thread_ = std::thread{[this] { run(); }};
}

void PluginScanWorker::request_rescan() {
    {
        const std::lock_guard lock{mutex_};
        rescan_requested_ = true;
    }
    wake_.notify_one();
}

void PluginScanWorker::stop() {
    if (!thread_.joinable()) {
        return;
    }
    {
        const std::lock_guard lock{mutex_};
        stop_requested_ = true;
    }
    wake_.notify_one();
    thread_.join();
}

void PluginScanWorker::run() {
    while (true) {
        {
            std::unique_lock lock{mutex_};
            wake_.wait(lock, [this] { return rescan_requested_ || stop_requested_; });
            if (stop_requested_) {
                return;
            }
            rescan_requested_ = false;
        }
        scan_all();
    }
}

void PluginScanWorker::scan_all() {
    listener_.started();
    catalog_.begin_rescan();
    auto progress = session::PluginScanProgress::kMore;
    while (catalog_.scanning()) {
        {
            const std::lock_guard lock{mutex_};
            if (stop_requested_) {
                // 途中までの記録は保存しない。次の起動で残りをスキャンし直す。
                return;
            }
        }
        progress = catalog_.scan_next();
        if (progress == session::PluginScanProgress::kScannerUnavailable) {
            logger_.log(common::LogLevel::kError,
                        "スキャン用ヘルパーを起動できないため、プラグインのスキャンを中断しました");
            break;
        }
    }
    listener_.finished(catalog());
}

}  // namespace vocroz::engine

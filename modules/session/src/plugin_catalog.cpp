// SPDX-License-Identifier: AGPL-3.0-only
//
// PluginCatalog（plugin_catalog.h）の実装。

#include "vocroz/session/plugin_catalog.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace vocroz::session {
namespace {

bool path_less(const PluginScanRecord& a, const PluginScanRecord& b) {
    return a.file.path < b.file.path;
}

PluginScanStatus status_of(PluginScanFailure failure) {
    switch (failure) {
        case PluginScanFailure::kCrashed:
            return PluginScanStatus::kCrashed;
        case PluginScanFailure::kTimedOut:
            return PluginScanStatus::kTimedOut;
        case PluginScanFailure::kInvalidOutput:
            return PluginScanStatus::kInvalidOutput;
        case PluginScanFailure::kScannerUnavailable:
            // 呼び出し側で先に扱い、記録に残さない。
            break;
    }
    return PluginScanStatus::kInvalidOutput;
}

}  // namespace

PluginCatalog::PluginCatalog(PluginScanner& scanner, PluginCatalogStore& store)
    : scanner_(scanner), store_(store) {}

void PluginCatalog::load() {
    records_.clear();
    pending_.clear();
    auto loaded = store_.load();
    if (!loaded) {
        return;
    }
    records_ = std::move(loaded.value());
    std::sort(records_.begin(), records_.end(), path_less);
    // 同じパスの記録が重なっていたら、壊れた記録とみなして後のものを捨てる。
    // 残すとファイル 1 つのプラグインが一覧に 2 回出るため。
    const auto same_path = [](const PluginScanRecord& a, const PluginScanRecord& b) {
        return a.file.path == b.file.path;
    };
    records_.erase(std::unique(records_.begin(), records_.end(), same_path), records_.end());
}

std::size_t PluginCatalog::begin_rescan() {
    std::vector<PluginFile> files = scanner_.list_plugin_files();
    std::sort(files.begin(), files.end(),
              [](const PluginFile& a, const PluginFile& b) { return a.path < b.path; });
    files.erase(
        std::unique(files.begin(), files.end(),
                    [](const PluginFile& a, const PluginFile& b) { return a.path == b.path; }),
        files.end());

    std::vector<PluginScanRecord> kept;
    pending_.clear();
    auto record = records_.begin();
    for (PluginFile& file : files) {
        record = std::find_if(record, records_.end(), [&file](const PluginScanRecord& r) {
            return r.file.path >= file.path;
        });
        if (record != records_.end() && record->file == file) {
            kept.push_back(std::move(*record));
        } else {
            pending_.push_back(std::move(file));
        }
    }
    // 変更されたファイルの古い記録は、スキャンが終わるまで残さない。
    // 古い記録のプラグインを読み込むと、記述と中身が食い違うため。
    records_ = std::move(kept);
    if (pending_.empty()) {
        // スキャンするものがなくても、なくなったファイルの記録を消した結果は保存する。
        static_cast<void>(store_.save(records_));
    }
    return pending_.size();
}

PluginScanProgress PluginCatalog::scan_next() {
    if (pending_.empty()) {
        return PluginScanProgress::kDone;
    }
    const PluginFile& file = pending_.front();
    auto result = scanner_.scan(file);
    if (!result && result.error() == PluginScanFailure::kScannerUnavailable) {
        pending_.clear();
        return PluginScanProgress::kScannerUnavailable;
    }
    PluginScanRecord record{file, PluginScanStatus::kScanned, {}};
    if (result) {
        record.plugins = std::move(result.value());
        // ヘルパーの出力のパスではなく、列挙したパスに合わせる。
        // 記録と一覧を同じパスで引けるようにするため。
        for (PluginDescription& plugin : record.plugins) {
            plugin.file_path = file.path;
        }
    } else {
        record.status = status_of(result.error());
    }
    put_record(std::move(record));
    pending_.pop_front();
    if (!pending_.empty()) {
        return PluginScanProgress::kMore;
    }
    // 保存に失敗しても一覧はメモリにあるので続行する。次の起動で再スキャンになるだけで済む。
    static_cast<void>(store_.save(records_));
    return PluginScanProgress::kDone;
}

bool PluginCatalog::scanning() const noexcept {
    return !pending_.empty();
}

const std::vector<PluginScanRecord>& PluginCatalog::records() const noexcept {
    return records_;
}

std::vector<PluginDescription> PluginCatalog::available_plugins() const {
    std::vector<PluginDescription> plugins;
    for (const PluginScanRecord& record : records_) {
        if (record.status == PluginScanStatus::kScanned) {
            std::copy(record.plugins.begin(), record.plugins.end(), std::back_inserter(plugins));
        }
    }
    return plugins;
}

void PluginCatalog::put_record(PluginScanRecord record) {
    const auto position = std::lower_bound(records_.begin(), records_.end(), record, path_less);
    records_.insert(position, std::move(record));
}

}  // namespace vocroz::session

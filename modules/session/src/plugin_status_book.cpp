// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin_status_book.h の実装。

#include "plugin_status_book.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "vocroz/control_protocol/constants.h"

#include "session_constants.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;

bool fits_text(std::string_view text) noexcept {
    return text.size() <= cp::kMaxTextBytes;
}

std::optional<cp::BlockedPluginReason> reason_of(PluginScanStatus status) noexcept {
    switch (status) {
        case PluginScanStatus::kScanned:
            return std::nullopt;
        case PluginScanStatus::kCrashed:
            return cp::BlockedPluginReason::kCrashed;
        case PluginScanStatus::kTimedOut:
            return cp::BlockedPluginReason::kTimedOut;
        case PluginScanStatus::kInvalidOutput:
            return cp::BlockedPluginReason::kInvalidOutput;
    }
    return std::nullopt;
}

// 長すぎるパスはファイル名だけにする。どのプラグインかはファイル名で分かるため。
// Windows のパスも Linux のテストのパスも扱えるよう、区切りは両方を見る。
std::optional<std::string> displayable_path(const std::string& path) {
    if (fits_text(path)) {
        return path;
    }
    const auto slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.empty() || !fits_text(name)) {
        return std::nullopt;
    }
    return name;
}

cp::PluginInsertState state_of(InsertLoadState state) noexcept {
    switch (state) {
        case InsertLoadState::kLoading:
            return cp::PluginInsertState::kLoading;
        case InsertLoadState::kReady:
            return cp::PluginInsertState::kReady;
        case InsertLoadState::kFailed:
            return cp::PluginInsertState::kFailed;
    }
    return cp::PluginInsertState::kFailed;
}

cp::PluginInsertFailure failure_of(InsertFailure failure) noexcept {
    switch (failure) {
        case InsertFailure::kNotFound:
            return cp::PluginInsertFailure::kNotFound;
        case InsertFailure::kInstantiateFailed:
            return cp::PluginInsertFailure::kInstantiateFailed;
        case InsertFailure::kUnsupportedLayout:
            return cp::PluginInsertFailure::kUnsupportedLayout;
        case InsertFailure::kProcessingFailed:
            return cp::PluginInsertFailure::kProcessingFailed;
    }
    return cp::PluginInsertFailure::kProcessingFailed;
}

}  // namespace

PluginStatusBook::Dropped PluginStatusBook::set_catalog(std::span<const PluginDescription> plugins,
                                                        std::span<const PluginScanRecord> blocked) {
    Dropped dropped;
    catalog_.clear();
    catalog_.reserve(plugins.size());
    for (const PluginDescription& plugin : plugins) {
        if (plugin.identifier.empty() || plugin.identifier.size() > cp::kMaxPluginIdBytes ||
            !fits_text(plugin.name) || !fits_text(plugin.vendor) || !fits_text(plugin.version)) {
            ++dropped.plugins;
            continue;
        }
        catalog_.push_back(cp::PluginEntry{plugin.identifier, plugin.name, plugin.vendor,
                                           plugin.version, plugin.is_instrument});
    }
    blocked_.clear();
    for (const PluginScanRecord& record : blocked) {
        const auto reason = reason_of(record.status);
        if (!reason) {
            continue;
        }
        auto path = displayable_path(record.file.path);
        if (!path || blocked_.size() >= cp::kMaxBlockedPluginFiles) {
            ++dropped.blocked;
            continue;
        }
        blocked_.push_back(cp::BlockedPluginFile{std::move(*path), *reason});
    }
    ++generation_;
    return dropped;
}

bool PluginStatusBook::set_scanning(bool scanning) noexcept {
    if (scanning_ == scanning) {
        return false;
    }
    scanning_ = scanning;
    return true;
}

cp::PluginsInfo PluginStatusBook::info(std::span<const cp::RoutingInsert> inserts,
                                       const PluginInsertBook& book) const {
    cp::PluginsInfo info;
    info.entity_id = kPluginsEntityId;
    info.revision = kPluginsRevision;
    info.scanning = scanning_;
    info.catalog_generation = generation_;
    info.catalog_size = catalog_.size();
    info.blocked = blocked_;
    info.inserts.reserve(inserts.size());
    for (const cp::RoutingInsert& insert : inserts) {
        const InsertSlot* slot = book.find(insert.entity_id);
        if (slot == nullptr) {
            continue;
        }
        cp::PluginInsertStatus status;
        status.entity_id = insert.entity_id;
        status.state = state_of(slot->state);
        if (slot->state == InsertLoadState::kFailed && slot->failure) {
            status.failure = failure_of(*slot->failure);
        }
        info.inserts.push_back(std::move(status));
    }
    return info;
}

std::vector<cp::PluginCatalogPage> PluginStatusBook::pages() const {
    std::vector<cp::PluginCatalogPage> pages;
    for (std::size_t offset = 0; offset < catalog_.size();
         offset += cp::kMaxPluginCatalogPageEntries) {
        const auto end = std::min(catalog_.size(), offset + cp::kMaxPluginCatalogPageEntries);
        cp::PluginCatalogPage page;
        page.generation = generation_;
        page.offset = offset;
        page.plugins.assign(catalog_.begin() + static_cast<std::ptrdiff_t>(offset),
                            catalog_.begin() + static_cast<std::ptrdiff_t>(end));
        pages.push_back(std::move(page));
    }
    return pages;
}

}  // namespace vocroz::session

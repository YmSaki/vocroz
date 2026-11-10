// SPDX-License-Identifier: AGPL-3.0-only
//
// 挿入ごとのプラグインの読み込みの状態の実装。

#include "plugin_insert_book.h"

#include <algorithm>
#include <utility>

namespace vocroz::session {
namespace {

InsertFailure failure_of(PluginLoadError error) noexcept {
    switch (error) {
        case PluginLoadError::kNotFound:
            return InsertFailure::kNotFound;
        case PluginLoadError::kInstantiateFailed:
            return InsertFailure::kInstantiateFailed;
        case PluginLoadError::kUnsupportedLayout:
            return InsertFailure::kUnsupportedLayout;
    }
    return InsertFailure::kInstantiateFailed;
}

}  // namespace

PluginInsertBook::PluginInsertBook(PluginRack& rack) : rack_(rack) {}

void PluginInsertBook::reconcile(std::span<const control_protocol::RoutingInsert> inserts,
                                 std::span<const PluginDescription> plugins) {
    // なくなった挿入と、プラグインを替えた挿入を先に手放す。同じ entity_id で読み込み直す前に
    // 破棄を依頼しておく（PluginRack の契約）。
    for (auto it = slots_.begin(); it != slots_.end();) {
        const auto kept = std::find_if(inserts.begin(), inserts.end(), [&](const auto& insert) {
            return insert.entity_id == it->first && insert.plugin_id == it->second.plugin_id;
        });
        if (kept == inserts.end()) {
            rack_.release(it->first);
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& insert : inserts) {
        if (slots_.contains(insert.entity_id)) {
            continue;
        }
        InsertSlot slot;
        slot.plugin_id = insert.plugin_id;
        const auto description = std::find_if(
            plugins.begin(), plugins.end(),
            [&](const PluginDescription& d) { return d.identifier == insert.plugin_id; });
        if (description == plugins.end()) {
            // 検証は一覧にあるプラグインだけを通すが、適用の後で一覧が変わることがある。
            slot.state = InsertLoadState::kFailed;
            slot.failure = InsertFailure::kNotFound;
        } else {
            slot.ticket = next_ticket_++;
            rack_.load(insert.entity_id, slot.ticket, *description);
        }
        slots_.emplace(insert.entity_id, std::move(slot));
    }
}

bool PluginInsertBook::on_loaded(const std::string& insert_id, PluginLoadTicket ticket,
                                 PluginLoadResult result) {
    const auto it = slots_.find(insert_id);
    if (it == slots_.end() || it->second.ticket != ticket ||
        it->second.state != InsertLoadState::kLoading) {
        return false;
    }
    auto& slot = it->second;
    if (!result || !result.value()) {
        slot.state = InsertLoadState::kFailed;
        slot.failure = result ? InsertFailure::kInstantiateFailed : failure_of(result.error());
        return true;
    }
    slot.state = InsertLoadState::kReady;
    slot.processor = std::move(result).value();
    slot.fault = std::make_shared<audio_core::PluginFault>();
    return true;
}

std::vector<std::string> PluginInsertBook::collect_faults() {
    std::vector<std::string> failed;
    for (auto& [id, slot] : slots_) {
        if (slot.state == InsertLoadState::kReady && slot.fault->raised()) {
            slot.state = InsertLoadState::kFailed;
            slot.failure = InsertFailure::kProcessingFailed;
            failed.push_back(id);
        }
    }
    return failed;
}

std::vector<InsertedPlugin> PluginInsertBook::ready_for(
    std::span<const control_protocol::RoutingInsert> inserts) const {
    std::vector<InsertedPlugin> ready;
    for (const auto& insert : inserts) {
        const auto it = slots_.find(insert.entity_id);
        if (it != slots_.end() && it->second.plugin_id == insert.plugin_id &&
            it->second.state == InsertLoadState::kReady) {
            ready.push_back(
                InsertedPlugin{insert.entity_id, it->second.processor, it->second.fault});
        }
    }
    return ready;
}

const InsertSlot* PluginInsertBook::find(const std::string& insert_id) const {
    const auto it = slots_.find(insert_id);
    return it == slots_.end() ? nullptr : &it->second;
}

}  // namespace vocroz::session

// SPDX-License-Identifier: AGPL-3.0-only
//
// ルーティングの状態と遷移の規則の実装。

#include "routing_book.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "vocroz/session/entity_ids.h"

#include "detail/revision_utils.h"
#include "routing_validator.h"
#include "session_constants.h"

namespace vocroz::session {
namespace {

namespace cp = control_protocol;
using cp::ErrorCode;

/// 構成のバスとセンドのレベルを、バス、センドの順に並べて作る。
/// 前の構成にあった entity_id
/// のレベルは引き継ぎ、本番中にフェーダーやミュートが戻らないようにする。
std::vector<cp::RouteLevel> levels_for(const cp::RoutingConfig& config,
                                       const std::vector<cp::RouteLevel>& previous) {
    std::vector<cp::RouteLevel> levels;
    levels.reserve(config.buses.size() + config.sends.size());
    const auto carry = [&](const std::string& entity_id) {
        const auto it = std::find_if(previous.begin(), previous.end(), [&](const auto& level) {
            return level.entity_id == entity_id;
        });
        if (it != previous.end()) {
            levels.push_back(*it);
        } else {
            // 初期ミュート: 新しいバスとセンドはミュートで作る。プリフェーダーのセンドも
            // 送り元のフェーダーに関わらず無音で始まる。
            levels.push_back(cp::RouteLevel{entity_id, 1, 0.0, true});
        }
    };
    for (const auto& bus : config.buses) {
        carry(bus.entity_id);
    }
    for (const auto& send : config.sends) {
        carry(send.entity_id);
    }
    return levels;
}

}  // namespace

RoutingBook::RoutingBook() {
    info_.entity_id = kRoutingEntityId;
    info_.revision = 1;
}

void RoutingBook::stage(cp::RoutingConfig config, std::span<const std::string> strip_ids) {
    auto check = check_routing(config, strip_ids, plugins_);
    info_.staged = cp::StagedRouting{std::move(config), std::move(check)};
    detail::bump_revision(info_.revision);
}

common::Result<void, ErrorCode> RoutingBook::discard() {
    if (!info_.staged) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    info_.staged.reset();
    detail::bump_revision(info_.revision);
    return {};
}

common::Result<void, ErrorCode> RoutingBook::prepare_commit(
    bool approve_feedback, std::span<const std::string> strip_ids) {
    if (!info_.staged) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    // ステージングのあとでストリップが削除されていることがあるので、適用の直前に検証し直す。
    info_.staged->check = check_routing(info_.staged->config, strip_ids, plugins_);
    const auto& check = info_.staged->check;
    if (check.issue || (check.needs_approval && !approve_feedback)) {
        return common::make_unexpected(ErrorCode::kInvalidState);
    }
    return {};
}

void RoutingBook::commit() {
    auto staged = std::move(*info_.staged);
    info_.staged.reset();
    info_.levels = levels_for(staged.config, info_.levels);
    info_.feedback_approved = staged.check.needs_approval;
    info_.applied = std::move(staged.config);
    detail::bump_revision(info_.revision);
}

void RoutingBook::set_plugins(std::vector<PluginDescription> plugins,
                              std::span<const std::string> strip_ids) {
    plugins_ = std::move(plugins);
    if (info_.staged) {
        // 検証の結果は観測値なので、変わっても revision は進めない。
        info_.staged->check = check_routing(info_.staged->config, strip_ids, plugins_);
    }
}

cp::RouteLevel* RoutingBook::find_level(const std::string& entity_id) noexcept {
    const auto it = std::find_if(info_.levels.begin(), info_.levels.end(),
                                 [&](const auto& level) { return level.entity_id == entity_id; });
    return it == info_.levels.end() ? nullptr : &*it;
}

bool RoutingBook::forget_strip(const std::string& strip_id,
                               std::span<const std::string> strip_ids) {
    auto& applied = info_.applied;
    const auto sends_before = applied.sends.size();
    const auto delays_before = applied.input_delays.size();
    const auto inserts_before = applied.inserts.size();
    std::erase_if(applied.sends,
                  [&](const auto& send) { return send.from == strip_id || send.to == strip_id; });
    std::erase_if(applied.input_delays,
                  [&](const auto& delay) { return delay.strip_id == strip_id; });
    std::erase_if(applied.inserts, [&](const auto& insert) { return insert.target == strip_id; });
    const bool changed = applied.sends.size() != sends_before ||
                         applied.input_delays.size() != delays_before ||
                         applied.inserts.size() != inserts_before;
    if (changed) {
        info_.levels = levels_for(applied, info_.levels);
        detail::bump_revision(info_.revision);
    }
    if (info_.staged) {
        // 検証の結果は観測値なので、変わっても revision は進めない。
        info_.staged->check = check_routing(info_.staged->config, strip_ids, plugins_);
    }
    return changed;
}

cp::RoutingConfig without_strip_references(cp::RoutingConfig config) {
    // ストリップの entity_id には決まった接頭辞がないので、構成の中で定義されていない端点を
    // ストリップとみなす。
    const auto defined = [&](const std::string& id) {
        const auto has = [&](const auto& items) {
            return std::any_of(items.begin(), items.end(),
                               [&](const auto& item) { return item.entity_id == id; });
        };
        return id == kMasterEntityId || has(config.buses) || has(config.outputs) ||
               has(config.delays);
    };
    std::erase_if(config.sends,
                  [&](const auto& send) { return !defined(send.from) || !defined(send.to); });
    std::erase_if(config.inserts, [&](const auto& insert) { return !defined(insert.target); });
    config.input_delays.clear();
    return config;
}

}  // namespace vocroz::session

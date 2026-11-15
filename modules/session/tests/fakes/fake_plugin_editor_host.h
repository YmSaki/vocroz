// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginEditorHost。開いているエディターをインスタンスのアドレスで覚える。
#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string_view>

#include "vocroz/session/ports/plugin_editor_host.h"

namespace vocroz::session {

/// 開閉を記録する PluginEditorHost。
class FakePluginEditorHost final : public PluginEditorHost {
public:
    [[nodiscard]] common::Result<void, PluginEditorError> open(
        PluginInstance& instance, std::string_view /*title*/) override {
        if (fail_with_) {
            return common::make_unexpected(*fail_with_);
        }
        open_.insert(&instance);
        return {};
    }

    /// 以降の open() を error で失敗させる。
    void fail_with(PluginEditorError error) noexcept {
        fail_with_ = error;
    }

    void close(const PluginInstance& instance) override {
        open_.erase(&instance);
        ++close_count_;
    }

    [[nodiscard]] bool is_open(const PluginInstance& instance) const override {
        return open_.contains(&instance);
    }

    void close_all() override {
        open_.clear();
    }

    /// 開いているエディターの数。
    [[nodiscard]] std::size_t open_count() const noexcept {
        return open_.size();
    }

    /// close() が呼ばれた回数。
    [[nodiscard]] int close_count() const noexcept {
        return close_count_;
    }

private:
    std::set<const PluginInstance*> open_;
    std::optional<PluginEditorError> fail_with_;
    int close_count_ = 0;
};

}  // namespace vocroz::session

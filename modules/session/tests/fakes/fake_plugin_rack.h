// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginRack。依頼と破棄を記録するだけで、結果はテストが
// HostSession::on_plugin_loaded() で届ける。読み込みの遅れと順序をテストが決められるようにする。
#pragma once

#include <string>
#include <vector>

#include "vocroz/session/ports/plugin_rack.h"

namespace vocroz::session {

/// 依頼を記録する PluginRack。
class FakePluginRack final : public PluginRack {
public:
    /// 読み込みの依頼 1 回。
    struct Load {
        std::string insert_id;
        PluginLoadTicket ticket = 0;
        std::string plugin_id;
    };

    void load(const std::string& insert_id, PluginLoadTicket ticket,
              const PluginDescription& description) override {
        loads_.push_back(Load{insert_id, ticket, description.identifier});
    }

    void release(const std::string& insert_id) override {
        releases_.push_back(insert_id);
    }

    /// 読み込みの依頼（古い順）。
    [[nodiscard]] const std::vector<Load>& loads() const noexcept {
        return loads_;
    }

    /// 破棄した挿入（古い順）。
    [[nodiscard]] const std::vector<std::string>& releases() const noexcept {
        return releases_;
    }

private:
    std::vector<Load> loads_;
    std::vector<std::string> releases_;
};

}  // namespace vocroz::session

// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginRack。依頼と破棄とエディターを開く依頼と状態を取る依頼を記録するだけで、
// 結果はテストが HostSession::on_plugin_loaded() と on_plugin_states_captured()
// で届ける。読み込みの遅れと順序をテストが決められるようにする。
#pragma once

#include <cstdint>
#include <span>
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
        /// 戻すよう渡された状態。
        std::vector<std::uint8_t> state;
    };

    void load(const std::string& insert_id, PluginLoadTicket ticket,
              const PluginDescription& description, std::span<const std::uint8_t> state) override {
        loads_.push_back(
            Load{insert_id, ticket, description.identifier, {state.begin(), state.end()}});
    }

    void capture_states(std::vector<std::string> insert_ids) override {
        captures_.push_back(std::move(insert_ids));
    }

    void release(const std::string& insert_id) override {
        releases_.push_back(insert_id);
    }

    void open_editor(const std::string& insert_id, const std::string& title) override {
        editors_.push_back(Editor{insert_id, title});
    }

    /// エディターを開く依頼 1 回。
    struct Editor {
        std::string insert_id;
        std::string title;
    };

    /// 読み込みの依頼（古い順）。
    [[nodiscard]] const std::vector<Load>& loads() const noexcept {
        return loads_;
    }

    /// 状態を取る依頼（古い順）。結果はテストが HostSession::on_plugin_states_captured() で届ける。
    [[nodiscard]] const std::vector<std::vector<std::string>>& captures() const noexcept {
        return captures_;
    }

    /// 破棄した挿入（古い順）。
    [[nodiscard]] const std::vector<std::string>& releases() const noexcept {
        return releases_;
    }

    /// エディターを開く依頼（古い順）。
    [[nodiscard]] const std::vector<Editor>& editors() const noexcept {
        return editors_;
    }

private:
    std::vector<Load> loads_;
    std::vector<std::vector<std::string>> captures_;
    std::vector<Editor> editors_;
    std::vector<std::string> releases_;
};

}  // namespace vocroz::session

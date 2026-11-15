// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の SettingsStore。メモリに設定を持ち、読み込みの失敗も再現できる。
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "vocroz/session/ports/settings_store.h"

namespace vocroz::session {

/// メモリに設定を持つ SettingsStore。値がなければ kNotFound を返す。
class FakeSettingsStore final : public SettingsStore {
public:
    common::Result<HostSettings, SettingsError> load_host() override {
        if (load_error_) {
            return common::make_unexpected(*load_error_);
        }
        if (!host_) {
            return common::make_unexpected(SettingsError::kNotFound);
        }
        return *host_;
    }

    common::Result<void, SettingsError> save_host(const HostSettings& settings) override {
        host_ = settings;
        ++save_count_;
        return {};
    }

    common::Result<std::vector<SavedPluginState>, SettingsError> load_plugin_states() override {
        if (load_error_) {
            return common::make_unexpected(*load_error_);
        }
        if (!plugin_states_) {
            return common::make_unexpected(SettingsError::kNotFound);
        }
        return *plugin_states_;
    }

    common::Result<void, SettingsError> save_plugin_states(
        std::span<const SavedPluginState> states) override {
        plugin_states_.emplace(states.begin(), states.end());
        ++plugin_state_save_count_;
        return {};
    }

    common::Result<ClientSettings, SettingsError> load_client() override {
        if (load_error_) {
            return common::make_unexpected(*load_error_);
        }
        if (!client_) {
            return common::make_unexpected(SettingsError::kNotFound);
        }
        return *client_;
    }

    common::Result<void, SettingsError> save_client(const ClientSettings& settings) override {
        client_ = settings;
        ++save_count_;
        return {};
    }

    /// 読み込みを error で失敗させる（壊れた設定ファイルの再現）。
    void fail_loads_with(SettingsError error) noexcept {
        load_error_ = error;
    }

    std::optional<HostSettings>& host() noexcept {
        return host_;
    }

    std::optional<ClientSettings>& client() noexcept {
        return client_;
    }

    std::optional<std::vector<SavedPluginState>>& plugin_states() noexcept {
        return plugin_states_;
    }

    [[nodiscard]] int save_count() const noexcept {
        return save_count_;
    }

    /// プラグインの状態を保存した回数。設定の保存（save_count()）とは別に数える。
    [[nodiscard]] int plugin_state_save_count() const noexcept {
        return plugin_state_save_count_;
    }

private:
    std::optional<HostSettings> host_;
    std::optional<ClientSettings> client_;
    std::optional<std::vector<SavedPluginState>> plugin_states_;
    std::optional<SettingsError> load_error_;
    int save_count_ = 0;
    int plugin_state_save_count_ = 0;
};

}  // namespace vocroz::session

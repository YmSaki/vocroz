// SPDX-License-Identifier: AGPL-3.0-only
//
// memory_settings_store.h の実装。

#include "memory_settings_store.h"

namespace vocroz::engine {

common::Result<session::HostSettings, session::SettingsError> MemorySettingsStore::load_host() {
    if (!host_) {
        return common::make_unexpected(session::SettingsError::kNotFound);
    }
    return *host_;
}

common::Result<void, session::SettingsError> MemorySettingsStore::save_host(
    const session::HostSettings& settings) {
    host_ = settings;
    return {};
}

common::Result<session::ClientSettings, session::SettingsError> MemorySettingsStore::load_client() {
    if (!client_) {
        return common::make_unexpected(session::SettingsError::kNotFound);
    }
    return *client_;
}

common::Result<void, session::SettingsError> MemorySettingsStore::save_client(
    const session::ClientSettings& settings) {
    client_ = settings;
    return {};
}

}  // namespace vocroz::engine

// SPDX-License-Identifier: AGPL-3.0-only
//
// メモリだけに置く SettingsStore。設定ファイルへの保存（persistence、P1 の #20）が入るまでの
// 仮の実装で、起動のたびに初回の起動（kNotFound）として振る舞う。P1 のマージ後にコンポジション
// ルートで差し替えて、このファイルは消す（docs/adr/0028-engine-assembly.md）。
#pragma once

#include <optional>

#include "vocroz/session/ports/settings_store.h"

namespace vocroz::engine {

/// プロセスの中でだけ設定を保持する SettingsStore。
/// スレッド安全性: コントロールスレッドからだけ呼ぶ。
class MemorySettingsStore final : public session::SettingsStore {
public:
    [[nodiscard]] common::Result<session::HostSettings, session::SettingsError> load_host()
        override;
    [[nodiscard]] common::Result<void, session::SettingsError> save_host(
        const session::HostSettings& settings) override;
    [[nodiscard]] common::Result<session::ClientSettings, session::SettingsError> load_client()
        override;
    [[nodiscard]] common::Result<void, session::SettingsError> save_client(
        const session::ClientSettings& settings) override;

private:
    std::optional<session::HostSettings> host_;
    std::optional<session::ClientSettings> client_;
};

}  // namespace vocroz::engine

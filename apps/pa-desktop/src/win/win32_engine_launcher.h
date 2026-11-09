// SPDX-License-Identifier: AGPL-3.0-only
//
// EngineLauncher の Win32 の実装。remote-pa と同じフォルダの pa-engine.exe を、同じデータフォルダと
// ログの段階で起動する（docs/adr/0030-remote-pa-assembly.md）。
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "vocroz/common/ports/logger.h"

#include "engine_launcher.h"

namespace vocroz::pa_desktop::win {

/// CreateProcessW で pa-engine を起動する EngineLauncher。
/// スレッド安全性: コントロールスレッドから呼ぶ。
class Win32EngineLauncher final : public EngineLauncher {
public:
    /// @param executable pa-engine.exe のパス
    /// @param data_dir   エンジンに渡すデータフォルダ（絶対パス）
    /// @param log_level  エンジンに渡すログの段階（--log-level の値）
    /// @param logger     起動の失敗の出力先。このオブジェクトより長く生存させる
    Win32EngineLauncher(std::filesystem::path executable, const std::filesystem::path& data_dir,
                        std::string_view log_level, common::Logger& logger);
    ~Win32EngineLauncher() override;

    Win32EngineLauncher(const Win32EngineLauncher&) = delete;
    Win32EngineLauncher& operator=(const Win32EngineLauncher&) = delete;

    [[nodiscard]] bool launch() override;
    [[nodiscard]] std::optional<int> exit_code() override;

private:
    void close_process() noexcept;

    std::filesystem::path executable_;
    std::wstring command_line_;
    common::Logger& logger_;
    void* process_ = nullptr;  // HANDLE。windows.h をヘッダーに出さないため void* で持つ
};

}  // namespace vocroz::pa_desktop::win

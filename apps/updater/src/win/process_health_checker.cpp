// SPDX-License-Identifier: AGPL-3.0-only
//
// process_health_checker.h の実装。

#include "win/process_health_checker.h"

#include <string>

#include "vocroz/common/version_info.h"
#include "vocroz/update/install_pointer.h"

#include "updater_constants.h"
#include "win/win32_process.h"
#include "win/win32_text.h"

namespace vocroz::updater::win {
namespace {

// 出力の末尾の改行を除く。--version は 1 行に改行を付けて出力する。
[[nodiscard]] std::string_view trim_line_end(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

ProcessHealthChecker::ProcessHealthChecker(std::filesystem::path install_dir)
    : install_dir_(std::move(install_dir)) {}

bool ProcessHealthChecker::check(const update::SemanticVersion& version) {
    const auto folder = install_dir_ / update::version_directory_name(version);
    const std::string version_text = update::to_string(version);
    for (const std::string_view name : kVersionedExecutables) {
        const auto executable = folder / (widen(name) + L".exe");
        const auto result = run_and_capture(executable, {L"--version"}, kHealthCheckTimeout);
        // 起動できること（DLL の欠落や破損がない）と、別の版のファイルが紛れ込んでいないことを、
        // 終了コードとバージョンの 1 行で確かめる。
        if (!result || result->exit_code != 0 ||
            trim_line_end(result->output) != common::version_line(name, version_text)) {
            return false;
        }
    }
    return true;
}

}  // namespace vocroz::updater::win

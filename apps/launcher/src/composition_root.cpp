// SPDX-License-Identifier: AGPL-3.0-only
//
// composition_root.h の実装。

#include "composition_root.h"

#include <filesystem>
#include <optional>

#include "vocroz/update/install_pointer.h"
#include "vocroz/update/launch_selection.h"
#include "vocroz/version.h"

#include "command_line.h"
#include "win/win32_launch.h"

namespace vocroz::launcher {
namespace {

// 起動する実行ファイル（バージョン別フォルダの中）。
constexpr std::wstring_view kTargetFileName = L"remote-pa.exe";

// どの版も起動できなかったときの表示。起動スタブには文字列リソースの仕組みがなく、
// ほぼ起こらない事態なので、日本語と英語を並べて出す。
constexpr std::wstring_view kLaunchFailedMessage =
    L"VOCROZ を起動できませんでした。インストーラーで修復するか、再インストールしてください。\n\n"
    L"VOCROZ could not be started. Repair or reinstall it with the installer.";

}  // namespace

CompositionRoot::CompositionRoot(std::wstring command_line)
    : command_line_(std::move(command_line)) {}

int CompositionRoot::run() {
    const auto install_dir = win::own_directory();
    if (!install_dir) {
        win::show_error(kLaunchFailedMessage);
        return 1;
    }
    const auto pointer_text = win::read_text_file(*install_dir / update::kInstallPointerFileName);
    const auto pointer = pointer_text ? update::parse_install_pointer(*pointer_text) : std::nullopt;
    const auto own_version =
        update::parse_semantic_version(kVersion).value_or(update::SemanticVersion{});

    const std::wstring_view arguments = arguments_of(command_line_);
    for (const auto& version : update::launch_candidates(pointer, own_version)) {
        const auto target = *install_dir / update::version_directory_name(version) /
                            std::filesystem::path{kTargetFileName};
        // 起動できない版（フォルダの欠落、DLL の破損）は飛ばし、次の候補を試す。
        if (win::launch(target, arguments)) {
            return 0;
        }
    }
    win::show_error(kLaunchFailedMessage);
    return 1;
}

}  // namespace vocroz::launcher

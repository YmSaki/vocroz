// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater のエントリポイント。引数のパース → コンポジションルートの構築 → 実行。
// 引数は内部用（docs/adr/0009-installer-and-updater.md）。
//   --version : バージョンを表示して終了
//   --check   : 更新を確認し、ダウンロードして検証する
//   --apply   : 検証済みの更新を適用する

#include <iostream>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

#include "launch_options.h"
#include "updater_constants.h"

#if defined(_WIN32)
#include "composition_root.h"
#include "win/known_paths.h"
#include "win/win32_text.h"
#endif

int main(int argc, char** argv) {
    namespace updater = vocroz::updater;
    const auto parsed = updater::parse_launch_options(vocroz::common::make_args(argc, argv));
    if (const auto* error = std::get_if<updater::LaunchError>(&parsed)) {
        std::cerr << "pa-updater: " << error->message << '\n';
        return updater::kExitUsage;
    }
    const auto& options = std::get<updater::LaunchOptions>(parsed);
    if (options.command == updater::Command::kVersion) {
        std::cout << vocroz::common::version_line("pa-updater") << '\n';
        return updater::kExitOk;
    }

#if defined(_WIN32)
    // 引数は UTF-8 で受け取る（アプリケーションマニフェストで activeCodePage を UTF-8
    // にしている）。
    const auto install_dir =
        options.install_dir
            ? std::optional{std::filesystem::path{updater::win::widen(*options.install_dir)}}
            : updater::win::default_install_dir();
    const auto data_dir =
        options.data_dir
            ? std::optional{std::filesystem::path{updater::win::widen(*options.data_dir)}}
            : updater::win::default_data_dir();
    if (!install_dir || !data_dir) {
        std::cerr << "pa-updater: cannot resolve the install or data folder\n";
        return updater::kExitFatal;
    }
    updater::CompositionRoot root{options, *install_dir, *data_dir};
    return root.run();
#else
    // 更新の対象は Windows の配布物だけ。Linux のビルドはドメインのテストのために存在する。
    std::cerr << "pa-updater: updates are supported on Windows only\n";
    return updater::kExitFatal;
#endif
}

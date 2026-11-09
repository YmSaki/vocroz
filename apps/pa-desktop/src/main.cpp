// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa のエントリポイント。引数をパースし、コンポジションルートを組み立てて実行する
// （docs/adr/0007-launch-interface.md）。
//   --version : バージョンを表示して終了
//   --help    : 引数の一覧を表示して終了
//   子機モード（--mode=client、--client、--join、vocroz://）: 出演者用の画面で起動
//   親機モード（指定なし）: 親機の操作卓の画面で起動し、pa-engine につなぐ

#include <iostream>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

#include "launch_options.h"

#if defined(VOCROZ_REMOTE_PA_HAS_UI)
#include "composition_root.h"
#include "win/console_output.h"
#endif

int main(int argc, char** argv) {
#if defined(VOCROZ_REMOTE_PA_HAS_UI)
    vocroz::pa_desktop::win::attach_parent_console();
#endif
    namespace app = vocroz::pa_desktop;
    const auto parsed = app::parse_launch_options(vocroz::common::make_args(argc, argv));
    if (const auto* error = std::get_if<app::LaunchError>(&parsed)) {
        std::cerr << error->message << '\n' << app::launch_options_help();
        return app::kExitUsage;
    }
    const auto& options = std::get<app::LaunchOptions>(parsed);
    switch (options.command) {
        case app::Command::kVersion:
            std::cout << vocroz::common::version_line("remote-pa") << '\n';
            return app::kExitOk;
        case app::Command::kHelp:
            std::cout << app::launch_options_help();
            return app::kExitOk;
        case app::Command::kRun:
            break;
    }

#if defined(VOCROZ_REMOTE_PA_HAS_UI)
    app::CompositionRoot root(options);
    return root.run();
#else
    // 画面（WebView2）と WASAPI は Windows で JUCE を使うビルドにしかない。
    std::cerr << "remote-pa: this build has no user interface\n";
    return app::kExitFatal;
#endif
}

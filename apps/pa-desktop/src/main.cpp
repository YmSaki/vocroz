// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa のエントリポイント。引数をパースし、モードに応じて親機モードか子機モードを実行する
// （docs/adr/0007-launch-interface.md）。
//   --version : バージョンを表示して終了
//   --help    : 引数の一覧を表示して終了
//   子機モード（--mode=client、--client、--join、vocroz://）: 出演者用の画面で起動
//   親機モード（指定なし）: 親機の操作卓 UI として起動（コンポジションルートは後続の PR）

#include <iostream>
#include <variant>

#include "vocroz/client/client_app.h"
#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

#include "launch_options.h"

int main(int argc, char** argv) {
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
    switch (options.mode) {
        case app::Mode::kClient:
            return vocroz::client::run_client(vocroz::common::make_args(argc, argv));
        case app::Mode::kHost:
            break;
    }
    std::cout << vocroz::common::version_line("remote-pa") << " (placeholder)\n";
    return app::kExitOk;
}

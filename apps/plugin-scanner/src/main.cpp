// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-plugin-scanner のエントリポイント。引数のパース → スキャン → 結果の出力。
// pa-engine がプラグインファイル 1 つにつき 1 回起動し、標準出力の JSON を受け取る
// （docs/adr/0009-beta-module-layout.md）。不良プラグインで落ちても、落ちるのはこのプロセスだけになる。
// 注入する依存がないので、コンポジションルートは持たない。
//   --version       : バージョンを表示して終了
//   --scan <ファイル> : プラグインファイル 1 つをスキャンし、結果を標準出力に書く

#include <iostream>
#include <variant>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"
#include "vocroz/plugin_host/factories.h"
#include "vocroz/plugin_host/scan_messages.h"

#include "launch_options.h"
#include "scanner_constants.h"

#if defined(_WIN32)
#include "win/crash_dialogs.h"
#endif

int main(int argc, char** argv) {
    namespace scanner = vocroz::plugin_scanner;
#if defined(_WIN32)
    scanner::win::suppress_crash_dialogs();
#endif
    const auto parsed = scanner::parse_launch_options(vocroz::common::make_args(argc, argv));
    if (const auto* error = std::get_if<scanner::LaunchError>(&parsed)) {
        std::cerr << "pa-plugin-scanner: " << error->message << '\n';
        return scanner::kExitUsage;
    }
    const auto& options = std::get<scanner::LaunchOptions>(parsed);
    if (options.command == scanner::Command::kVersion) {
        std::cout << vocroz::common::version_line("pa-plugin-scanner") << '\n';
        return scanner::kExitOk;
    }

    // 引数は UTF-8 で受け取る。
    // Windows ではアプリケーションマニフェストで activeCodePage を UTF-8 にしている。
    const std::filesystem::path file{
        std::u8string{options.plugin_file.begin(), options.plugin_file.end()}};
    const vocroz::plugin_host::ScanReport report = vocroz::plugin_host::scan_in_process(file);
    std::cout << vocroz::plugin_host::serialize_scan_report(report) << std::flush;
    return scanner::kExitOk;
}

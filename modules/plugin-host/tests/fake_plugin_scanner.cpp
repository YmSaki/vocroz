// SPDX-License-Identifier: AGPL-3.0-only
//
// scan_in_helper() のテストで、pa-plugin-scanner の代わりに起動する実行ファイル。
// 「--scan
// <ファイル>」のファイル名で、不良プラグインをスキャンしたときのヘルパーの振る舞いをまねる。
//   crash   : アクセス違反で落ちる
//   hang    : 終わらない
//   garbage : スキーマに合わない出力を書く
//   silent  : 何も書かずに終了コード 0 で終わる
//   その他  : そのファイルのプラグインを 1 つ見つけたとして結果を書く

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "vocroz/plugin_host/scan_messages.h"

namespace {

[[noreturn]] void crash() {
#if defined(_WIN32)
    // CI でエラー報告のダイアログを出さずに落ちるようにする（出すと制限時間まで待つことになる）。
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    std::raise(SIGSEGV);
    std::abort();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--scan") {
        return 2;
    }
    const std::string path = argv[2];
    const std::string name = std::filesystem::path(path).filename().string();
    if (name == "crash") {
        crash();
    }
    if (name == "hang") {
        std::this_thread::sleep_for(std::chrono::minutes(10));
        return 0;
    }
    if (name == "garbage") {
        std::cout << "{\"version\":1,\"plugins\":[{\"name\":";
        return 0;
    }
    if (name == "silent") {
        return 0;
    }
    vocroz::plugin_host::PluginDescription plugin;
    plugin.name = name;
    plugin.vendor = "Fake";
    plugin.version = "1.0.0";
    plugin.file_path = path;
    plugin.identifier = "VST3-" + name;
    plugin.input_channels = 2;
    plugin.output_channels = 2;
    std::cout << vocroz::plugin_host::serialize_scan_report({{plugin}});
    return 0;
}

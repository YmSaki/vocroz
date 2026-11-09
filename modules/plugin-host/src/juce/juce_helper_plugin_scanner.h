// SPDX-License-Identifier: AGPL-3.0-only
//
// スキャン用ヘルパー（pa-plugin-scanner）で、session の PluginScanner ポートを実装する。
// ファイルの列挙は find_plugin_files()、スキャンはファイルごとの scan_in_helper() に任せ、
// ヘルパーの失敗の理由を session の区分に写す。
#pragma once

#include <chrono>
#include <filesystem>
#include <vector>

#include "vocroz/session/ports/plugin_scanner.h"

namespace vocroz::plugin_host {

/// ファイルごとにヘルパーを起動してスキャンする。
/// スレッド安全性: PluginCatalog と同じスレッドから呼ぶ。scan() はヘルパーの終了までブロックする。
class JuceHelperPluginScanner final : public session::PluginScanner {
public:
    /// @param helper ヘルパーの実行ファイル
    /// @param directories 探す場所
    /// @param timeout ファイル 1 つの制限時間
    JuceHelperPluginScanner(std::filesystem::path helper,
                            std::vector<std::filesystem::path> directories,
                            std::chrono::milliseconds timeout);

    [[nodiscard]] std::vector<session::PluginFile> list_plugin_files() override;
    [[nodiscard]] common::Result<std::vector<session::PluginDescription>,
                                 session::PluginScanFailure>
    scan(const session::PluginFile& file) override;

private:
    std::filesystem::path helper_;
    std::vector<std::filesystem::path> directories_;
    std::chrono::milliseconds timeout_;
};

}  // namespace vocroz::plugin_host

// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の PluginScanner。列挙するファイルとファイルごとのスキャン結果を、テストが決める。
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "vocroz/session/ports/plugin_scanner.h"

namespace vocroz::session {

/// 決めたとおりに列挙とスキャンの結果を返す PluginScanner。
/// 結果を決めていないファイルは、空の結果を返す。
class FakePluginScanner final : public PluginScanner {
public:
    std::vector<PluginFile> list_plugin_files() override {
        return files_;
    }

    common::Result<std::vector<PluginDescription>, PluginScanFailure> scan(
        const PluginFile& file) override {
        scanned_.push_back(file.path);
        if (const auto failure = failures_.find(file.path); failure != failures_.end()) {
            return common::make_unexpected(failure->second);
        }
        if (const auto plugins = plugins_.find(file.path); plugins != plugins_.end()) {
            return plugins->second;
        }
        return std::vector<PluginDescription>{};
    }

    /// list_plugin_files() が返すファイルを決める。
    void set_files(std::vector<PluginFile> files) {
        files_ = std::move(files);
    }

    /// path のスキャンで plugins を返す。
    void set_plugins(const std::string& path, std::vector<PluginDescription> plugins) {
        failures_.erase(path);
        plugins_[path] = std::move(plugins);
    }

    /// path のスキャンを failure で失敗させる。
    void fail(const std::string& path, PluginScanFailure failure) {
        failures_[path] = failure;
    }

    /// @return scan() を呼んだファイルのパス（呼んだ順）
    [[nodiscard]] const std::vector<std::string>& scanned() const noexcept {
        return scanned_;
    }

private:
    std::vector<PluginFile> files_;
    std::map<std::string, std::vector<PluginDescription>> plugins_;
    std::map<std::string, PluginScanFailure> failures_;
    std::vector<std::string> scanned_;
};

}  // namespace vocroz::session

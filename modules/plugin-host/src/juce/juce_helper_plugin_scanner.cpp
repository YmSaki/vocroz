// SPDX-License-Identifier: AGPL-3.0-only
//
// JuceHelperPluginScanner（juce_helper_plugin_scanner.h）と make_helper_plugin_scanner() の実装。

#include "juce_helper_plugin_scanner.h"

#include <memory>
#include <string>
#include <utility>

#include "vocroz/plugin_host/factories.h"

#include "plugin_file_finder.h"

namespace vocroz::plugin_host {
namespace {

session::PluginScanFailure to_scan_failure(HelperScanError error) {
    switch (error) {
        case HelperScanError::kLaunchFailed:
            return session::PluginScanFailure::kScannerUnavailable;
        case HelperScanError::kCrashed:
            return session::PluginScanFailure::kCrashed;
        case HelperScanError::kTimedOut:
            return session::PluginScanFailure::kTimedOut;
        case HelperScanError::kInvalidOutput:
            return session::PluginScanFailure::kInvalidOutput;
    }
    return session::PluginScanFailure::kInvalidOutput;
}

std::filesystem::path from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace

JuceHelperPluginScanner::JuceHelperPluginScanner(std::filesystem::path helper,
                                                 std::vector<std::filesystem::path> directories,
                                                 std::chrono::milliseconds timeout)
    : helper_(std::move(helper)), directories_(std::move(directories)), timeout_(timeout) {}

std::vector<session::PluginFile> JuceHelperPluginScanner::list_plugin_files() {
    return find_plugin_files(directories_);
}

common::Result<std::vector<session::PluginDescription>, session::PluginScanFailure>
JuceHelperPluginScanner::scan(const session::PluginFile& file) {
    auto report = scan_in_helper(helper_, from_utf8(file.path), timeout_);
    if (!report) {
        return common::make_unexpected(to_scan_failure(report.error()));
    }
    return std::move(report.value().plugins);
}

std::unique_ptr<session::PluginScanner> make_helper_plugin_scanner(
    std::filesystem::path helper, std::vector<std::filesystem::path> directories,
    std::chrono::milliseconds timeout) {
    return std::make_unique<JuceHelperPluginScanner>(std::move(helper), std::move(directories),
                                                     timeout);
}

}  // namespace vocroz::plugin_host

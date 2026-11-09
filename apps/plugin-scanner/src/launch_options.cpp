// SPDX-License-Identifier: AGPL-3.0-only
//
// launch_options.h の実装。

#include "launch_options.h"

#include <string_view>

namespace vocroz::plugin_scanner {

std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args) {
    if (args.size() == 1 && std::string_view{args[0]} == "--version") {
        return LaunchOptions{Command::kVersion, {}};
    }
    if (args.size() == 2 && std::string_view{args[0]} == "--scan") {
        const std::string_view file{args[1]};
        if (file.empty()) {
            return LaunchError{"--scan には空でないパスを指定します"};
        }
        return LaunchOptions{Command::kScan, std::string{file}};
    }
    return LaunchError{"--version か --scan <ファイル> のどちらかだけを指定します"};
}

}  // namespace vocroz::plugin_scanner

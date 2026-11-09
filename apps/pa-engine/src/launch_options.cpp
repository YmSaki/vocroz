// SPDX-License-Identifier: AGPL-3.0-only
//
// launch_options.h の実装。

#include "launch_options.h"

#include <string_view>

namespace vocroz::engine {
namespace {

// "--name=value" の value を取り出す。name が一致しなければ std::nullopt。
[[nodiscard]] std::optional<std::string_view> value_of(std::string_view arg,
                                                       std::string_view name) {
    if (arg.size() <= name.size() || arg.substr(0, name.size()) != name ||
        arg[name.size()] != '=') {
        return std::nullopt;
    }
    return arg.substr(name.size() + 1);
}

// --log-level の値。ADR-0007 の 4 段階だけを受け付ける。
[[nodiscard]] std::optional<common::LogLevel> parse_log_level(std::string_view value) {
    if (value == "error") {
        return common::LogLevel::kError;
    }
    if (value == "warn") {
        return common::LogLevel::kWarning;
    }
    if (value == "info") {
        return common::LogLevel::kInfo;
    }
    if (value == "debug") {
        return common::LogLevel::kDebug;
    }
    return std::nullopt;
}

}  // namespace

std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args) {
    LaunchOptions options;
    bool has_log_level = false;

    for (const char* raw : args) {
        const std::string_view arg{raw};
        if (arg == "--version") {
            options.command = Command::kVersion;
        } else if (const auto data = value_of(arg, "--data-dir")) {
            if (data->empty() || options.data_dir) {
                return LaunchError{"--data-dir には空でないパスを 1 回だけ指定します"};
            }
            options.data_dir = std::string{*data};
        } else if (const auto level = value_of(arg, "--log-level")) {
            const auto parsed = parse_log_level(*level);
            if (!parsed || has_log_level) {
                return LaunchError{
                    "--log-level には error、warn、info、debug のどれかを 1 回だけ指定します"};
            }
            options.log_level = *parsed;
            has_log_level = true;
        } else {
            return LaunchError{"未知の引数です: " + std::string{arg}};
        }
    }
    return options;
}

}  // namespace vocroz::engine

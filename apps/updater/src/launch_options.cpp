// SPDX-License-Identifier: AGPL-3.0-only
//
// launch_options.h の実装。

#include "launch_options.h"

#include <string_view>

namespace vocroz::updater {
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

[[nodiscard]] bool set_command(std::optional<Command>& command, Command value) {
    if (command) {
        return false;
    }
    command = value;
    return true;
}

}  // namespace

std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args) {
    LaunchOptions options;
    std::optional<Command> command;
    bool after_separator = false;

    for (const char* raw : args) {
        const std::string_view arg{raw};
        if (after_separator) {
            options.restart_args.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            after_separator = true;
        } else if (arg == "--version") {
            if (!set_command(command, Command::kVersion)) {
                return LaunchError{"--version、--check、--apply は 1 つだけ指定します"};
            }
        } else if (arg == "--check") {
            if (!set_command(command, Command::kCheck)) {
                return LaunchError{"--version、--check、--apply は 1 つだけ指定します"};
            }
        } else if (arg == "--apply") {
            if (!set_command(command, Command::kApply)) {
                return LaunchError{"--version、--check、--apply は 1 つだけ指定します"};
            }
        } else if (arg == "--restart") {
            options.restart = true;
        } else if (const auto dir = value_of(arg, "--install-dir")) {
            if (dir->empty() || options.install_dir) {
                return LaunchError{"--install-dir には空でないパスを 1 回だけ指定します"};
            }
            options.install_dir = std::string{*dir};
        } else if (const auto data = value_of(arg, "--data-dir")) {
            if (data->empty() || options.data_dir) {
                return LaunchError{"--data-dir には空でないパスを 1 回だけ指定します"};
            }
            options.data_dir = std::string{*data};
        } else {
            return LaunchError{"未知の引数です: " + std::string{arg}};
        }
    }

    if (!command) {
        return LaunchError{"--version、--check、--apply のいずれかを指定します"};
    }
    options.command = *command;
    if (options.restart && options.command != Command::kApply) {
        return LaunchError{"--restart は --apply と一緒にだけ指定できます"};
    }
    if (!options.restart_args.empty() && !options.restart) {
        return LaunchError{"\"--\" より後ろの引数は --restart と一緒にだけ指定できます"};
    }
    return options;
}

}  // namespace vocroz::updater

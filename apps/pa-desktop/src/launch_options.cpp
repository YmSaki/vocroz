// SPDX-License-Identifier: AGPL-3.0-only
//
// parse_launch_options() の実装。

#include "launch_options.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>
#include <utility>

namespace vocroz::pa_desktop {
namespace {

// "--name=value" の value を取り出す。name が違えば std::nullopt。
std::optional<std::string_view> value_of(std::string_view arg, std::string_view name) {
    if (arg.size() <= name.size() || !arg.starts_with(name) || arg[name.size()] != '=') {
        return std::nullopt;
    }
    return arg.substr(name.size() + 1);
}

// URL スキームは大文字小文字を区別しない（RFC 3986）。ブラウザは小文字で渡すが、手入力に備える。
bool is_url_scheme_argument(std::string_view arg) {
    constexpr std::string_view kScheme = "vocroz:";
    if (arg.size() < kScheme.size()) {
        return false;
    }
    return std::equal(kScheme.begin(), kScheme.end(), arg.begin(), [](char a, char b) {
        return a == std::tolower(static_cast<unsigned char>(b));
    });
}

std::optional<common::LogLevel> parse_log_level(std::string_view text) {
    if (text == "error") {
        return common::LogLevel::kError;
    }
    if (text == "warn") {
        return common::LogLevel::kWarning;
    }
    if (text == "info") {
        return common::LogLevel::kInfo;
    }
    if (text == "debug") {
        return common::LogLevel::kDebug;
    }
    return std::nullopt;
}

LaunchError error(std::string message) {
    return LaunchError{std::move(message)};
}

}  // namespace

std::variant<LaunchOptions, LaunchError> parse_launch_options(common::Args args) {
    LaunchOptions options;
    std::optional<Mode> explicit_mode;
    std::set<std::string, std::less<>> seen;
    bool version = false;
    bool help = false;

    for (const char* raw : args) {
        const std::string_view arg(raw);
        // 同じ引数の二重指定は、どちらを採るかの推測になるのでエラーにする。
        const std::string key(arg.substr(0, arg.find('=')));
        if (!seen.insert(key).second) {
            return error("引数が二重に指定されています: " + key);
        }
        if (arg == "--version") {
            version = true;
        } else if (arg == "--help") {
            help = true;
        } else if (arg == "--client" || arg == "--mode=client") {
            if (explicit_mode == Mode::kHost) {
                return error("--mode=host と --mode=client（--client）は同時に指定できません");
            }
            explicit_mode = Mode::kClient;
        } else if (arg == "--mode=host") {
            if (explicit_mode == Mode::kClient) {
                return error("--mode=host と --mode=client（--client）は同時に指定できません");
            }
            explicit_mode = Mode::kHost;
        } else if (const auto join = value_of(arg, "--join")) {
            if (options.join) {
                return error("招待は 1 つだけ指定できます（--join と vocroz:// の重複）");
            }
            options.join = std::string(*join);
        } else if (is_url_scheme_argument(arg)) {
            if (options.join) {
                return error("招待は 1 つだけ指定できます（--join と vocroz:// の重複）");
            }
            // URL のまま渡し、招待コードへの変換と検証は session の parse_invite() に任せる。
            options.join = std::string(arg);
        } else if (const auto dir = value_of(arg, "--data-dir")) {
            options.data_dir = std::string(*dir);
        } else if (const auto lang = value_of(arg, "--lang")) {
            if (*lang != "ja" && *lang != "en") {
                return error("--lang は ja か en を指定してください");
            }
            options.lang = std::string(*lang);
        } else if (const auto level = value_of(arg, "--log-level")) {
            const auto parsed = parse_log_level(*level);
            if (!parsed) {
                return error("--log-level は error、warn、info、debug のどれかを指定してください");
            }
            options.log_level = *parsed;
        } else if (arg == "--no-update-check") {
            options.update_check = false;
        } else if (const auto url = value_of(arg, "--rendezvous-url")) {
            options.rendezvous_url = std::string(*url);
        } else {
            return error("未知の引数です: " + std::string(arg));
        }
    }

    if (options.join && explicit_mode == Mode::kHost) {
        return error("--mode=host と招待（--join、vocroz://）は同時に指定できません");
    }
    // 招待があればモードの指定がなくても子機モードにする（招待 URL から起動した場合）。
    options.mode = explicit_mode.value_or(options.join ? Mode::kClient : Mode::kHost);
    if (help) {
        options.command = Command::kHelp;
    } else if (version) {
        options.command = Command::kVersion;
    }
    return options;
}

std::string_view log_level_argument(common::LogLevel level) noexcept {
    switch (level) {
        case common::LogLevel::kError:
            return "error";
        case common::LogLevel::kWarning:
            return "warn";
        case common::LogLevel::kInfo:
            return "info";
        case common::LogLevel::kDebug:
            return "debug";
    }
    return "info";
}

std::string launch_options_help() {
    return "使い方: remote-pa [オプション] [vocroz://join?code=<招待コード>]\n"
           "\n"
           "  （なし）                     親機モードで起動する\n"
           "  --mode=host | --mode=client  モードを指定する（--client は --mode=client と同じ）\n"
           "  --join=<招待 URL かコード>   子機モードで起動し、その招待の確認画面を開く\n"
           "  --data-dir=<パス>            設定とログの置き場所（既定は %LOCALAPPDATA%\\VOCROZ）\n"
           "  --lang=ja | --lang=en        表示言語（既定は OS の表示言語）\n"
           "  --log-level=error|warn|info|debug  ログの詳細度（既定は info）\n"
           "  --no-update-check            起動時に更新を確認しない\n"
           "  --rendezvous-url=<URL>       ランデブーサーバーを変える（検証用）\n"
           "  --version                    バージョンを表示して終了する\n"
           "  --help                       この一覧を表示して終了する\n";
}

}  // namespace vocroz::pa_desktop

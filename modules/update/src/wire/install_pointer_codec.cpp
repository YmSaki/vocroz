// SPDX-License-Identifier: AGPL-3.0-only
//
// install_pointer.h のパースとシリアライズ。ポインターファイルはディスク上の外部入力なので、
// ここで検証し、壊れていれば「ポインターなし」として扱わせる。

#include "vocroz/update/install_pointer.h"

#include "detail/text_utils.h"

namespace vocroz::update {
namespace {

constexpr std::string_view kVersionDirectoryPrefix = "app-";

}  // namespace

std::optional<InstallPointer> parse_install_pointer(std::string_view text) {
    if (text.empty() || text.back() != '\n') {
        return std::nullopt;
    }
    text.remove_suffix(1);
    const auto lines = detail::split(text, '\n');
    if (lines.empty() || lines.size() > 2) {
        return std::nullopt;
    }
    const auto current_text = detail::value_of(lines[0], "current");
    if (!current_text) {
        return std::nullopt;
    }
    const auto current = parse_semantic_version(*current_text);
    if (!current) {
        return std::nullopt;
    }
    InstallPointer pointer{*current, std::nullopt};
    if (lines.size() == 2) {
        const auto previous_text = detail::value_of(lines[1], "previous");
        if (!previous_text) {
            return std::nullopt;
        }
        pointer.previous = parse_semantic_version(*previous_text);
        if (!pointer.previous) {
            return std::nullopt;
        }
    }
    return pointer;
}

std::string serialize_install_pointer(const InstallPointer& pointer) {
    std::string text = "current=" + to_string(pointer.current) + '\n';
    if (pointer.previous) {
        text += "previous=" + to_string(*pointer.previous) + '\n';
    }
    return text;
}

std::string version_directory_name(const SemanticVersion& version) {
    return std::string{kVersionDirectoryPrefix} + to_string(version);
}

std::optional<SemanticVersion> parse_version_directory_name(std::string_view name) {
    if (name.substr(0, kVersionDirectoryPrefix.size()) != kVersionDirectoryPrefix) {
        return std::nullopt;
    }
    return parse_semantic_version(name.substr(kVersionDirectoryPrefix.size()));
}

}  // namespace vocroz::update

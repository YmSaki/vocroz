// SPDX-License-Identifier: AGPL-3.0-only
//
// file_name_utils.h の実装。

#include "detail/file_name_utils.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace vocroz::session::detail {
namespace {

[[nodiscard]] bool is_forbidden(unsigned char c) noexcept {
    constexpr std::string_view kForbidden = "<>:\"/\\|?*";
    return c < 0x20 || c == 0x7F || kForbidden.find(static_cast<char>(c)) != std::string_view::npos;
}

// UTF-8 の継続バイト（10xxxxxx）なら true。
[[nodiscard]] bool is_continuation(unsigned char c) noexcept {
    return (c & 0xC0U) == 0x80U;
}

[[nodiscard]] std::string upper_ascii(std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

// Windows が予約しているデバイス名なら true。拡張子が付いていても予約名として扱われる。
[[nodiscard]] bool is_reserved_device_name(std::string_view name) {
    const std::string stem = upper_ascii(name.substr(0, name.find('.')));
    constexpr std::array<std::string_view, 4> kNames = {"CON", "PRN", "AUX", "NUL"};
    if (std::ranges::find(kNames, stem) != kNames.end()) {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

}  // namespace

std::string to_file_name_part(std::string_view name, std::size_t max_bytes,
                              std::string_view fallback) {
    std::string result;
    result.reserve(name.size());
    for (const char c : name) {
        result.push_back(is_forbidden(static_cast<unsigned char>(c)) ? '_' : c);
    }
    if (result.size() > max_bytes) {
        std::size_t end = max_bytes;
        while (end > 0 && is_continuation(static_cast<unsigned char>(result[end]))) {
            --end;
        }
        result.resize(end);
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    if (result.empty()) {
        return std::string(fallback);
    }
    if (is_reserved_device_name(result)) {
        result.insert(result.begin(), '_');
        if (result.size() > max_bytes) {
            result.resize(max_bytes);
        }
    }
    return result;
}

}  // namespace vocroz::session::detail

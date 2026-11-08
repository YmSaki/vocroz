// SPDX-License-Identifier: AGPL-3.0-only
//
// semantic_version.h の実装。

#include "vocroz/update/semantic_version.h"

#include <algorithm>
#include <charconv>
#include <limits>

#include "detail/text_utils.h"

namespace vocroz::update {
namespace {

// 数値の識別子として検証する。先頭のゼロは、"01" と "1" を同じ版とみなすかどうかの
// 曖昧さを生むため Semantic Versioning と同じく拒否する。
[[nodiscard]] std::optional<std::uint32_t> parse_numeric(std::string_view text) {
    if (text.empty() || !detail::is_all_digits(text)) {
        return std::nullopt;
    }
    if (text.size() > 1 && text.front() == '0') {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool is_identifier_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}

[[nodiscard]] bool is_valid_prerelease(std::string_view text) {
    for (const std::string_view identifier : detail::split(text, '.')) {
        if (identifier.empty()) {
            return false;
        }
        for (const char c : identifier) {
            if (!is_identifier_char(c)) {
                return false;
            }
        }
        if (detail::is_all_digits(identifier) && identifier.size() > 1 &&
            identifier.front() == '0') {
            return false;
        }
    }
    return true;
}

// プレリリース識別子 1 つ分の優先順位。数値の識別子は数値として比べ、英数字より古い。
[[nodiscard]] std::strong_ordering compare_identifier(std::string_view lhs, std::string_view rhs) {
    const bool lhs_numeric = detail::is_all_digits(lhs);
    const bool rhs_numeric = detail::is_all_digits(rhs);
    if (lhs_numeric && rhs_numeric) {
        // 先頭のゼロはパースで拒否しているので、桁数の比較で数値の大小が決まる。
        if (lhs.size() != rhs.size()) {
            return lhs.size() <=> rhs.size();
        }
        return lhs.compare(rhs) <=> 0;
    }
    if (lhs_numeric != rhs_numeric) {
        return lhs_numeric ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return lhs.compare(rhs) <=> 0;
}

[[nodiscard]] std::strong_ordering compare_prerelease(std::string_view lhs, std::string_view rhs) {
    // プレリリースのない正式版は、同じ番号のどのプレリリースよりも新しい。
    if (lhs.empty() || rhs.empty()) {
        return lhs.empty() <=> rhs.empty();
    }
    const auto lhs_ids = detail::split(lhs, '.');
    const auto rhs_ids = detail::split(rhs, '.');
    const std::size_t common = std::min(lhs_ids.size(), rhs_ids.size());
    for (std::size_t i = 0; i < common; ++i) {
        const std::strong_ordering order = compare_identifier(lhs_ids[i], rhs_ids[i]);
        if (order != std::strong_ordering::equal) {
            return order;
        }
    }
    return lhs_ids.size() <=> rhs_ids.size();
}

}  // namespace

std::strong_ordering operator<=>(const SemanticVersion& lhs, const SemanticVersion& rhs) {
    if (const auto order = lhs.major <=> rhs.major; order != 0) {
        return order;
    }
    if (const auto order = lhs.minor <=> rhs.minor; order != 0) {
        return order;
    }
    if (const auto order = lhs.patch <=> rhs.patch; order != 0) {
        return order;
    }
    return compare_prerelease(lhs.prerelease, rhs.prerelease);
}

std::optional<SemanticVersion> parse_semantic_version(std::string_view text) {
    std::string_view core = text;
    std::string_view prerelease;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        core = text.substr(0, dash);
        prerelease = text.substr(dash + 1);
        if (!is_valid_prerelease(prerelease)) {
            return std::nullopt;
        }
    }
    const auto parts = detail::split(core, '.');
    if (parts.size() != 3) {
        return std::nullopt;
    }
    const auto major = parse_numeric(parts[0]);
    const auto minor = parse_numeric(parts[1]);
    const auto patch = parse_numeric(parts[2]);
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    return SemanticVersion{*major, *minor, *patch, std::string{prerelease}};
}

std::string to_string(const SemanticVersion& version) {
    std::string text = std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' +
                       std::to_string(version.patch);
    if (!version.prerelease.empty()) {
        text += '-';
        text += version.prerelease;
    }
    return text;
}

}  // namespace vocroz::update

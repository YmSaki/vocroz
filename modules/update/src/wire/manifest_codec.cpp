// SPDX-License-Identifier: AGPL-3.0-only
//
// manifest_codec.h の実装。

#include "wire/manifest_codec.h"

#include <charconv>
#include <vector>

#include "detail/text_utils.h"
#include "update_constants.h"

namespace vocroz::update::wire {
namespace {

constexpr std::string_view kHttpsScheme = "https://";

// 本文の行の数。キーを追加するときは kManifestFormat も上げる。
constexpr std::size_t kBodyLineCount = 7;

[[nodiscard]] bool contains_control_char(std::string_view text) noexcept {
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::uint64_t> parse_size(std::string_view text) {
    if (!detail::is_all_digits(text) || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<SignedManifestText> split_signed_manifest(std::string_view text) {
    // 末尾の改行は 1 つだけ許す。署名の行の後ろに何かを足しても検証を通る、という
    // 余地を残さないため。
    if (!text.empty() && text.back() == '\n') {
        text.remove_suffix(1);
    }
    const std::size_t last_newline = text.rfind('\n');
    if (last_newline == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view signature_line = text.substr(last_newline + 1);
    const auto signature_hex = detail::value_of(signature_line, kSignatureKey);
    if (!signature_hex) {
        return std::nullopt;
    }
    SignedManifestText signed_text;
    if (!detail::decode_lower_hex(*signature_hex, signed_text.signature)) {
        return std::nullopt;
    }
    signed_text.body = text.substr(0, last_newline + 1);
    return signed_text;
}

ManifestResult decode_manifest_body(std::string_view body) {
    if (body.empty() || body.back() != '\n') {
        return ManifestError::kMalformed;
    }
    body.remove_suffix(1);
    const std::vector<std::string_view> lines = detail::split(body, '\n');
    if (lines.size() != kBodyLineCount) {
        return ManifestError::kMalformed;
    }
    for (const std::string_view line : lines) {
        if (contains_control_char(line)) {
            return ManifestError::kMalformed;
        }
    }

    const auto format = detail::value_of(lines[0], "format");
    const auto product = detail::value_of(lines[1], "product");
    const auto version = detail::value_of(lines[2], "version");
    const auto url = detail::value_of(lines[3], "url");
    const auto sha256 = detail::value_of(lines[4], "sha256");
    const auto size = detail::value_of(lines[5], "size");
    const auto publisher = detail::value_of(lines[6], "publisher");
    if (!format || !product || !version || !url || !sha256 || !size || !publisher) {
        return ManifestError::kMalformed;
    }

    if (*format != kManifestFormat) {
        return ManifestError::kUnsupportedFormat;
    }
    if (*product != kProductId) {
        return ManifestError::kWrongProduct;
    }
    // 平文の HTTP は経路上で差し替えられる。SHA-256 で改ざんは検出できるが、
    // 取得先を利用者に推測させる情報も漏れるため、HTTPS 以外は受け付けない。
    if (url->size() <= kHttpsScheme.size() || url->substr(0, kHttpsScheme.size()) != kHttpsScheme) {
        return ManifestError::kInsecureUrl;
    }

    UpdateManifest manifest;
    const auto parsed_version = parse_semantic_version(*version);
    const auto parsed_size = parse_size(*size);
    if (!parsed_version || !parsed_size ||
        !detail::decode_lower_hex(*sha256, manifest.package_sha256)) {
        return ManifestError::kInvalidField;
    }
    if (*parsed_size == 0 || *parsed_size > kMaxPackageSize) {
        return ManifestError::kTooLarge;
    }
    manifest.version = *parsed_version;
    manifest.package_url = std::string{*url};
    manifest.package_size = *parsed_size;
    manifest.publisher = std::string{*publisher};
    return manifest;
}

}  // namespace vocroz::update::wire

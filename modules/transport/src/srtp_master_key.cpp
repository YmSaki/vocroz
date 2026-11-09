// SPDX-License-Identifier: AGPL-3.0-only
//
// SRTP のマスターキーのパース、書式化、生成。Base64 と乱数と消去は libsodium を使う。

#include "vocroz/transport/srtp_master_key.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include <sodium.h>

#include "detail/crypto_utils.h"

namespace vocroz::transport {
namespace {

// SDES の a=crypto 属性で鍵の前に付く接頭辞（RFC 4568）。
constexpr std::string_view kInlinePrefix = "inline:";
// 鍵の寿命と MKI の区切り。
constexpr char kParameterSeparator = '|';
// 30 バイトを Base64 にした長さ（終端の NUL を含む）。30 バイトは 3 の倍数なので、
// パディングの付かない 40 文字になる。
constexpr std::size_t kBase64Chars =
    sodium_base64_ENCODED_LEN(kSrtpKeyMaterialBytes, sodium_base64_VARIANT_ORIGINAL);

constexpr std::string_view kWhitespace = " \t\r\n";

std::string_view trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

}  // namespace

SrtpMasterKey::SrtpMasterKey(const SrtpMasterKey& other) noexcept = default;

SrtpMasterKey& SrtpMasterKey::operator=(const SrtpMasterKey& other) noexcept = default;

SrtpMasterKey::SrtpMasterKey(SrtpMasterKey&& other) noexcept : material_(other.material_) {
    sodium_memzero(other.material_.data(), other.material_.size());
}

SrtpMasterKey& SrtpMasterKey::operator=(SrtpMasterKey&& other) noexcept {
    if (this != &other) {
        material_ = other.material_;
        sodium_memzero(other.material_.data(), other.material_.size());
    }
    return *this;
}

SrtpMasterKey::~SrtpMasterKey() {
    sodium_memzero(material_.data(), material_.size());
}

common::Result<SrtpMasterKey, SrtpKeyError> parse_srtp_master_key(std::string_view text) noexcept {
    text = trim(text);
    if (text.starts_with(kInlinePrefix)) {
        text.remove_prefix(kInlinePrefix.size());
    }
    if (text.find(kParameterSeparator) != std::string_view::npos) {
        return common::make_unexpected(SrtpKeyError::kUnsupportedParameter);
    }
    // 終端の NUL を含む長さ。長さが違えば Base64 のデコードの前に拒否する。
    if (text.size() + 1 != kBase64Chars) {
        return common::make_unexpected(SrtpKeyError::kMalformed);
    }
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(SrtpKeyError::kRandomUnavailable);
    }
    SrtpMasterKey key;
    std::size_t decoded = 0;
    const char* end = nullptr;
    const int status =
        sodium_base642bin(key.material_.data(), key.material_.size(), text.data(), text.size(),
                          nullptr, &decoded, &end, sodium_base64_VARIANT_ORIGINAL);
    if (status != 0 || decoded != kSrtpKeyMaterialBytes || end != text.data() + text.size()) {
        return common::make_unexpected(SrtpKeyError::kMalformed);
    }
    if (sodium_is_zero(key.material_.data(), key.material_.size()) != 0) {
        return common::make_unexpected(SrtpKeyError::kAllZero);
    }
    return key;
}

std::string format_srtp_master_key(const SrtpMasterKey& key) {
    std::array<char, kBase64Chars> encoded{};
    const auto material = key.material();
    sodium_bin2base64(encoded.data(), encoded.size(), material.data(), material.size(),
                      sodium_base64_VARIANT_ORIGINAL);
    std::string text{kInlinePrefix};
    text.append(encoded.data());
    sodium_memzero(encoded.data(), encoded.size());
    return text;
}

common::Result<SrtpMasterKey, SrtpKeyError> generate_srtp_master_key() noexcept {
    if (!detail::ensure_crypto_initialized()) {
        return common::make_unexpected(SrtpKeyError::kRandomUnavailable);
    }
    SrtpMasterKey key;
    // すべて 0 になる確率は 2^-240 で、実際には起きないが、parse と同じ条件を満たすまで引き直す。
    do {
        randombytes_buf(key.material_.data(), key.material_.size());
    } while (sodium_is_zero(key.material_.data(), key.material_.size()) != 0);
    return key;
}

}  // namespace vocroz::transport

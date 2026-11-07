// SPDX-License-Identifier: AGPL-3.0-only
//
// session_lock.h の実装。

#include "vocroz/update/session_lock.h"

#include <array>
#include <cstdint>

#include "detail/text_utils.h"

namespace vocroz::update {
namespace {

// FNV-1a（64 ビット）の定数。暗号学的な強度は要らず、同じパスから同じ名前を
// 再現できればよいので、実装が短く処理系に依存しないこのハッシュを使う。
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] char to_ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string session_lock_name(std::string_view data_dir_utf8) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const char c : data_dir_utf8) {
        hash ^= static_cast<unsigned char>(to_ascii_lower(c));
        hash *= kFnvPrime;
    }
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(hash >> (56 - i * 8));
    }
    return "Local\\VOCROZ.SessionActive." + detail::encode_lower_hex(bytes);
}

}  // namespace vocroz::update

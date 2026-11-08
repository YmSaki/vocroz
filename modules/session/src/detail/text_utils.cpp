// SPDX-License-Identifier: AGPL-3.0-only
//
// 外部から来る文字列の検査の実装。

#include "detail/text_utils.h"

#include <cstdint>

namespace vocroz::session::detail {
namespace {

// UTF-8 の 1 文字を読み、コードポイントを返す。不正なら false。
// 過長な表現、サロゲート、U+10FFFF を超える値を拒む（RFC 3629）。
bool decode_one(std::string_view text, std::size_t& pos, std::uint32_t& code_point) noexcept {
    const auto lead = static_cast<std::uint8_t>(text[pos]);
    std::size_t length = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80) {
        code_point = lead;
        ++pos;
        return true;
    }
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        minimum = 0x80;
        code_point = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        minimum = 0x800;
        code_point = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        minimum = 0x10000;
        code_point = lead & 0x07;
    } else {
        return false;
    }
    if (pos + length > text.size()) {
        return false;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<std::uint8_t>(text[pos + i]);
        if ((next & 0xC0) != 0x80) {
            return false;
        }
        code_point = (code_point << 6) | (next & 0x3F);
    }
    if (code_point < minimum || code_point > 0x10FFFF ||
        (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return false;
    }
    pos += length;
    return true;
}

bool is_control(std::uint32_t code_point) noexcept {
    return code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F);
}

}  // namespace

bool is_displayable_text(std::string_view text, std::size_t max_bytes) noexcept {
    if (text.size() > max_bytes) {
        return false;
    }
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::uint32_t code_point = 0;
        if (!decode_one(text, pos, code_point) || is_control(code_point)) {
            return false;
        }
    }
    return true;
}

}  // namespace vocroz::session::detail

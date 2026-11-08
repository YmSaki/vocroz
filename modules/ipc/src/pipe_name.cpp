// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の名前付きパイプの名前の導出。

#include "vocroz/ipc/pipe_name.h"

#include <cstdint>

#include "ipc_constants.h"

namespace vocroz::ipc {
namespace {

// 名前の衝突を避けるためのハッシュで、暗号学的な強度は要らない。
// 外部ライブラリを使わずに数行で書け、結果が処理系によらず決まる FNV-1a を使う。
constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

char normalize_char(char c) {
    if (c == '/') {
        return '\\';
    }
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

std::string to_hex(std::uint64_t value) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string hex(16, '0');
    for (std::size_t i = 0; i < hex.size(); ++i) {
        hex[hex.size() - 1 - i] = kDigits[(value >> (4U * i)) & 0xFU];
    }
    return hex;
}

}  // namespace

std::string make_engine_pipe_name(std::string_view user_sid, std::string_view data_dir_utf8) {
    std::string normalized;
    normalized.reserve(data_dir_utf8.size());
    for (const char c : data_dir_utf8) {
        normalized.push_back(normalize_char(c));
    }
    // 末尾の区切り文字の有無で別のパイプにならないようにする。ルート（`C:\`）でも同じ規則にする。
    while (!normalized.empty() && normalized.back() == '\\') {
        normalized.pop_back();
    }

    std::uint64_t hash = kFnvOffsetBasis;
    for (const char c : normalized) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kFnvPrime;
    }

    std::string name{kEnginePipePrefix};
    name.append(user_sid);
    name.push_back('-');
    name.append(to_hex(hash));
    return name;
}

}  // namespace vocroz::ipc

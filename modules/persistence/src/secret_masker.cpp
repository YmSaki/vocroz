// SPDX-License-Identifier: AGPL-3.0-only
//
// mask_sensitive_text() の実装。
// ログの 1 件ごとに呼ぶので、std::regex は使わず 1 回の走査で判定する。

#include "secret_masker.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vocroz::persistence {
namespace {

constexpr std::string_view kSecretMask = "<secret>";
constexpr std::string_view kInviteCodeMask = "<invite-code>";
constexpr std::string_view kIpMask = "<ip>";
constexpr std::string_view kUserMask = "<user>";

/// 鍵やトークンとみなす英数字の並びの最短の長さ。128 bit を 16 進数で書くと 32 文字になり、
/// 招待トークン（256 bit）や鍵（256 bit）はどの表記でもこれより長い。
constexpr std::size_t kMinSecretRunLength = 32;

/// 招待コードの文字数とハイフンの位置。
/// vocroz/session/invite_code.h の正規化した形と、人に見せる形に合わせる。
constexpr std::size_t kInviteCodeLength = 10;
constexpr std::size_t kInviteCodeGroupLength = 5;

bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool is_alpha(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// 鍵、トークン、招待コードを構成しうる文字（16 進数、base64url、ハイフン区切り）。
bool is_run_char(char c) noexcept {
    return is_digit(c) || is_alpha(c) || c == '_' || c == '-';
}

// 正規化した招待コードの文字。Crockford Base32 は I、L、O、U を使わないので、
// "CONNECTING" のような大文字の単語を招待コードと取り違えにくい。
bool is_crockford_char(char c) noexcept {
    if (is_digit(c)) {
        return true;
    }
    return c >= 'A' && c <= 'Z' && c != 'I' && c != 'L' && c != 'O' && c != 'U';
}

bool all_crockford(std::string_view text) noexcept {
    return std::ranges::all_of(text, is_crockford_char);
}

bool is_invite_code(std::string_view run) noexcept {
    if (run.size() == kInviteCodeLength) {
        return all_crockford(run);
    }
    if (run.size() == kInviteCodeLength + 1 && run[kInviteCodeGroupLength] == '-') {
        return all_crockford(run.substr(0, kInviteCodeGroupLength)) &&
               all_crockford(run.substr(kInviteCodeGroupLength + 1));
    }
    return false;
}

bool looks_like_secret(std::string_view run) noexcept {
    if (run.size() < kMinSecretRunLength) {
        return false;
    }
    return std::ranges::any_of(run, is_digit);
}

// "a.b.c.d" の 4 つの 10 進数（各 0〜255）を読む。
std::optional<std::array<std::uint32_t, 4>> parse_ipv4(std::string_view text) noexcept {
    std::array<std::uint32_t, 4> octets{};
    std::size_t index = 0;
    std::size_t digits = 0;
    for (const char c : text) {
        if (c == '.') {
            if (digits == 0 || ++index >= octets.size()) {
                return std::nullopt;
            }
            digits = 0;
            continue;
        }
        if (!is_digit(c) || ++digits > 3) {
            return std::nullopt;
        }
        octets[index] = octets[index] * 10 + static_cast<std::uint32_t>(c - '0');
        if (octets[index] > 255) {
            return std::nullopt;
        }
    }
    if (index != octets.size() - 1 || digits == 0) {
        return std::nullopt;
    }
    return octets;
}

// LAN、ループバック、リンクローカル、CGNAT のアドレスは個人を特定しないので残す。
bool is_private_ipv4(const std::array<std::uint32_t, 4>& o) noexcept {
    return o[0] == 10 || o[0] == 127 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
           (o[0] == 192 && o[1] == 168) || (o[0] == 169 && o[1] == 254) ||
           (o[0] == 100 && o[1] >= 64 && o[1] <= 127) || o[0] == 0;
}

char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// text の position から "\users\" か "/users/" が始まるか（大文字小文字は区別しない）。
bool starts_users_segment(std::string_view text, std::size_t position) noexcept {
    constexpr std::string_view kUsers = "users";
    const char separator = text[position];
    if (separator != '\\' && separator != '/') {
        return false;
    }
    if (position + kUsers.size() + 2 > text.size()) {
        return false;
    }
    for (std::size_t k = 0; k < kUsers.size(); ++k) {
        if (to_lower(text[position + 1 + k]) != kUsers[k]) {
            return false;
        }
    }
    return text[position + 1 + kUsers.size()] == separator;
}

bool ends_path_segment(char c) noexcept {
    return c == '\\' || c == '/' || c == ' ' || c == '"' || c == '\'' || c == '\t';
}

// ユーザーのプロファイルのフォルダ名を伏せる。
std::string mask_user_profiles(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (!starts_users_segment(text, i)) {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        const std::size_t name_begin = i + std::string_view{"/users/"}.size();
        std::size_t name_end = name_begin;
        while (name_end < text.size() && !ends_path_segment(text[name_end])) {
            ++name_end;
        }
        out.append(text.substr(i, name_begin - i));
        if (name_end > name_begin) {
            out.append(kUserMask);
        }
        i = name_end;
    }
    return out;
}

// position から始まる語がグローバルな IPv4 アドレスなら、その語の終わりの位置を返す。
std::optional<std::size_t> global_ipv4_end(std::string_view text, std::size_t position) noexcept {
    const bool at_word_start =
        position == 0 || (!is_run_char(text[position - 1]) && text[position - 1] != '.');
    if (!at_word_start || !is_digit(text[position])) {
        return std::nullopt;
    }
    std::size_t end = position;
    while (end < text.size() && (is_digit(text[end]) || text[end] == '.')) {
        ++end;
    }
    const bool at_word_end = end == text.size() || !is_run_char(text[end]);
    const auto octets = parse_ipv4(text.substr(position, end - position));
    if (!at_word_end || !octets || is_private_ipv4(*octets)) {
        return std::nullopt;
    }
    return end;
}

// 英数字の並び 1 つを、伏せるべきなら伏せ字に、そうでなければそのまま返す。
std::string_view mask_run(std::string_view run) noexcept {
    if (looks_like_secret(run)) {
        return kSecretMask;
    }
    if (is_invite_code(run)) {
        return kInviteCodeMask;
    }
    return run;
}

}  // namespace

std::string mask_sensitive_text(std::string_view text) {
    const std::string profiles_masked = mask_user_profiles(text);
    const std::string_view masked = profiles_masked;
    std::string out;
    out.reserve(masked.size());
    std::size_t i = 0;
    while (i < masked.size()) {
        if (const auto ip_end = global_ipv4_end(masked, i)) {
            out.append(kIpMask);
            i = *ip_end;
            continue;
        }
        if (!is_run_char(masked[i])) {
            out.push_back(masked[i]);
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < masked.size() && is_run_char(masked[j])) {
            ++j;
        }
        out.append(mask_run(masked.substr(i, j - i)));
        i = j;
    }
    return out;
}

}  // namespace vocroz::persistence

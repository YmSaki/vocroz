// SPDX-License-Identifier: AGPL-3.0-only
//
// 招待コード、招待 URL、vocroz:// の引数のパース。
// 招待コードの正規化の規則はランデブーサーバー（infra/rendezvous/internal/invite/code.go）と同じにする。
// 両者がずれると、親機に表示したコードを子機に入力しても解決できなくなるため。

#include "vocroz/session/invite_code.h"

#include <cstddef>

namespace vocroz::session {
namespace {

// Crockford Base32 の 32 文字。I、L、O、U を含まないので読み間違いが起きにくい。
constexpr std::string_view kCodeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// 表示時にハイフンで区切る文字数（XXXXX-XXXXX）。
constexpr std::size_t kCodeGroupSize = 5;

// 招待 URL のパスと、URL スキームの形（docs/adr/0006-invite-url-scheme.md）。
constexpr std::string_view kHttpsPrefix = "https://";
constexpr std::string_view kJoinPath = "/join";
constexpr std::string_view kSchemePrefix = "vocroz://";
constexpr std::string_view kSchemeJoin = "join";
constexpr std::string_view kCodeParameter = "code";

// 入力の上限。招待 URL として十分な長さで、control-protocol の invite の上限（2048 バイト）と同じ。
constexpr std::size_t kMaxInviteTextBytes = 2048;

std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view kSpaces = " \t\r\n";
    const auto first = text.find_first_not_of(kSpaces);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

bool starts_with_ignoring_case(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != prefix[i]) {
            return false;
        }
    }
    return true;
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// URL のパーセントエンコードを戻す。ブラウザが "-" などをエンコードして渡す場合があるため。
// 不正なエンコードは空文字列にして、招待コードとして拒否させる。
std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size()) {
            return {};
        }
        const int high = hex_value(text[i + 1]);
        const int low = hex_value(text[i + 2]);
        if (high < 0 || low < 0) {
            return {};
        }
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return out;
}

// https://<ホスト>/join#<コード> を読む。ホストは問わない（解決先は設定したランデブーサーバー）。
common::Result<InviteCode, InviteParseError> parse_https_invite(std::string_view url) {
    const auto rest = url.substr(kHttpsPrefix.size());
    const auto hash = rest.find('#');
    if (hash == std::string_view::npos) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    const auto before_fragment = rest.substr(0, hash);
    const auto slash = before_fragment.find('/');
    if (slash == 0 || slash == std::string_view::npos) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    auto path = before_fragment.substr(slash);
    if (path.size() > kJoinPath.size() && path.back() == '/') {
        path.remove_suffix(1);
    }
    if (path != kJoinPath) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    return parse_invite_code(percent_decode(rest.substr(hash + 1)));
}

// vocroz://join?code=<コード> を読む。join 以外の操作は拒否し、code 以外のクエリは無視する。
common::Result<InviteCode, InviteParseError> parse_scheme_invite(std::string_view uri) {
    auto rest = uri.substr(kSchemePrefix.size());
    const auto fragment = rest.find('#');
    if (fragment != std::string_view::npos) {
        rest = rest.substr(0, fragment);
    }
    const auto question = rest.find('?');
    auto action = rest.substr(0, question);
    if (!action.empty() && action.back() == '/') {
        action.remove_suffix(1);
    }
    if (action != kSchemeJoin || question == std::string_view::npos) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    auto query = rest.substr(question + 1);
    while (!query.empty()) {
        const auto amp = query.find('&');
        const auto pair = query.substr(0, amp);
        const auto eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == kCodeParameter) {
            return parse_invite_code(percent_decode(pair.substr(eq + 1)));
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query = query.substr(amp + 1);
    }
    return common::make_unexpected(InviteParseError::kInvalidFormat);
}

}  // namespace

std::string InviteCode::display() const {
    return value_.substr(0, kCodeGroupSize) + "-" + value_.substr(kCodeGroupSize);
}

common::Result<InviteCode, InviteParseError> parse_invite_code(std::string_view text) {
    std::string code;
    code.reserve(kInviteCodeLength);
    for (char c : text) {
        if (c == '-' || c == ' ') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (c == 'O') {
            c = '0';
        } else if (c == 'I' || c == 'L') {
            c = '1';
        }
        if (kCodeAlphabet.find(c) == std::string_view::npos || code.size() == kInviteCodeLength) {
            return common::make_unexpected(InviteParseError::kInvalidFormat);
        }
        code.push_back(c);
    }
    if (code.size() != kInviteCodeLength) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    return InviteCode{std::move(code)};
}

common::Result<InviteCode, InviteParseError> parse_invite(std::string_view text) {
    if (text.size() > kMaxInviteTextBytes) {
        return common::make_unexpected(InviteParseError::kInvalidFormat);
    }
    const auto trimmed = trim(text);
    if (starts_with_ignoring_case(trimmed, kHttpsPrefix)) {
        return parse_https_invite(trimmed);
    }
    if (starts_with_ignoring_case(trimmed, kSchemePrefix)) {
        return parse_scheme_invite(trimmed);
    }
    return parse_invite_code(trimmed);
}

std::string make_invite_url(std::string_view base_url, const InviteCode& code) {
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.remove_suffix(1);
    }
    std::string url{base_url};
    url.append(kJoinPath);
    url.push_back('#');
    url.append(code.display());
    return url;
}

}  // namespace vocroz::session

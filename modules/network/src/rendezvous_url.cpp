// SPDX-License-Identifier: AGPL-3.0-only
//
// ランデブーサーバーの URL の検証の実装。

#include "vocroz/network/rendezvous_url.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace vocroz::network {
namespace {

constexpr std::uint16_t kDefaultHttpsPort = 443;
constexpr std::uint16_t kDefaultHttpPort = 80;

common::Unexpected<NetworkError> invalid() {
    return common::make_unexpected(NetworkError::kInvalidServerUrl);
}

/// 先頭の scheme:// を大文字小文字を区別せずに取り除く。一致しなければ false。
bool consume_scheme(std::string_view& rest, std::string_view scheme) {
    if (rest.size() < scheme.size()) {
        return false;
    }
    const bool match = std::equal(scheme.begin(), scheme.end(), rest.begin(), [](char a, char b) {
        return a == std::tolower(static_cast<unsigned char>(b));
    });
    if (match) {
        rest.remove_prefix(scheme.size());
    }
    return match;
}

/// ホスト名に使える文字か（英数字、ハイフン、ドット）。
bool is_host_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.';
}

/// 角かっこの中の IPv6 のアドレスに使える文字か（16 進数字、コロン、IPv4 射影のドット）。
bool is_ipv6_literal_char(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0 || c == ':' || c == '.';
}

/// http を許すホスト（自分のマシンの中で閉じるもの）か。
bool is_loopback_host(std::string_view host) {
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "localhost" || lower == "127.0.0.1" || lower == "::1";
}

}  // namespace

common::Result<RendezvousUrl, NetworkError> parse_rendezvous_url(std::string_view url) {
    RendezvousUrl out;
    std::string_view rest = url;
    if (consume_scheme(rest, "https://")) {
        out.secure = true;
        out.port = kDefaultHttpsPort;
    } else if (consume_scheme(rest, "http://")) {
        out.secure = false;
        out.port = kDefaultHttpPort;
    } else {
        return invalid();
    }
    if (rest.find_first_of("?#@") != std::string_view::npos) {
        return invalid();
    }
    const auto path_begin = rest.find('/');
    std::string_view authority = rest.substr(0, path_begin);
    std::string_view path = path_begin == std::string_view::npos ? "" : rest.substr(path_begin);

    std::string_view host;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return invalid();
        }
        host = authority.substr(1, close - 1);
        if (host.empty() || !std::all_of(host.begin(), host.end(), is_ipv6_literal_char)) {
            return invalid();
        }
        authority.remove_prefix(close + 1);
    } else {
        const auto colon = authority.find(':');
        host = authority.substr(0, colon);
        if (host.empty() || !std::all_of(host.begin(), host.end(), is_host_name_char)) {
            return invalid();
        }
        authority.remove_prefix(host.size());
    }
    if (!authority.empty()) {
        // 残りは ":<ポート>" だけ。
        if (authority.front() != ':' || authority.size() == 1) {
            return invalid();
        }
        const auto digits = authority.substr(1);
        std::uint16_t port = 0;
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), port);
        if (error != std::errc{} || end != digits.data() + digits.size() || port == 0) {
            return invalid();
        }
        out.port = port;
    }
    // 招待トークンを平文で送るのは、通信が自分のマシンの外に出ないときだけにする。
    if (!out.secure && !is_loopback_host(host)) {
        return invalid();
    }
    // パスは印字できる ASCII だけにする。WinHTTP へ渡す UTF-16 への変換を 1 文字ずつで済ませ、
    // 空白や制御文字で要求の行を壊さないため。
    if (!std::all_of(path.begin(), path.end(), [](char c) { return c > ' ' && c < 0x7F; })) {
        return invalid();
    }
    while (!path.empty() && path.back() == '/') {
        path.remove_suffix(1);
    }
    out.host = std::string(host);
    out.base_path = std::string(path);
    return out;
}

}  // namespace vocroz::network

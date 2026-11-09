// SPDX-License-Identifier: AGPL-3.0-only
//
// rendezvous_host_from_url() の実装。

#include "server_url.h"

#include <array>

namespace vocroz::client {

std::optional<std::string> rendezvous_host_from_url(std::string_view base_url) {
    constexpr std::array<std::string_view, 2> kSchemes = {"https://", "http://"};
    std::string_view rest;
    for (const auto scheme : kSchemes) {
        if (base_url.starts_with(scheme)) {
            rest = base_url.substr(scheme.size());
            break;
        }
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    if (rest.front() == '[') {
        const auto close = rest.find(']');
        if (close == std::string_view::npos || close == 1) {
            return std::nullopt;
        }
        return std::string(rest.substr(1, close - 1));
    }
    const auto end = rest.find_first_of(":/?#");
    const auto host = rest.substr(0, end);
    if (host.empty() || host.find('@') != std::string_view::npos) {
        // 利用者情報（user@host）は受け付けない。設定の誤りか、別のホストへ誘導する URL なので。
        return std::nullopt;
    }
    return std::string(host);
}

}  // namespace vocroz::client

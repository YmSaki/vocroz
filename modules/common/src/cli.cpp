// SPDX-License-Identifier: AGPL-3.0-only
//
// cli.h の実装。

#include "vocroz/common/cli.h"

#include <algorithm>
#include <cstddef>

namespace vocroz::common {

Args make_args(int argc, const char* const* argv) noexcept {
    // argv[0] はプログラム名なので除外する。不正な argc でも空を返して安全側に倒す。
    if (argc <= 1 || argv == nullptr) {
        return {};
    }
    return {argv + 1, static_cast<std::size_t>(argc - 1)};
}

bool has_flag(Args args, std::string_view flag) noexcept {
    return std::ranges::any_of(
        args, [flag](const char* arg) { return arg != nullptr && std::string_view{arg} == flag; });
}

}  // namespace vocroz::common

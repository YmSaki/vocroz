// SPDX-License-Identifier: AGPL-3.0-only
//
// stderr_logger.h の実装。

#include "stderr_logger.h"

#include <cstdio>

namespace vocroz::engine {
namespace {

[[nodiscard]] const char* label_of(common::LogLevel level) noexcept {
    switch (level) {
        case common::LogLevel::kDebug:
            return "debug";
        case common::LogLevel::kInfo:
            return "info";
        case common::LogLevel::kWarning:
            return "warn";
        case common::LogLevel::kError:
            return "error";
    }
    return "?";
}

}  // namespace

void StderrLogger::log(common::LogLevel level, std::string_view message) noexcept {
    if (level < min_level_) {
        return;
    }
    const std::lock_guard lock{mutex_};
    // iostream は例外を投げうるので、noexcept の中では stdio を使う。
    std::fprintf(stderr, "pa-engine [%s] %.*s\n", label_of(level), static_cast<int>(message.size()),
                 message.data());
    std::fflush(stderr);
}

}  // namespace vocroz::engine

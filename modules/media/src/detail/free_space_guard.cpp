// SPDX-License-Identifier: AGPL-3.0-only
//
// FreeSpaceGuard の実装。

#include "detail/free_space_guard.h"

#include <utility>

namespace vocroz::media::detail {

FreeSpaceGuard::FreeSpaceGuard(FreeBytesQuery query, std::uint64_t min_free_bytes,
                               std::uint64_t check_interval)
    : query_(std::move(query)), min_free_bytes_(min_free_bytes), check_interval_(check_interval) {}

bool FreeSpaceGuard::admit(std::uint64_t bytes) {
    if (!query_) {
        return true;
    }
    if (!checked_ || written_since_check_ + bytes >= check_interval_) {
        last_free_ = query_();
        written_since_check_ = 0;
        checked_ = true;
    }
    if (last_free_) {
        // 前に調べてから書いた量を引いて見積もる。調べる間隔の間に、ほかのアプリが書いた量は
        // 見積もれないが、下限に余裕を持たせているので、次に調べるまでに使い切ることはない。
        const std::uint64_t used = written_since_check_ + bytes;
        if (*last_free_ < used || *last_free_ - used < min_free_bytes_) {
            return false;
        }
    }
    written_since_check_ += bytes;
    return true;
}

bool FreeSpaceGuard::is_below_minimum() {
    if (!query_) {
        return false;
    }
    last_free_ = query_();
    written_since_check_ = 0;
    checked_ = true;
    return last_free_ && *last_free_ < min_free_bytes_;
}

}  // namespace vocroz::media::detail

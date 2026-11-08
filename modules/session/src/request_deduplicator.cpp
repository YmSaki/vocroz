// SPDX-License-Identifier: AGPL-3.0-only
//
// request_id の重複の判定の実装。

#include "request_deduplicator.h"

namespace vocroz::session {

RequestDeduplicator::RequestDeduplicator(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

bool RequestDeduplicator::remember(std::string_view request_id) {
    std::string id{request_id};
    if (seen_.contains(id)) {
        return false;
    }
    if (order_.size() == capacity_) {
        seen_.erase(order_.front());
        order_.pop_front();
    }
    seen_.insert(id);
    order_.push_back(std::move(id));
    return true;
}

}  // namespace vocroz::session

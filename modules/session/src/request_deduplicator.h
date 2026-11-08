// SPDX-License-Identifier: AGPL-3.0-only
//
// 離散コマンドの request_id の重複を見分ける。
// 同じ request_id のコマンドが 2 回届いても（UI の再送や二重送信）、操作を 2 回適用しないため。
#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>

namespace vocroz::session {

/// 最近の request_id を、決まった件数だけ覚えておく。古いものから忘れる。
/// スレッド安全性: コントロールスレッドからだけ使う。
class RequestDeduplicator {
public:
    /// @param capacity 覚えておく件数（1 以上）
    explicit RequestDeduplicator(std::size_t capacity);

    /// request_id を初めて見たなら覚えて true、最近見たものなら false を返す。
    /// @param request_id コマンドの request_id
    [[nodiscard]] bool remember(std::string_view request_id);

private:
    std::size_t capacity_;
    std::deque<std::string> order_;
    std::unordered_set<std::string> seen_;
};

}  // namespace vocroz::session

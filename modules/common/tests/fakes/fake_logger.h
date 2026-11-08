// SPDX-License-Identifier: AGPL-3.0-only
//
// テスト用の Logger。出力されたログをメモリに記録し、
// テストが「どの重要度で何が出力されたか」を検証できるようにする。
#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "vocroz/common/ports/logger.h"

namespace vocroz::common {

/// 出力されたログを順に記録する Logger。
/// スレッド安全性: log() と entries() はどのスレッドからも同時に呼び出せる。
class FakeLogger final : public Logger {
public:
    /// 記録した 1 件のログ。
    struct Entry {
        LogLevel level;
        std::string message;
    };

    /// ログを記録する。テスト用なのでメモリ不足は考慮せず、確保に失敗したら終了させる。
    void log(LogLevel level, std::string_view message) noexcept override {
        const std::lock_guard lock{mutex_};
        entries_.push_back(Entry{level, std::string{message}});
    }

    /// これまでに記録したログを、出力された順に返す。
    [[nodiscard]] std::vector<Entry> entries() const {
        const std::lock_guard lock{mutex_};
        return entries_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

}  // namespace vocroz::common

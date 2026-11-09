// SPDX-License-Identifier: AGPL-3.0-only
//
// 標準エラー出力へのロガー。ログファイルへの出力（persistence、P1 の #20）が入るまでの仮の実装で、
// P1 のマージ後にコンポジションルートで差し替えて、このファイルは消す
// （docs/adr/0028-engine-assembly.md）。
#pragma once

#include <mutex>
#include <string_view>

#include "vocroz/common/ports/logger.h"

namespace vocroz::engine {

/// 標準エラー出力へ 1 行ずつ書くロガー。
/// スレッド安全性: log() はオーディオスレッド以外のどのスレッドからも同時に呼び出し可能。
class StderrLogger final : public common::Logger {
public:
    /// @param min_level これより低い重要度のログは出さない（--log-level）
    explicit StderrLogger(common::LogLevel min_level) noexcept : min_level_(min_level) {}

    void log(common::LogLevel level, std::string_view message) noexcept override;

private:
    common::LogLevel min_level_;
    // 複数のスレッドの行が混ざらないようにする。
    std::mutex mutex_;
};

}  // namespace vocroz::engine

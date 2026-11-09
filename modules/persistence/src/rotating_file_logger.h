// SPDX-License-Identifier: AGPL-3.0-only
//
// common の Logger を、大きさでローテーションするログファイルで実装する。
// 1 件ごとに秘密情報を伏せ（secret_masker.h）、改行を空白にして 1 行に収めてから書く。
#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>

#include "vocroz/common/ports/logger.h"
#include "vocroz/persistence/log_options.h"

namespace vocroz::persistence {

/// <base_name>.log に追記し、上限を超えそうになったら <base_name>.1.log、.2.log、… へずらす。
/// 一番古いファイルは消す。
/// スレッド安全性: log() はオーディオスレッド以外のどのスレッドからも同時に呼び出せる。
/// 内部で排他する。
class RotatingFileLogger final : public common::Logger {
public:
    /// 壁時計の取得元。
    /// ログの時刻はサポートで突き合わせるため、単調時計ではなく UTC の壁時計を使う。
    using WallClock = std::function<std::chrono::system_clock::time_point()>;

    /// @param options    出力先と上限
    /// @param wall_clock 時刻の取得元。テストでは固定の時刻を返す関数に差し替える
    RotatingFileLogger(LogOptions options, WallClock wall_clock);

    void log(common::LogLevel level, std::string_view message) noexcept override;

private:
    void write_line(common::LogLevel level, std::string_view message);
    void open_current();
    void rotate();
    [[nodiscard]] std::filesystem::path file_path(int index) const;

    const LogOptions options_;
    const WallClock wall_clock_;
    std::mutex mutex_;
    std::ofstream file_;
    std::uintmax_t current_bytes_ = 0;
};

}  // namespace vocroz::persistence

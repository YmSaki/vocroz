// SPDX-License-Identifier: AGPL-3.0-only
//
// RotatingFileLogger の実装。

#include "rotating_file_logger.h"

#include <string>
#include <system_error>
#include <utility>

#include "detail/time_format.h"
#include "secret_masker.h"

namespace vocroz::persistence {
namespace {

std::string_view level_label(common::LogLevel level) noexcept {
    switch (level) {
        case common::LogLevel::kDebug:
            return "DEBUG";
        case common::LogLevel::kInfo:
            return "INFO";
        case common::LogLevel::kWarning:
            return "WARN";
        case common::LogLevel::kError:
            return "ERROR";
    }
    return "UNKNOWN";
}

// 改行などの制御文字を空白にする。メッセージに改行が入ると、偽のログ行を作れてしまうため。
std::string to_single_line(std::string_view message) {
    std::string out{message};
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            c = ' ';
        }
    }
    return out;
}

}  // namespace

RotatingFileLogger::RotatingFileLogger(LogOptions options, WallClock wall_clock)
    : options_(std::move(options)), wall_clock_(std::move(wall_clock)) {
    std::error_code ec;
    std::filesystem::create_directories(options_.directory, ec);
    open_current();
}

void RotatingFileLogger::log(common::LogLevel level, std::string_view message) noexcept {
    if (level < options_.min_level) {
        return;
    }
    // ログの失敗で本来の処理を止めないため、メモリ不足なども含めて黙って捨てる（Logger の契約）。
    try {
        write_line(level, message);
    } catch (...) {  // NOLINT(bugprone-empty-catch): 失敗は呼び出し側に伝えない契約
    }
}

void RotatingFileLogger::write_line(common::LogLevel level, std::string_view message) {
    std::string line = detail::format_utc_timestamp(wall_clock_());
    line += " [";
    line += level_label(level);
    line += "] ";
    line += mask_sensitive_text(to_single_line(message));
    line += '\n';

    const std::lock_guard lock{mutex_};
    if (current_bytes_ > 0 && current_bytes_ + line.size() > options_.max_file_bytes) {
        rotate();
    }
    if (!file_.is_open()) {
        return;
    }
    file_.write(line.data(), static_cast<std::streamsize>(line.size()));
    // 強制終了の直前のログを残すため、1 件ごとに OS へ渡す（ディスクへの同期まではしない）。
    file_.flush();
    current_bytes_ += line.size();
}

void RotatingFileLogger::open_current() {
    const auto path = file_path(0);
    file_.open(path, std::ios::binary | std::ios::app);
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    current_bytes_ = ec ? 0 : size;
}

void RotatingFileLogger::rotate() {
    file_.close();
    std::error_code ec;
    const int count = options_.file_count < 1 ? 1 : options_.file_count;
    std::filesystem::remove(file_path(count - 1), ec);
    for (int index = count - 2; index >= 0; --index) {
        std::filesystem::rename(file_path(index), file_path(index + 1), ec);
    }
    // file_count が 1 なら、上の rename をせず今のファイルを消してから開き直す。
    std::filesystem::remove(file_path(0), ec);
    open_current();
}

std::filesystem::path RotatingFileLogger::file_path(int index) const {
    std::string name = options_.base_name;
    if (index > 0) {
        name += '.';
        name += std::to_string(index);
    }
    name += ".log";
    return options_.directory / name;
}

}  // namespace vocroz::persistence

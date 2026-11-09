// SPDX-License-Identifier: AGPL-3.0-only
//
// format_utc_timestamp() の実装。std::format の chrono 対応はコンパイラーによって揃わないため、
// C++20 の暦の型で分解して snprintf で書く。

#include "detail/time_format.h"

#include <cstdio>

namespace vocroz::persistence::detail {

std::string format_utc_timestamp(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto day = floor<days>(time);
    const year_month_day date{day};
    const hh_mm_ss clock{floor<milliseconds>(time - day)};
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ",
                  static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                  static_cast<unsigned>(date.day()), static_cast<int>(clock.hours().count()),
                  static_cast<int>(clock.minutes().count()),
                  static_cast<int>(clock.seconds().count()),
                  static_cast<int>(clock.subseconds().count()));
    return buffer;
}

}  // namespace vocroz::persistence::detail

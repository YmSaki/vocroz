// SPDX-License-Identifier: AGPL-3.0-only
//
// crash_dump_retention.h の実装。

#include "crash_dump_retention.h"

#include <algorithm>
#include <functional>

namespace vocroz::engine {

std::vector<std::string> select_stale_crash_dumps(const std::vector<std::string>& file_names,
                                                  std::size_t keep) {
    std::vector<std::string> dumps;
    for (const auto& name : file_names) {
        if (name.size() > kCrashDumpPrefix.size() + kCrashDumpExtension.size() &&
            name.starts_with(kCrashDumpPrefix) && name.ends_with(kCrashDumpExtension)) {
            dumps.push_back(name);
        }
    }
    if (dumps.size() <= keep) {
        return {};
    }
    // 名前は日時で始まるので、降順に並べると新しい順になる。
    std::sort(dumps.begin(), dumps.end(), std::greater<>{});
    return {dumps.begin() + static_cast<std::ptrdiff_t>(keep), dumps.end()};
}

}  // namespace vocroz::engine

// SPDX-License-Identifier: AGPL-3.0-only
//
// version_info.h の実装。製品名とバージョンはビルド時生成の version.h から取る。

#include "vocroz/common/version_info.h"

#include "vocroz/version.h"

namespace vocroz::common {

std::string version_line(std::string_view app_name) {
    std::string line{kProductName};
    line += ' ';
    line += app_name;
    line += ' ';
    line += kVersion;
    return line;
}

}  // namespace vocroz::common

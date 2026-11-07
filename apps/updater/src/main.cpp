// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-updater のエントリポイント。アップデーター。P5
// で署名検証とバージョン切り替え（docs/adr/0004）を実装する。
//   --version : バージョンを表示して終了

#include <iostream>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

int main(int argc, char** argv) {
    const vocroz::common::Args args = vocroz::common::make_args(argc, argv);

    if (vocroz::common::has_flag(args, "--version")) {
        std::cout << vocroz::common::version_line("pa-updater") << '\n';
        return 0;
    }

    std::cout << vocroz::common::version_line("pa-updater") << " (placeholder)\n";
    return 0;
}

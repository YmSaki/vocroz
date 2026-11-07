// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine のエントリポイント。親機のオーディオエンジン。P1 で音声グラフ、P2 で通信、P3
// でセッション管理を載せる。
//   --version : バージョンを表示して終了

#include <iostream>

#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

int main(int argc, char** argv) {
    const vocroz::common::Args args = vocroz::common::make_args(argc, argv);

    if (vocroz::common::has_flag(args, "--version")) {
        std::cout << vocroz::common::version_line("pa-engine") << '\n';
        return 0;
    }

    std::cout << vocroz::common::version_line("pa-engine") << " (placeholder)\n";
    return 0;
}

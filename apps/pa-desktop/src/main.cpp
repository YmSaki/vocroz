// SPDX-License-Identifier: AGPL-3.0-only
//
// remote-pa のエントリポイント。
//   --version : バージョンを表示して終了
//   --client  : 出演者用クライアント（子機モード）として起動
//   指定なし  : 親機の操作卓 UI として起動（P4 で実装）

#include <iostream>

#include "vocroz/client/client_app.h"
#include "vocroz/common/cli.h"
#include "vocroz/common/version_info.h"

int main(int argc, char** argv) {
    const vocroz::common::Args args = vocroz::common::make_args(argc, argv);

    if (vocroz::common::has_flag(args, "--version")) {
        std::cout << vocroz::common::version_line("remote-pa") << '\n';
        return 0;
    }
    if (vocroz::common::has_flag(args, "--client")) {
        return vocroz::client::run_client(args);
    }

    std::cout << vocroz::common::version_line("remote-pa") << " (placeholder)\n";
    return 0;
}

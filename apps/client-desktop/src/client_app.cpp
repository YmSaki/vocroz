// SPDX-License-Identifier: AGPL-3.0-only
//
// 子機モードの実装。P2 で音声スレッド、P4 で WebView2 UI をここから起動する。

#include "vocroz/client/client_app.h"

#include <iostream>

#include "vocroz/common/version_info.h"

namespace vocroz::client {

int run_client(common::Args /*args*/) {
    std::cout << common::version_line("client") << " (placeholder)\n";
    return 0;
}

}  // namespace vocroz::client

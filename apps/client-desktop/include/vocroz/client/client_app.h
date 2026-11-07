// SPDX-License-Identifier: AGPL-3.0-only
//
// 出演者用クライアント（子機）の入口。remote-pa が --client 指定時に呼び出す。
#pragma once

#include "vocroz/common/cli.h"

namespace vocroz::client {

/// 子機モードを実行する。
/// @param args プログラム名を除いたコマンドライン引数
/// @return プロセスの終了コード
[[nodiscard]] int run_client(common::Args args);

}  // namespace vocroz::client

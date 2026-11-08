// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-plugin-scanner の終了コード。pa-engine は 0 以外を異常終了として扱う。
#pragma once

namespace vocroz::plugin_scanner {

/// 正常終了。スキャン結果（プラグインが見つからなければ空の結果）を標準出力に書いた。
inline constexpr int kExitOk = 0;
/// 引数の誤り。
inline constexpr int kExitUsage = 2;

}  // namespace vocroz::plugin_scanner

// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin-host の利用側（コンポジションルート）が使う定数。
#pragma once

#include <chrono>

namespace vocroz::plugin_host {

/// スキャン用ヘルパーの、プラグインファイル 1 つあたりの制限時間の既定値。
/// 大きなサンプルライブラリを持つ VSTi は、初回のスキャンで読み込みに 10 秒を超えることがある。
/// 正常なプラグインを読み込み不可にしないよう、余裕を取る。
/// 一方で、止まったプラグイン 1 つで一覧の用意を何分も待たせないよう、30 秒にとどめる。
/// 決めた経緯は docs/adr/0032-plugin-catalog-and-loader.md に書いた。
inline constexpr std::chrono::seconds kDefaultScanTimeout{30};

}  // namespace vocroz::plugin_host

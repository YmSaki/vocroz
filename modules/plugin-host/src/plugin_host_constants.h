// SPDX-License-Identifier: AGPL-3.0-only
//
// plugin-host の内部だけで使う定数。
#pragma once

#include <cstddef>

namespace vocroz::plugin_host {

/// スキャン結果のスキーマのバージョン。フィールドの意味を変えたら上げる。
/// pa-engine と pa-plugin-scanner は同じ配布物に入るので、通常は食い違わない。
/// 更新の途中などで食い違ったら、古い形を推測で読まずに不正として扱う。
inline constexpr int kScanReportVersion = 1;

/// 1 つのファイルから受け付けるプラグインの数の上限。
/// シェルプラグイン（1 つのファイルに数百のプラグインを含む製品）でも収まる大きさにする。
inline constexpr std::size_t kMaxPluginsPerFile = 4096;

/// 名前、製造元、パスなどの文字列の長さの上限（バイト）。
/// Windows の長いパス（32,767 文字）は UTF-8 で 3 倍まで膨らむので、それを収める長さにする。
inline constexpr std::size_t kMaxStringBytes = 98'304;

/// スキャン結果で受け付けるチャンネル数の上限。
/// グラフに挿すときは 1〜2 チャンネルに設定し直すが、記述としては多チャンネルの製品も受け付ける。
inline constexpr std::size_t kMaxReportedChannels = 128;

/// ヘルパーの出力の長さの上限（バイト）。上の件数と長さの上限に収まる結果より十分大きい値にし、
/// それを超えて書き続ける不良ヘルパーを止める。
inline constexpr std::size_t kMaxHelperOutputBytes = 64U * 1024U * 1024U;

}  // namespace vocroz::plugin_host

// SPDX-License-Identifier: AGPL-3.0-only
//
// update モジュール内だけで使う定数。
#pragma once

#include <cstdint>
#include <string_view>

namespace vocroz::update {

/// マニフェストの形式の版。キーの追加や意味の変更をしたら上げ、古い updater に誤読させない。
inline constexpr std::string_view kManifestFormat = "1";

/// マニフェストの product に書く値。別製品の署名付きマニフェストの流用を防ぐ。
inline constexpr std::string_view kProductId = "vocroz";

/// 更新パッケージの上限（512 MiB）。現在の配布物は数十 MiB で、JUCE と VST3 関連を
/// 含めても 1 桁以上の余裕がある。不正なマニフェストでディスクを使い切らせないための上限。
inline constexpr std::uint64_t kMaxPackageSize = 512ULL * 1024ULL * 1024ULL;

/// 署名の行のキー。
inline constexpr std::string_view kSignatureKey = "signature";

}  // namespace vocroz::update

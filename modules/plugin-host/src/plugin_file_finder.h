// SPDX-License-Identifier: AGPL-3.0-only
//
// 探す場所の下にある VST3 のプラグインファイルを列挙する。
// JUCE を使わず std::filesystem だけで書き、Linux の CI でもテストする。
#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "vocroz/session/plugin_scan_record.h"

namespace vocroz::plugin_host {

/// directories の下を再帰的にたどり、拡張子が .vst3 のファイルとバンドルを列挙する。
/// バンドルの中はたどらない（中のファイルはバンドル全体で 1 つのプラグインファイルとして扱う）。
/// バンドルの更新日時は中のファイルの最も新しいもの、サイズは中のファイルの合計にする。
/// 更新でバンドルの中のバイナリだけが入れ替わっても、変更として見つけるため。
/// 読めないディレクトリと、存在しない探す場所は飛ばす。
/// @param directories 探す場所
/// @return 見つけたファイル（パスの順。同じファイルは 1 回だけ）
/// スレッド安全性: ブロックする。オーディオスレッドからは呼ばない。
[[nodiscard]] std::vector<session::PluginFile> find_plugin_files(
    std::span<const std::filesystem::path> directories);

}  // namespace vocroz::plugin_host

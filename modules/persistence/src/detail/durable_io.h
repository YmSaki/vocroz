// SPDX-License-Identifier: AGPL-3.0-only
//
// ディスクまで書き切るファイル操作。OS ごとの実装を src/win/ と src/posix/ に置く
// （docs/adr/0027-persistence.md）。
// 標準ライブラリの std::ofstream や std::filesystem::rename は、
// OS のキャッシュに書いただけで戻るため、電源断や強制終了の直後に
// 「新しい名前で中身が空のファイル」が残りうる。
// アトミックな保存は「書いた内容がディスクに届いてから名前を差し替える」順序に依存するので、
// その 2 つだけを OS の API で行う。
#pragma once

#include <filesystem>
#include <span>

#include "vocroz/common/result.h"

namespace vocroz::persistence::detail {

/// ディスク操作の失敗。原因の区別は呼び出し側で使わないので 1 種類にする。
enum class IoError {
    kFailed,  ///< 書き込み、フラッシュ、名前の変更のいずれかに失敗した
};

/// path に bytes を書き、ディスクまでフラッシュしてから閉じる。既存のファイルは上書きする。
/// @param path  書き込み先
/// @param bytes 書き込む内容
/// @return 失敗したら kFailed。途中まで書かれたファイルが残ることがある
/// スレッド安全性: 同じ path を複数スレッドから同時に書かない。
[[nodiscard]] common::Result<void, IoError> write_file_durably(
    const std::filesystem::path& path, std::span<const char> bytes) noexcept;

/// from を to に名前を変えて置き換え、名前の変更がディスクに届くまで待つ。
/// to が既にあれば、置き換えはアトミックに行う。
/// どの時点で止まっても、to は古い内容か新しい内容のどちらかになる。
/// @param from 置き換える元のファイル
/// @param to   置き換え先
/// @return 失敗したら kFailed。from と to は呼び出し前の状態のまま残る
[[nodiscard]] common::Result<void, IoError> replace_file_durably(
    const std::filesystem::path& from, const std::filesystem::path& to) noexcept;

}  // namespace vocroz::persistence::detail

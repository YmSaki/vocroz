// SPDX-License-Identifier: AGPL-3.0-only
//
// 録音のファイル名の組み立て（docs/adr/0043-recording-and-media-player.md）。
// ファイル名にはストリップやバスの表示名（子機の名前を含む）を使うので、Windows でファイル名に
// 使えない文字や名前を置き換え、テイクのフォルダの外を指す名前（".." など）を作らない。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace vocroz::session::detail {

/// 表示名をファイル名の一部に使える形にする。
/// - Windows でファイル名に使えない文字（< > : " / \ | ? * と制御文字）を "_" にする
/// - 末尾の空白とピリオドを取り除く（Windows はファイル名の末尾のそれらを黙って落とすため）
/// - 予約されたデバイス名（CON、PRN、AUX、NUL、COM1〜9、LPT1〜9）は先頭に "_" を付ける
/// - max_bytes を超えたら、UTF-8 の文字の途中で切らずに切り詰める
/// - 空になったら fallback を使う
/// @param name      表示名（正しい UTF-8 であること）
/// @param max_bytes 上限のバイト数（1 以上）
/// @param fallback  空になったときの名前（そのまま使える名前であること）
/// スレッド安全性: どのスレッドからも呼び出し可能（状態を持たない）。
[[nodiscard]] std::string to_file_name_part(std::string_view name, std::size_t max_bytes,
                                            std::string_view fallback);

}  // namespace vocroz::session::detail

// SPDX-License-Identifier: AGPL-3.0-only
//
// pa-engine の名前付きパイプの名前の導出。
// remote-pa と pa-engine が同じ規則で名前を作れるよう、規則をここ 1 か所に置く。
// 名前は引数で渡さず、ユーザーと --data-dir から導出する（docs/adr/0007-launch-interface.md、
// docs/adr/0020-ipc-pipe-and-webui-bridge.md）。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::ipc {

/// pa-engine の名前付きパイプの名前を作る。
/// 形式は `\\.\pipe\vocroz-engine-<SID>-<データフォルダのハッシュ>`。
/// ハッシュは、区切り文字を `\` にそろえて末尾の区切り文字を除き、ASCII の英字を小文字にした
/// UTF-8 のパスの FNV-1a（64 ビット、16 桁の小文字の 16 進数）。
/// Windows
/// のパスは大文字と小文字を区別しないので、表記の違いで別のエンジンにつながらないようにする。
/// @param user_sid      ログイン中ユーザーの SID の文字列（例: `S-1-5-21-...`）
/// @param data_dir_utf8 --data-dir の絶対パス（UTF-8）
/// @return パイプの名前（UTF-8）
/// スレッド安全性: 状態を持たないので、どのスレッドからも同時に呼び出し可能。
[[nodiscard]] std::string make_engine_pipe_name(std::string_view user_sid,
                                                std::string_view data_dir_utf8);

}  // namespace vocroz::ipc

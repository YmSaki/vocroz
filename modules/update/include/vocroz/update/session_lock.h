// SPDX-License-Identifier: AGPL-3.0-only
//
// 「セッションが稼働中」を pa-updater に伝える名前付きミューテックスの名前。
// pa-engine はセッションの稼働中にこの名前のミューテックスを保持し、pa-updater は
// その有無で本番中かどうかを判定する。両者が同じ名前を使うため、名前の規則はここだけで定義する。
#pragma once

#include <string>
#include <string_view>

namespace vocroz::update {

/// データフォルダごとのミューテックスの名前を返す。
/// "Local\" を付けてログオンセッション内に限り、データフォルダのハッシュで範囲を分ける
/// （--data-dir
/// を変えた検証用のインスタンスが、本番のインスタンスの更新を止めないようにするため）。 パスは
/// ASCII の大文字と小文字を区別せずに扱う（Windows のパスの比較に合わせる）。
/// @param data_dir_utf8 データフォルダの絶対パス（UTF-8）
/// @return "Local\VOCROZ.SessionActive.<16 桁の 16 進数>"。スレッド安全。
[[nodiscard]] std::string session_lock_name(std::string_view data_dir_utf8);

}  // namespace vocroz::update
